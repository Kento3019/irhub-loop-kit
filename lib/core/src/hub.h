// アプリの中心。全状態を RAM に持ち、ポートを使って送信・読み取り・スケジュール実行を行う
// 根拠：docs/design/01-architecture.md 5節、docs/design/02-ac-state.md 6節、docs/design/03-schedule.md 5節
#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "ac_state.h"
#include "ports.h"
#include "schedule.h"

namespace irhub {

// センサーの読み取り周期。仮(F5)：要件「仮に30秒」
constexpr uint32_t kClimateIntervalMs = 30000;

class Hub {
 public:
  // コンストラクタは赤外線を送らない（N-BOOT）。状態は初期値（N-STATE、F2 の表）。
  Hub(IIrSender& ir, IClock& clock, IClimateSensor& sensor);

  // loop() から毎回呼ぶ。nowMs は millis() の値（差は uint32_t の引き算で取る）。
  //  - センサー：一度も読んでいない、または前回から kClimateIntervalMs 以上経っていれば読む
  //  - スケジュール：kScheduleCheckIntervalMs ごとに clock.now() を1回取り、synced のときだけ判定・実行
  void tick(uint32_t nowMs);

  // 検証に通れば状態を更新して true。送信は patch が power を含むか、更新前が運転中のときだけ（D-02 6節）
  bool applyAc(const AcPatch& patch, std::string* error);

  // PUT /api/schedules と POST /api/schedules/import はこれを使う。送信も判定記録の更新もしない
  ScheduleError replaceSchedules(const Schedule* items, int n, int* badIndex);

  const AcState& acState() const { return ac_.state(); }
  ScheduleList& schedules() { return schedules_; }
  const ClimateReading& lastClimate() const { return climate_; }  // まだ読めていなければ valid=false
  ClockReading clockNow() { return clock_.now(); }

 private:
  void runSchedules(const ClockReading& now);
  void runOne(const Schedule& s);

  IIrSender& ir_;
  IClock& clock_;
  IClimateSensor& sensor_;
  AcModel ac_;
  ScheduleList schedules_;
  ClimateReading climate_{false, 0.0f, 0.0f};
  bool climateEverRead_ = false;
  uint32_t lastClimateMs_ = 0;
  bool scheduleCheckedOnce_ = false;
  uint32_t lastScheduleCheckMs_ = 0;
  std::optional<int64_t> lastScheduleMin_;  // 判定済みの最後の分（epochMin）
};

}  // namespace irhub
