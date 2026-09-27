// IrSenderEsp32（I-06）。AcState を stdAc::state_t に変換し、IRac で状態一式を1回送る（F1、PROTO）。
// 根拠：docs/design/02-ac-state.md 7節、docs/design/06-runtime.md 2.4
// IRremoteESP8266 v2.9.0 の IRac.h / IRsend.h / IRac.cpp で API を確認した。
#include "ir_sender_esp32.h"

#include <Arduino.h>

#include "pins.h"

namespace irhub {
namespace {

constexpr decode_type_t kAcProtocol = decode_type_t::HITACHI_AC296;  // PROTO（決定）。ここ1か所だけ
constexpr int16_t kAcModel = -1;  // モデル指定なし（HITACHI_AC296 は model を使わない）

// core の列挙と stdAc の数値は違う（stdAc は kOff=-1）ので switch で1つずつ対応させる
stdAc::opmode_t toStdMode(AcMode m) {
  switch (m) {
    case AcMode::Auto: return stdAc::opmode_t::kAuto;  // → kHitachiAc296Auto(7)
    case AcMode::Cool: return stdAc::opmode_t::kCool;  // → kHitachiAc296Cool(3)
    case AcMode::Dry: return stdAc::opmode_t::kDry;    // → kHitachiAc296Dehumidify(5)
    case AcMode::Heat: return stdAc::opmode_t::kHeat;  // → kHitachiAc296Heat(6)
  }
  return stdAc::opmode_t::kCool;  // 来ない
}

stdAc::fanspeed_t toStdFan(AcFan f) {
  switch (f) {
    case AcFan::Auto: return stdAc::fanspeed_t::kAuto;      // → FanAuto(5)
    case AcFan::Min: return stdAc::fanspeed_t::kMin;        // → FanSilent(1)＝静音
    case AcFan::Low: return stdAc::fanspeed_t::kLow;        // → FanLow(2)
    case AcFan::Medium: return stdAc::fanspeed_t::kMedium;  // → FanMedium(3)
    case AcFan::High: return stdAc::fanspeed_t::kHigh;      // → FanHigh(4)
    case AcFan::Max: return stdAc::fanspeed_t::kMax;        // 選択肢外で来ない（来ても FanHigh）
  }
  return stdAc::fanspeed_t::kAuto;
}

stdAc::state_t toStdAc(const AcState& s) {
  stdAc::state_t r;  // quiet/turbo/econo/light/filter/clean/beep=false、sleep/clock=-1 のまま
  r.protocol = kAcProtocol;
  r.model = kAcModel;
  r.power = s.power;
  r.mode = toStdMode(s.mode);
  // hasTemp=false（自動）でも tempC を入れる。自動では IRHitachiAc296::setTemp が温度欄を置き換える
  r.degrees = static_cast<float>(s.tempC);
  r.celsius = true;
  r.fanspeed = toStdFan(s.fan);
  r.swingv = stdAc::swingv_t::kOff;  // 風向は作らない（D-01 7a）。IRac::hitachi296 は風向を使わない
  r.swingh = stdAc::swingh_t::kOff;
  return r;
}

}  // namespace

// IRac のコンストラクタは値を覚えるだけ。ピンにも赤外線にも触らない（N-BOOT）
IrSenderEsp32::IrSenderEsp32() : ac_(pins::kIrSend) {}

void IrSenderEsp32::begin() {
  // IO4 を出力・LOW に固定する。送信はしない（N-BOOT、HW-PINS）
  pinMode(pins::kIrSend, OUTPUT);
  digitalWrite(pins::kIrSend, LOW);
}

bool IrSenderEsp32::sendAc(const AcState& state) {
  const uint32_t t0 = millis();
  const bool ok = ac_.sendAc(toStdAc(state));  // prev は渡さない（HITACHI_AC296 は切り替え扱いの対象外）
  // 送信時間の確認用（D-06 8節：loop() が止まる時間の記録）
  Serial.printf("[ir] sendAc power=%d mode=%s temp=%d fan=%s ok=%d (%lums)\n", static_cast<int>(state.power),
                toString(state.mode), static_cast<int>(state.tempC), toString(state.fan), static_cast<int>(ok),
                static_cast<unsigned long>(millis() - t0));
  return ok;
}

}  // namespace irhub
