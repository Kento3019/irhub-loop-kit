// スケジュールの実装。根拠：docs/design/03-schedule.md 1・3・4節
#include "schedule.h"

namespace irhub {
namespace {

constexpr int64_t kMinutesPerDay = 24 * 60;
constexpr int64_t kEpochWday = 4;  // 1970-01-01 は木曜

uint16_t idAfter(uint16_t id) { return id >= kScheduleIdMax ? 1 : static_cast<uint16_t>(id + 1); }

}  // namespace

const char* errorMessage(ScheduleError e) {
  switch (e) {
    case ScheduleError::None: return "";
    case ScheduleError::TooMany: return "too many schedules";
    case ScheduleError::IdOutOfRange: return "id out of range";
    case ScheduleError::DuplicateId: return "duplicate id";
    case ScheduleError::TimeOutOfRange: return "time out of range";
    case ScheduleError::NoDays: return "no days";
    case ScheduleError::AcPowerMissing: return "power required";
    case ScheduleError::AcStopHasOtherFields: return "ac stop takes only power";
    case ScheduleError::AcTempWithoutMode: return "temp requires mode";
    case ScheduleError::AcTempNotSupported: return "temp not supported in this mode";
    case ScheduleError::AcTempOutOfRange: return "temp out of range";
    case ScheduleError::AcFanNotSupported: return "fan not supported";
    case ScheduleError::AcSwingVNotSupported: return "swingV not supported";
    case ScheduleError::AcSwingHNotSupported: return "swingH not supported";
  }
  return "";
}

// 1.1 の規則（上から順に見て最初に当たったもの）
ScheduleError validateSchedule(const Schedule& s) {
  if (s.id > kScheduleIdMax) return ScheduleError::IdOutOfRange;
  if (s.hour < 0 || s.hour > 23 || s.minute < 0 || s.minute > 59) {
    return ScheduleError::TimeOutOfRange;
  }
  if (s.days == 0 || (s.days & static_cast<uint8_t>(~kAllDays)) != 0) return ScheduleError::NoDays;
  const AcPatch& p = s.ac;
  if (!p.power) return ScheduleError::AcPowerMissing;
  if (!*p.power && (p.mode || p.tempC || p.fan || p.swingV || p.swingH)) {
    return ScheduleError::AcStopHasOtherFields;
  }
  if (p.tempC) {
    if (!p.mode) return ScheduleError::AcTempWithoutMode;
    if (!cap::tempSupported(*p.mode)) return ScheduleError::AcTempNotSupported;
    if (!cap::tempInRange(*p.tempC)) return ScheduleError::AcTempOutOfRange;
  }
  if (p.fan && !cap::fanSupported(*p.fan)) return ScheduleError::AcFanNotSupported;
  if (p.swingV) return ScheduleError::AcSwingVNotSupported;
  if (p.swingH) return ScheduleError::AcSwingHNotSupported;
  return ScheduleError::None;
}

// ---- ScheduleList ---------------------------------------------------------------------

const Schedule* ScheduleList::findById(uint16_t id) const {
  for (int i = 0; i < count_; ++i) {
    if (items_[i].id == id) return &items_[i];
  }
  return nullptr;
}

ScheduleError ScheduleList::replaceAll(const Schedule* items, int n, int* badIndex) {
  int dummy = 0;
  int* bad = badIndex ? badIndex : &dummy;
  // 1. 件数
  if (n < 0 || n > kScheduleMax) {
    *bad = -1;
    return ScheduleError::TooMany;
  }
  // 2. 1件ずつの検証
  for (int i = 0; i < n; ++i) {
    const ScheduleError e = validateSchedule(items[i]);
    if (e != ScheduleError::None) {
      *bad = i;
      return e;
    }
  }
  // 3. id の重複（2件目の添字を返す）
  for (int i = 1; i < n; ++i) {
    if (items[i].id == 0) continue;
    for (int j = 0; j < i; ++j) {
      if (items[j].id == items[i].id) {
        *bad = i;
        return ScheduleError::DuplicateId;
      }
    }
  }
  // 4. 写す
  for (int i = 0; i < n; ++i) items_[i] = items[i];
  count_ = n;
  // 5. nextId_ を入力 id の後ろへ進める
  uint16_t maxId = 0;
  for (int i = 0; i < n; ++i) {
    if (items_[i].id > maxId) maxId = items_[i].id;
  }
  if (maxId != 0 && maxId >= nextId_) nextId_ = idAfter(maxId);
  // 6. id の無い件に入力の順に採番
  for (int i = 0; i < n; ++i) {
    if (items_[i].id == 0) items_[i].id = allocateId();
  }
  *bad = -1;
  return ScheduleError::None;
}

uint16_t ScheduleList::allocateId() {
  uint16_t id = nextId_;
  // 件数は最大 kScheduleMax なので必ず見つかる
  while (findById(id) != nullptr) id = idAfter(id);
  nextId_ = idAfter(id);
  return id;
}

// ---- 実行判定 ---------------------------------------------------------------------------

JstMinute jstFromEpochMinute(int64_t epochMin) {
  const int64_t m = epochMin + kJstOffsetMin;
  const int64_t dayNo = m / kMinutesPerDay;
  const int64_t minOfDay = m % kMinutesPerDay;
  JstMinute t;
  t.wday = static_cast<int8_t>((dayNo + kEpochWday) % 7);
  t.hour = static_cast<int8_t>(minOfDay / 60);
  t.minute = static_cast<int8_t>(minOfDay % 60);
  return t;
}

bool scheduleMatches(const Schedule& s, const JstMinute& t) {
  if (!s.enabled) return false;
  if (t.wday < 0 || t.wday > 6) return false;
  if ((s.days & kDayBit(t.wday)) == 0) return false;
  return s.hour == t.hour && s.minute == t.minute;
}

MinuteWindow nextWindow(std::optional<int64_t> lastMin, int64_t curMin) {
  MinuteWindow w{curMin + 1, curMin, curMin};  // 既定は「判定しない」
  if (!lastMin) return w;                      // NTP 取得後はじめての判定：遡らない
  const int64_t d = curMin - *lastMin;
  if (d < 0) {
    w.newLast = *lastMin;  // 時計が戻った：据え置き
    return w;
  }
  if (d == 0) return w;  // 同じ分の2回目以降（二重実行の防止）
  if (d <= kScheduleCatchUpMinutes) {
    w.from = *lastMin + 1;  // d==1 なら curMin だけ
  } else {
    w.from = curMin;  // 大きく進んだ：今の分だけ
  }
  w.to = curMin;
  return w;
}

int dueSchedules(const ScheduleList& list, int64_t fromMin, int64_t toMin, DueEntry* out, int cap) {
  int n = 0;
  for (int64_t m = fromMin; m <= toMin; ++m) {
    const JstMinute t = jstFromEpochMinute(m);
    for (int i = 0; i < list.size(); ++i) {
      const Schedule& s = list.at(i);
      if (!scheduleMatches(s, t)) continue;
      if (n >= cap) return n;
      out[n].id = s.id;
      out[n].epochMin = m;
      ++n;
    }
  }
  return n;
}

}  // namespace irhub
