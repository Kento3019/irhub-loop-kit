// スケジュール ⇔ JSON（一覧・エクスポート／インポート）。根拠：docs/design/03-schedule.md 6節
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

#include "schedule.h"
#include "time_types.h"

namespace irhub {

constexpr int kScheduleExportVersion = 1;       // エクスポートファイルの版
constexpr size_t kScheduleJsonMaxBytes = 8192;  // 本文の上限（推測。10件の実寸は 2KB 程度）

enum class ScheduleJsonKind : uint8_t {
  List,    // GET/PUT /api/schedules
  Export,  // GET /api/schedules/export、POST /api/schedules/import
};

// 一覧 → JSON 文字列。kind==Export のときだけ now を使う（exportedAt）
std::string schedulesToJson(const ScheduleList& list, ScheduleJsonKind kind, const ClockReading& now);

struct ScheduleParseResult {
  Schedule items[kScheduleMax];
  int count = 0;
  std::string error;  // 失敗時の理由。成功時は空
};
// JSON 文字列 → Schedule の配列。形・型・文字列・件数・1件ごとの validateSchedule まで確かめる。
// id の重複は確かめない（Hub::replaceSchedules → replaceAll が DuplicateId を返す）。
bool schedulesFromJson(std::string_view body, ScheduleJsonKind kind, ScheduleParseResult* out);

// エクスポートのファイル名
std::string exportFilename(const ClockReading& now);

// LocalTime → "YYYY-MM-DDTHH:MM:SS+09:00"（ゼロ埋め）。synced の確かめは呼ぶ側で行う
std::string formatJstIso(const LocalTime& t);

}  // namespace irhub
