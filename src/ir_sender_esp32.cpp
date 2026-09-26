// IrSenderEsp32 のスタブ（I-05）。赤外線は出さず、シリアルに出すだけ。本物は I-06。
// 根拠：docs/design/06-runtime.md 2.4
#include "ir_sender_esp32.h"

#include <Arduino.h>

#include "pins.h"

namespace irhub {

IrSenderEsp32::IrSenderEsp32() {}  // 何もしない（N-BOOT）

void IrSenderEsp32::begin() {
  // IO4 を出力・LOW に固定する。送信はしない（N-BOOT、HW-PINS）
  pinMode(pins::kIrSend, OUTPUT);
  digitalWrite(pins::kIrSend, LOW);
}

bool IrSenderEsp32::sendAc(const AcState& state) {
  Serial.printf("[ir] sendAc power=%d mode=%d temp=%d fan=%d\n", static_cast<int>(state.power),
                static_cast<int>(state.mode), static_cast<int>(state.tempC), static_cast<int>(state.fan));
  return true;
}

}  // namespace irhub
