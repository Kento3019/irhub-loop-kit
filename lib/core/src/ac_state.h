// エアコンの状態モデル（型・文字列変換・部分更新）
// 根拠：docs/design/02-ac-state.md 2〜4節。状態は RAM のメンバだけに持つ（N-STATE）
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

#include "ac_capabilities.h"

namespace irhub {

// モード1つ分の設定。モードごとに1つずつ持つ（F2「モードごとに最後に使った設定」）
struct AcSettings {
  int8_t tempC = 26;  // tempSupported(mode)==false のモードでは使わない（保持だけする）
  AcFan fan = AcFan::Auto;
  AcSwingV swingV = AcSwingV::Off;
  AcSwingH swingH = AcSwingH::Off;
};

// 今の状態一式。IIrSender::sendAc に渡し、/api/status で返すもの
struct AcState {
  bool power = false;
  AcMode mode = AcMode::Cool;
  bool hasTemp = true;  // == cap::tempSupported(mode)。false のとき tempC は送らない・見せない
  int8_t tempC = 26;
  AcFan fan = AcFan::Auto;
  AcSwingV swingV = AcSwingV::Off;
  AcSwingH swingH = AcSwingH::Off;
};
// 既定のメンバ初期化子は「値初期化で壊れた値にならない」ためだけのもの。
// 起動時の値は AcModel のコンストラクタが初期値表から作る。

// 部分更新。値の入っている項目だけを変える
struct AcPatch {
  std::optional<bool> power;
  std::optional<AcMode> mode;
  std::optional<int> tempC;  // int8_t に詰める前に範囲を検証するため int
  std::optional<AcFan> fan;
  std::optional<AcSwingV> swingV;
  std::optional<AcSwingH> swingH;
  bool empty() const { return !power && !mode && !tempC && !fan && !swingV && !swingH; }
};

// 検証の結果
enum class AcError : uint8_t {
  None,
  EmptyPatch,        // 項目が1つも無い
  TempOutOfRange,    // cap::kTempMinC..kTempMaxC の外
  TempNotSupported,  // 更新後のモードで温度を指定できない
  FanNotSupported,   // cap::kFanChoices に無い
  SwingVNotSupported,
  SwingHNotSupported,
};
const char* errorMessage(AcError e);  // None は ""

// 文字列 ⇔ 列挙（不明な文字列は nullopt。選択肢に入っているかは見ない）
std::optional<AcMode> parseAcMode(std::string_view s);
std::optional<AcFan> parseAcFan(std::string_view s);
std::optional<AcSwingV> parseAcSwingV(std::string_view s);
std::optional<AcSwingH> parseAcSwingH(std::string_view s);
const char* toString(AcMode m);
const char* toString(AcFan f);
const char* toString(AcSwingV v);
const char* toString(AcSwingH h);

// F2 の初期値
AcSettings initialSettings(AcMode m);

class AcModel {
 public:
  AcModel();  // 初期状態にする。送信はしない（送信の手段を持たない。N-BOOT）
  AcError validate(const AcPatch& p) const;  // 状態を変えずに検証だけする
  AcError apply(const AcPatch& p);  // validate が None なら更新して None。それ以外は何も変えずにその値
  const AcState& state() const { return state_; }
  const AcSettings& settingsFor(AcMode m) const { return perMode_[static_cast<int>(m)]; }

 private:
  void rebuildState();  // power_/mode_/perMode_ から state_ を作り直す
  bool power_;
  AcMode mode_;
  AcSettings perMode_[kAcModeCount];
  AcState state_;  // rebuildState() の結果（参照を返すための写し）
};

}  // namespace irhub
