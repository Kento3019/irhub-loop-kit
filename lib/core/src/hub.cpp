// Hub の実装。根拠：docs/design/01-architecture.md 5節、docs/design/02-ac-state.md 6節、
// docs/design/03-schedule.md 5節
#include "hub.h"

namespace irhub {

Hub::Hub(IIrSender& ir, IClock& clock, IClimateSensor& sensor)
    : ir_(ir), clock_(clock), sensor_(sensor) {}  // 送信しない（N-BOOT）

void Hub::tick(uint32_t nowMs) {
  if (!climateEverRead_ || static_cast<uint32_t>(nowMs - lastClimateMs_) >= kClimateIntervalMs) {
    climateEverRead_ = true;
    lastClimateMs_ = nowMs;
    climate_ = sensor_.read();
  }
  if (!scheduleCheckedOnce_ ||
      static_cast<uint32_t>(nowMs - lastScheduleCheckMs_) >= kScheduleCheckIntervalMs) {
    scheduleCheckedOnce_ = true;
    lastScheduleCheckMs_ = nowMs;
    const ClockReading now = clock_.now();  // tick の中で1回だけ
    if (now.synced) runSchedules(now);      // N-TIME：取れていなければ何もしない
  }
}

bool Hub::applyAc(const AcPatch& patch, std::string* error) {
  const bool wasOn = ac_.state().power;  // 更新「前」の運転状態
  const AcError e = ac_.apply(patch);
  if (e != AcError::None) {
    if (error) *error = errorMessage(e);
    return false;
  }
  if (patch.power.has_value() || wasOn) {
    ir_.sendAc(ac_.state());  // 状態一式をちょうど1回。戻り値は見ない
  }
  return true;
}

ScheduleError Hub::replaceSchedules(const Schedule* items, int n, int* badIndex) {
  return schedules_.replaceAll(items, n, badIndex);
}

void Hub::runSchedules(const ClockReading& now) {
  const int64_t cur = now.epochSec / 60;
  const MinuteWindow w = nextWindow(lastScheduleMin_, cur);
  lastScheduleMin_ = w.newLast;  // 先に進めてから送る
  if (w.from > w.to) return;
  DueEntry due[kScheduleMax];
  const int n = dueSchedules(schedules_, w.from, w.to, due, kScheduleMax);
  for (int i = 0; i < n; ++i) {
    if (const Schedule* s = schedules_.findById(due[i].id)) runOne(*s);
  }
}

void Hub::runOne(const Schedule& s) {
  // target は Ac だけ。画面からの操作と同じ検証・更新・送信の流れ
  applyAc(s.ac, nullptr);
}

}  // namespace irhub
