// 日本時間（JST）の日時の値型。根拠：docs/design/01-architecture.md 4節、docs/design/03-schedule.md 2節
// core の他モジュールを include しない
#pragma once
#include <cstdint>

namespace irhub {

// 日本時間（JST）の日時。曜日は 0=日曜 … 6=土曜（struct tm の tm_wday と同じ）
struct LocalTime {
  int16_t year;    // 例 2026
  int8_t month;    // 1..12
  int8_t day;      // 1..31
  int8_t hour;     // 0..23
  int8_t minute;   // 0..59
  int8_t second;   // 0..59
  int8_t wday;     // 0..6
};

struct ClockReading {
  bool synced;       // NTP で一度でも時刻が取れていれば true。false の間 local は無意味
  LocalTime local;   // JST
  int64_t epochSec;  // UNIX 時刻（秒）。synced=false のとき 0
};

constexpr int64_t kJstOffsetMin = 9 * 60;  // JST = UTC+9。日本に夏時間は無いので固定

// ある1分の JST での曜日と時分
struct JstMinute {
  int8_t wday;    // 0=日 … 6=土
  int8_t hour;    // 0..23
  int8_t minute;  // 0..59
};

}  // namespace irhub
