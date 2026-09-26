// ピン番号の唯一の定義（HW-PINS）。根拠：docs/design/01-architecture.md 9節
#pragma once
#include <cstdint>

namespace irhub::pins {
constexpr uint8_t kIrSend = 4;        // IO4 → 1kΩ → 2SC1815 ベース。赤外線 LED（エアコン向き。2個目は予備）
constexpr uint8_t kI2cSda = 21;       // DHT20 SDA（10kΩ で 3.3V へプルアップ）
constexpr uint8_t kI2cScl = 22;       // DHT20 SCL（10kΩ で 3.3V へプルアップ）
constexpr uint8_t kDht20Addr = 0x38;  // DHT20 の I2C アドレス（固定。ライブラリの中で使われる）
// IO14（受信モジュール OUT）は本体ファームでは使わない。フェーズ1の env:dump（tools/phase1_dump/）だけが使う
}  // namespace irhub::pins
