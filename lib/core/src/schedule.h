// スケジュール（データ構造・検証・一覧の置き換え・実行判定の純関数）
// 根拠：docs/design/03-schedule.md 1・3・4節。一覧は RAM の固定配列だけに持つ（N-STATE）
#pragma once
#include <cstdint>
#include <optional>

#include "ac_state.h"
#include "time_types.h"

namespace irhub {

constexpr int kScheduleMax = 10;           // 仮(F4-LIMIT) 要件「仮に10件」
constexpr uint16_t kScheduleIdMax = 9999;  // ID は 1..9999。0 は「未採番」

// 曜日のビット。bit n = LocalTime::wday n（0=日 … 6=土）
constexpr uint8_t kDayBit(int wday) { return static_cast<uint8_t>(1u << wday); }
constexpr uint8_t kAllDays = 0x7F;

// 対象。要件 F4 の対象はエアコンだけ。JSON の "target":"ac" に対応
enum class ScheduleTarget : uint8_t { Ac };

struct Schedule {
  uint16_t id = 0;  // 1..kScheduleIdMax。0 は「入力に id が無かった」（replaceAll が採番）
  bool enabled = true;
  int8_t hour = 0;    // 0..23（JST）
  int8_t minute = 0;  // 0..59
  uint8_t days = 0;   // 曜日ビットの OR。0 は不可
  ScheduleTarget target = ScheduleTarget::Ac;
  AcPatch ac;  // swingV/swingH は常に空
};

enum class ScheduleError : uint8_t {
  None,
  TooMany,
  IdOutOfRange,
  DuplicateId,
  TimeOutOfRange,
  NoDays,
  AcPowerMissing,
  AcStopHasOtherFields,
  AcTempWithoutMode,
  AcTempNotSupported,
  AcTempOutOfRange,
  AcFanNotSupported,
  AcSwingVNotSupported,
  AcSwingHNotSupported,
};
const char* errorMessage(ScheduleError e);  // None は ""

// 1件の検証（id の重複と件数は見ない。それは replaceAll）
ScheduleError validateSchedule(const Schedule& s);

class ScheduleList {
 public:
  int size() const { return count_; }
  const Schedule& at(int i) const { return items_[i]; }  // 0 <= i < size()。並びは入力の順
  const Schedule* findById(uint16_t id) const;           // 無ければ nullptr

  // 一覧をまとめて置き換える。1件でも駄目なら何も変えずにエラーを返す。
  // *badIndex にはエラーの件の添字（件数のエラーは -1）。成功時は id=0 の件に採番する。
  ScheduleError replaceAll(const Schedule* items, int n, int* badIndex);

 private:
  uint16_t allocateId();
  Schedule items_[kScheduleMax];
  int count_ = 0;
  uint16_t nextId_ = 1;
};

constexpr uint32_t kScheduleCheckIntervalMs = 1000;  // 判定の間隔（推測：1秒）
constexpr int64_t kScheduleCatchUpMinutes = 5;       // 遅れて判定したとき遡る最大の分数（推測）

// UNIX 時刻の分番号 → JST の曜日・時・分
JstMinute jstFromEpochMinute(int64_t epochMin);

// s がその分に実行すべきか：enabled、かつ days に t.wday のビット、かつ hour/minute が一致
bool scheduleMatches(const Schedule& s, const JstMinute& t);

// 前回判定した分と今の分から、今回判定する分の範囲 [from, to] を決める
struct MinuteWindow {
  int64_t from;  // from > to なら判定しない
  int64_t to;
  int64_t newLast;  // 判定後に Hub が lastScheduleMin_ に入れる値
};
MinuteWindow nextWindow(std::optional<int64_t> lastMin, int64_t curMin);

struct DueEntry {
  uint16_t id;
  int64_t epochMin;
};
// [fromMin, toMin] の各分について一覧の順に scheduleMatches を調べ、当たったものを out に入れる。
// 並び：分の昇順、同じ分の中は一覧の順。戻り値は入れた件数（cap を超えた分は捨てる）
int dueSchedules(const ScheduleList& list, int64_t fromMin, int64_t toMin, DueEntry* out, int cap);

}  // namespace irhub
