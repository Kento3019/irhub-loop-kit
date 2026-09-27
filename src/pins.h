// ピン番号の唯一の定義（HW-PINS）。根拠：docs/design/01-architecture.md 9節（要件 v0.5・配線v2）
#pragma once
#include <cstdint>

namespace irhub::pins {
// IO23 → 1kΩ → 2SC1815 ベース。赤外線 LED 2個を直列で駆動（5V → 100Ω×2 並列で 50Ω → LED 2個直列 → 2SC1815。約 40mA）
constexpr uint8_t kIrSend = 23;
constexpr uint8_t kI2cSda = 21;       // AHT25 SDA（10kΩ で 3.3V へプルアップ）
constexpr uint8_t kI2cScl = 22;       // AHT25 SCL（10kΩ で 3.3V へプルアップ）
constexpr uint8_t kDht20Addr = 0x38;  // AHT25 の I2C アドレス（固定。DHT20 ライブラリの中で使われる。定数名はライブラリに合わせて DHT20 のまま）
// IO14 は未接続。受信モジュールは配線v2で外した（フェーズ1の env:dump（tools/phase1_dump/）で受信に使ったピン）。本体ファームでは使わない
}  // namespace irhub::pins
