// エアコンの列挙と、受信結果待ちの値（使える温度範囲・選択肢）
// 根拠：docs/design/02-ac-state.md 1節。core の他モジュールを include しない
#pragma once
#include <cstdint>

namespace irhub {

// ---- 列挙（確定。stdAc の列挙子と1対1） ----------------------------------------------
// 要件 F1 のモードは4つ。stdAc の kFan（送風）と kOff は使わない。
enum class AcMode : uint8_t { Auto = 0, Cool = 1, Dry = 2, Heat = 3 };
constexpr int kAcModeCount = 4;  // 配列の添字は static_cast<int>(AcMode)

enum class AcFan : uint8_t { Auto, Min, Low, Medium, High, Max };
enum class AcSwingV : uint8_t { Off, Auto, Highest, High, Middle, Low, Lowest };
enum class AcSwingH : uint8_t { Off, Auto, LeftMax, Left, Middle, Right, RightMax, Wide };

namespace cap {

// ---- 受信結果待ちの値（フェーズ1で確定する。値だけを直す） ---------------------------
constexpr int kTempMinC = 16;   // PENDING(F1-VALUES) 要件の仮：16℃
constexpr int kTempMaxC = 30;   // PENDING(F1-VALUES) 要件の仮：30℃
constexpr int kTempStepC = 1;   // 要件 F1（決定）：1℃刻み。PENDING ではない

// モードごとに温度を指定できるか（添字は AcMode）。false のモードでは温度を持たず送らない（F2 除湿の但し書き）
constexpr bool kTempSupported[kAcModeCount] = {
    true,  // Auto  PENDING(F1-VALUES)
    true,  // Cool  PENDING(F1-VALUES)
    true,  // Dry   PENDING(F1-VALUES) 機種が除湿で温度指定できなければ false にする
    true,  // Heat  PENDING(F1-VALUES)
};

// 風量の選択肢（画面に並べる順）
constexpr AcFan kFanChoices[] = {
    AcFan::Auto, AcFan::Low, AcFan::Medium, AcFan::High,  // PENDING(F1-VALUES)
};
// 風向 上下の選択肢
constexpr AcSwingV kSwingVChoices[] = {
    AcSwingV::Off, AcSwingV::Auto,  // PENDING(F1-VALUES)
};
// 風向 左右の選択肢
constexpr AcSwingH kSwingHChoices[] = {
    AcSwingH::Off,  // PENDING(F1-VALUES)
};

// 「機種の標準」の風向（F2 の表の風向欄。どのモードでも同じ）
constexpr AcSwingV kSwingVDefault = AcSwingV::Off;  // PENDING(F1-VALUES)
constexpr AcSwingH kSwingHDefault = AcSwingH::Off;  // PENDING(F1-VALUES)

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
