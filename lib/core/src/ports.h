// 外界との境界になる抽象クラス（純粋仮想）。根拠：docs/design/01-architecture.md 4節
#pragma once
#include "ac_state.h"
#include "time_types.h"

namespace irhub {

// 赤外線送信。エアコンの状態一式を1回で送る（F1）。
// 戻り値：送信処理を呼べたら true（相手に届いたかは分からない）。
class IIrSender {
 public:
  virtual ~IIrSender() = default;
  virtual bool sendAc(const AcState& state) = 0;
};

// 時計。src では NTP＋JST、テストでは手で進めるフェイク。
class IClock {
 public:
  virtual ~IClock() = default;
  virtual ClockReading now() = 0;
};

struct ClimateReading {
  bool valid;          // 読み取り成功で true
  float temperatureC;  // ℃
  float humidityPct;   // %RH
};

// 温湿度センサー。呼ばれたときに1回読む。周期の判断は Hub が行う。
class IClimateSensor {
 public:
  virtual ~IClimateSensor() = default;
  virtual ClimateReading read() = 0;
};

}  // namespace irhub
