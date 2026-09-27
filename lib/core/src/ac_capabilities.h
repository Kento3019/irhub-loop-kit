// エアコンの列挙と、使える値（温度範囲・選択肢。D1 で確定）
// 根拠：docs/design/02-ac-state.md 1節。core の他モジュールを include しない
#pragma once
#include <cstdint>

namespace irhub {

// ---- 列挙（確定。stdAc の列挙子と1対1） ----------------------------------------------
// 要件 F1 のモードは4つ。stdAc の kFan（送風）と kOff は使わない。
enum class AcMode : uint8_t { Auto = 0, Cool = 1, Dry = 2, Heat = 3 };
constexpr int kAcModeCount = 4;  // 配列の添字は static_cast<int>(AcMode)

enum class AcFan : uint8_t { Auto, Min, Low, Medium, High, Max };  // Min = 静音
enum class AcSwingV : uint8_t { Off, Auto, Highest, High, Middle, Low, Lowest };
enum class AcSwingH : uint8_t { Off, Auto, LeftMax, Left, Middle, Right, RightMax, Wide };

namespace cap {

// ---- 使える値（D1 で確定。F1-VALUES の値はこのファイルだけに置く） -------------------
constexpr int kTempMinC = 16;   // 確定(D1)
constexpr int kTempMaxC = 30;   // 確定(D1)。HITACHI_AC296 の温度欄は 5 ビットで 31 まで
constexpr int kTempStepC = 1;   // 要件 F1（決定）：1℃刻み

// モードごとに温度を指定できるか（添字は AcMode）。false のモードでは温度を受け付けず、見せない
constexpr bool kTempSupported[kAcModeCount] = {
    false,  // Auto  確定(D1)：自動は温度指定なし（受信で温度欄 0）
    true,   // Cool
    true,   // Dry   受信で除湿の温度欄に 28℃ が載っている
    true,   // Heat
};

// 風量の選択肢（画面に並べる順）。確定(D1)：自動・静音(Min)・弱・中・強
constexpr AcFan kFanChoices[] = {
    AcFan::Auto, AcFan::Min, AcFan::Low, AcFan::Medium, AcFan::High,
};
// 風向は作らない（D1）。状態の項目は残し、選択肢は Off だけ
constexpr AcSwingV kSwingVChoices[] = {AcSwingV::Off};
constexpr AcSwingH kSwingHChoices[] = {AcSwingH::Off};
constexpr AcSwingV kSwingVDefault = AcSwingV::Off;
constexpr AcSwingH kSwingHDefault = AcSwingH::Off;

// ---- 以下は値ではなく道具（確定） -------------------------------------------------------
constexpr int kFanChoiceCount = sizeof(kFanChoices) / sizeof(kFanChoices[0]);
constexpr int kSwingVChoiceCount = sizeof(kSwingVChoices) / sizeof(kSwingVChoices[0]);
constexpr int kSwingHChoiceCount = sizeof(kSwingHChoices) / sizeof(kSwingHChoices[0]);

constexpr bool tempSupported(AcMode m) { return kTempSupported[static_cast<int>(m)]; }
constexpr bool tempInRange(int c) { return c >= kTempMinC && c <= kTempMaxC; }
constexpr bool fanSupported(AcFan f) {
  for (int i = 0; i < kFanChoiceCount; ++i)
    if (kFanChoices[i] == f) return true;
  return false;
}
constexpr bool swingVSupported(AcSwingV v) {
  for (int i = 0; i < kSwingVChoiceCount; ++i)
    if (kSwingVChoices[i] == v) return true;
  return false;
}
constexpr bool swingHSupported(AcSwingH h) {
  for (int i = 0; i < kSwingHChoiceCount; ++i)
    if (kSwingHChoices[i] == h) return true;
  return false;
}

}  // namespace cap
}  // namespace irhub
