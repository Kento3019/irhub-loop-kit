// エアコンの状態モデルの実装。根拠：docs/design/02-ac-state.md 2〜4節
#include "ac_state.h"

namespace irhub {
namespace {

// F2 の初期値（この表だけに置く）。添字は AcMode（Auto, Cool, Dry, Heat）
// 風向は「機種の標準」＝ cap::kSwingVDefault / kSwingHDefault
constexpr AcSettings kInitialSettings[kAcModeCount] = {
    {25, AcFan::Auto, cap::kSwingVDefault, cap::kSwingHDefault},  // 自動 仮(F2) 温度は「機種の標準」。推測で 25
    {26, AcFan::Auto, cap::kSwingVDefault, cap::kSwingHDefault},  // 冷房 決定
    {26, AcFan::Auto, cap::kSwingVDefault, cap::kSwingHDefault},  // 除湿 仮(F2) 温度を指定できない機種では送らない
    {20, AcFan::Auto, cap::kSwingVDefault, cap::kSwingHDefault},  // 暖房 仮(F2)
};
constexpr AcMode kInitialMode = AcMode::Cool;  // 仮(F2) 起動直後のモード。推測で冷房
constexpr bool kInitialPower = false;  // 起動直後は停止として持つ（N-BOOT：送っていないので実機は不明）

// 選択肢が変わったときに初期値が選択肢・範囲の外になったら、ビルドで気付くようにする
constexpr bool allInitialValid() {
  for (int i = 0; i < kAcModeCount; ++i) {
    const AcSettings& s = kInitialSettings[i];
    if (!cap::tempInRange(s.tempC)) return false;
    if (!cap::fanSupported(s.fan)) return false;
    if (!cap::swingVSupported(s.swingV)) return false;
    if (!cap::swingHSupported(s.swingH)) return false;
  }
  return true;
}
static_assert(allInitialValid(), "initial settings must be within the capabilities");

}  // namespace

const char* errorMessage(AcError e) {
  switch (e) {
    case AcError::None: return "";
    case AcError::EmptyPatch: return "no ac fields";
    case AcError::TempOutOfRange: return "temp out of range";
    case AcError::TempNotSupported: return "temp not supported in this mode";
    case AcError::FanNotSupported: return "fan not supported";
    case AcError::SwingVNotSupported: return "swingV not supported";
    case AcError::SwingHNotSupported: return "swingH not supported";
  }
  return "";
}

// ---- 文字列変換 ----------------------------------------------------------------------

std::optional<AcMode> parseAcMode(std::string_view s) {
  if (s == "auto") return AcMode::Auto;
  if (s == "cool") return AcMode::Cool;
  if (s == "dry") return AcMode::Dry;
  if (s == "heat") return AcMode::Heat;
  return std::nullopt;
}

std::optional<AcFan> parseAcFan(std::string_view s) {
  if (s == "auto") return AcFan::Auto;
  if (s == "min") return AcFan::Min;
  if (s == "low") return AcFan::Low;
  if (s == "medium") return AcFan::Medium;
  if (s == "high") return AcFan::High;
  if (s == "max") return AcFan::Max;
  return std::nullopt;
}

std::optional<AcSwingV> parseAcSwingV(std::string_view s) {
  if (s == "off") return AcSwingV::Off;
  if (s == "auto") return AcSwingV::Auto;
  if (s == "highest") return AcSwingV::Highest;
  if (s == "high") return AcSwingV::High;
  if (s == "middle") return AcSwingV::Middle;
  if (s == "low") return AcSwingV::Low;
  if (s == "lowest") return AcSwingV::Lowest;
  return std::nullopt;
}

std::optional<AcSwingH> parseAcSwingH(std::string_view s) {
  if (s == "off") return AcSwingH::Off;
  if (s == "auto") return AcSwingH::Auto;
  if (s == "leftMax") return AcSwingH::LeftMax;
  if (s == "left") return AcSwingH::Left;
  if (s == "middle") return AcSwingH::Middle;
  if (s == "right") return AcSwingH::Right;
  if (s == "rightMax") return AcSwingH::RightMax;
  if (s == "wide") return AcSwingH::Wide;
  return std::nullopt;
}

const char* toString(AcMode m) {
  switch (m) {
    case AcMode::Auto: return "auto";
    case AcMode::Cool: return "cool";
    case AcMode::Dry: return "dry";
    case AcMode::Heat: return "heat";
  }
  return "";
}

const char* toString(AcFan f) {
  switch (f) {
    case AcFan::Auto: return "auto";
    case AcFan::Min: return "min";
    case AcFan::Low: return "low";
    case AcFan::Medium: return "medium";
    case AcFan::High: return "high";
    case AcFan::Max: return "max";
  }
  return "";
}

const char* toString(AcSwingV v) {
  switch (v) {
    case AcSwingV::Off: return "off";
    case AcSwingV::Auto: return "auto";
    case AcSwingV::Highest: return "highest";
    case AcSwingV::High: return "high";
    case AcSwingV::Middle: return "middle";
    case AcSwingV::Low: return "low";
    case AcSwingV::Lowest: return "lowest";
  }
  return "";
}

const char* toString(AcSwingH h) {
  switch (h) {
    case AcSwingH::Off: return "off";
    case AcSwingH::Auto: return "auto";
    case AcSwingH::LeftMax: return "leftMax";
    case AcSwingH::Left: return "left";
    case AcSwingH::Middle: return "middle";
    case AcSwingH::Right: return "right";
    case AcSwingH::RightMax: return "rightMax";
    case AcSwingH::Wide: return "wide";
  }
  return "";
}

AcSettings initialSettings(AcMode m) { return kInitialSettings[static_cast<int>(m)]; }

// ---- AcModel -------------------------------------------------------------------------

AcModel::AcModel() : power_(kInitialPower), mode_(kInitialMode) {
  for (int i = 0; i < kAcModeCount; ++i) perMode_[i] = kInitialSettings[i];
  rebuildState();
}

AcError AcModel::validate(const AcPatch& p) const {
  if (p.empty()) return AcError::EmptyPatch;
  if (p.tempC) {
    const AcMode m = p.mode ? *p.mode : mode_;
    if (!cap::tempSupported(m)) return AcError::TempNotSupported;
    if (!cap::tempInRange(*p.tempC)) return AcError::TempOutOfRange;
  }
  if (p.fan && !cap::fanSupported(*p.fan)) return AcError::FanNotSupported;
  if (p.swingV && !cap::swingVSupported(*p.swingV)) return AcError::SwingVNotSupported;
  if (p.swingH && !cap::swingHSupported(*p.swingH)) return AcError::SwingHNotSupported;
  return AcError::None;
}

AcError AcModel::apply(const AcPatch& p) {
  const AcError e = validate(p);
  if (e != AcError::None) return e;
  if (p.power) power_ = *p.power;
  if (p.mode) mode_ = *p.mode;  // 温度・風量・風向は perMode_[新しいモード] から復元される
  AcSettings& s = perMode_[static_cast<int>(mode_)];
  if (p.tempC) s.tempC = static_cast<int8_t>(*p.tempC);  // 範囲は validate 済み
  if (p.fan) s.fan = *p.fan;
  if (p.swingV) s.swingV = *p.swingV;
  if (p.swingH) s.swingH = *p.swingH;
  rebuildState();
  return AcError::None;
}

void AcModel::rebuildState() {
  const AcSettings& s = perMode_[static_cast<int>(mode_)];
  state_.power = power_;
  state_.mode = mode_;
  state_.hasTemp = cap::tempSupported(mode_);
  state_.tempC = s.tempC;  // hasTemp=false でも値は入れておく
  state_.fan = s.fan;
  state_.swingV = s.swingV;
  state_.swingH = s.swingH;
}

}  // namespace irhub
