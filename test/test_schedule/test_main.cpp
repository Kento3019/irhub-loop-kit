// test_schedule：スケジュール（純関数・ScheduleList・schedule_json・Hub のスケジュール実行）の単体テスト
// 根拠：docs/design/03-schedule.md（D-03）、docs/design/01-architecture.md（D-01 4・5・9節）、
//       docs/design/02-ac-state.md（D-02）、docs/test/test-plan.md「B. test/test_schedule」
//       （TC-N35〜TC-N98、TC-N100〜TC-N129、TC-N182、TC-N183、TC-N197、TC-N198、TC-N205〜TC-N216）
// F1-VALUES の値（温度範囲・温度を指定できるモード・風量の選択肢）と仮値（F4-LIMIT の件数、F2 の初期値）は
// 直書きせず、cap::・kScheduleMax・initialSettings() から作る（テスト計画 方針2）。
// フェイク（赤外線送信・時計・センサー）は D-01 4節のインターフェースに対してこのファイルの中に書く。
#include <unity.h>

#include <ArduinoJson.h>

#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "ac_capabilities.h"
#include "ac_state.h"
#include "hub.h"
#include "ports.h"
#include "schedule.h"
#include "schedule_json.h"
#include "time_types.h"

using namespace irhub;

// ---- フェイク（D-01 9節の形＋テスト計画 方針3 の ★） ---------------------------------------

struct FakeIrSender : IIrSender {
  int acCount = 0;
  AcState lastAc{};
  bool result = true;
  bool sendAc(const AcState& s) override {
    ++acCount;
    lastAc = s;
    return result;
  }
};

struct FakeClock : IClock {
  ClockReading r{false, {}, 0};
  int nowCount = 0;
  std::vector<ClockReading> seq;
  size_t seqIdx = 0;
  ClockReading now() override {
    ++nowCount;
    if (!seq.empty()) {
      const size_t i = seqIdx < seq.size() ? seqIdx : seq.size() - 1;
      if (seqIdx < seq.size()) ++seqIdx;
      return seq[i];
    }
    return r;
  }
};

struct FakeClimateSensor : IClimateSensor {
  int readCount = 0;
  ClimateReading next{true, 25.0f, 50.0f};
  ClimateReading read() override {
    ++readCount;
    return next;
  }
};

// ---- 補助 -----------------------------------------------------------------------------------

#define ASSERT_ENUM(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))
#define ASSERT_ENUM_MSG(expected, actual, msg) \
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), msg)

#define REQUIRE_TEMP_MODE(m)                                         \
  do {                                                               \
    if (!cap::tempSupported(m)) {                                    \
      TEST_IGNORE_MESSAGE("mode does not support temp (F1-VALUES)"); \
    }                                                                \
  } while (0)

static const AcMode kAllModes[kAcModeCount] = {AcMode::Auto, AcMode::Cool, AcMode::Dry,
                                               AcMode::Heat};
static const int kAcFanCount = 6;  // AcFan { Auto, Min, Low, Medium, High, Max }（D-02 1節）

static bool findF1(AcFan* out) {
  for (int i = 0; i < cap::kFanChoiceCount; ++i) {
    if (cap::kFanChoices[i] != AcFan::Auto) { *out = cap::kFanChoices[i]; return true; }
  }
  return false;
}
static bool findFx(AcFan* out) {
  for (int i = 0; i < kAcFanCount; ++i) {
    const AcFan f = static_cast<AcFan>(i);
    if (!cap::fanSupported(f)) { *out = f; return true; }
  }
  return false;
}
static bool findMt(AcMode* out) {
  for (int i = 0; i < kAcModeCount; ++i) {
    if (!cap::tempSupported(kAllModes[i])) { *out = kAllModes[i]; return true; }
  }
  return false;
}

// 曜日ビット（日=0x01 … 土=0x40）
static const uint8_t kSun = 0x01;
static const uint8_t kFri = 0x20;
static const uint8_t kSat = 0x40;
static const uint8_t kWeekdays = 0x3E;
static const uint8_t kEveryDay = 0x7F;

// 基準の時刻（テスト計画 方針4）
static const int64_t T0 = 1790287200;   // 2026-09-25（金）07:00:00 JST
static const int64_t M0 = 29838120;     // T0 / 60
static const int64_t T20 = 1790334000;  // 2026-09-25（金）20:00:00 JST
static const int64_t kDayStart = T0 - 7 * 3600;  // 2026-09-25 00:00:00 JST

// 2026-09-25（金）の中の epochSec から同期済みの ClockReading を作る
static ClockReading readingAt(int64_t sec) {
  const int64_t off = sec - kDayStart;
  ClockReading c;
  c.synced = true;
  c.local = LocalTime{2026, 9, 25, static_cast<int8_t>(off / 3600),
                      static_cast<int8_t>((off / 60) % 60), static_cast<int8_t>(off % 60), 5};
  c.epochSec = sec;
  return c;
}
static ClockReading unsyncedReading() { return ClockReading{false, {}, 0}; }

static AcPatch runP(AcMode m, int t) { AcPatch p; p.power = true; p.mode = m; p.tempC = t; return p; }
static AcPatch runModeP(AcMode m) { AcPatch p; p.power = true; p.mode = m; return p; }
static AcPatch stopP() { AcPatch p; p.power = false; return p; }
static AcPatch PA() { return runP(AcMode::Cool, 25); }
static AcPatch PB() { return runP(AcMode::Heat, 21); }

static Schedule S(uint16_t id, int h, int m, uint8_t days, const AcPatch& ac, bool enabled = true) {
  Schedule s;
  s.id = id;
  s.enabled = enabled;
  s.hour = static_cast<int8_t>(h);
  s.minute = static_cast<int8_t>(m);
  s.days = days;
  s.target = ScheduleTarget::Ac;
  s.ac = ac;
  return s;
}

// B-2 の基準の件 B
static Schedule baseB() { return S(0, 7, 0, kWeekdays, runP(AcMode::Heat, 20)); }

static JstMinute jm(int w, int h, int m) {
  JstMinute t;
  t.wday = static_cast<int8_t>(w);
  t.hour = static_cast<int8_t>(h);
  t.minute = static_cast<int8_t>(m);
  return t;
}

static void assertJst(int w, int h, int m, const JstMinute& a, const char* msg) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(w, a.wday, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(h, a.hour, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(m, a.minute, msg);
}

static ScheduleError replaceList(ScheduleList& l, std::initializer_list<Schedule> items,
                                 int* bad = nullptr) {
  Schedule buf[16];
  int n = 0;
  for (const Schedule& s : items) buf[n++] = s;
  int b = -99;
  const ScheduleError e = l.replaceAll(buf, n, &b);
  if (bad) *bad = b;
  return e;
}

// id の並び（0＝無し）で B の件を作って replaceAll
static ScheduleError replaceIds(ScheduleList& l, std::initializer_list<int> ids, int* bad = nullptr) {
  Schedule buf[16];
  int n = 0;
  for (int id : ids) {
    buf[n] = baseB();
    buf[n].id = static_cast<uint16_t>(id);
    ++n;
  }
  int b = -99;
  const ScheduleError e = l.replaceAll(buf, n, &b);
  if (bad) *bad = b;
  return e;
}

static void assertIds(const ScheduleList& l, std::initializer_list<int> ids, const char* msg) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(ids.size()), l.size(), msg);
  int i = 0;
  for (int id : ids) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(id, l.at(i).id, msg);
    ++i;
  }
}

static void assertAcStateEq(const AcState& e, const AcState& a, const char* msg) {
  TEST_ASSERT_EQUAL_MESSAGE(e.power, a.power, msg);
  ASSERT_ENUM_MSG(e.mode, a.mode, msg);
  TEST_ASSERT_EQUAL_MESSAGE(e.hasTemp, a.hasTemp, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(e.tempC, a.tempC, msg);
  ASSERT_ENUM_MSG(e.fan, a.fan, msg);
  ASSERT_ENUM_MSG(e.swingV, a.swingV, msg);
  ASSERT_ENUM_MSG(e.swingH, a.swingH, msg);
}

static void assertPatchEq(const AcPatch& e, const AcPatch& a, const char* msg) {
  TEST_ASSERT_EQUAL_MESSAGE(e.power.has_value(), a.power.has_value(), msg);
  if (e.power && a.power) TEST_ASSERT_EQUAL_MESSAGE(*e.power, *a.power, msg);
  TEST_ASSERT_EQUAL_MESSAGE(e.mode.has_value(), a.mode.has_value(), msg);
  if (e.mode && a.mode) ASSERT_ENUM_MSG(*e.mode, *a.mode, msg);
  TEST_ASSERT_EQUAL_MESSAGE(e.tempC.has_value(), a.tempC.has_value(), msg);
  if (e.tempC && a.tempC) TEST_ASSERT_EQUAL_INT_MESSAGE(*e.tempC, *a.tempC, msg);
  TEST_ASSERT_EQUAL_MESSAGE(e.fan.has_value(), a.fan.has_value(), msg);
  if (e.fan && a.fan) ASSERT_ENUM_MSG(*e.fan, *a.fan, msg);
  TEST_ASSERT_FALSE_MESSAGE(a.swingV.has_value(), msg);
  TEST_ASSERT_FALSE_MESSAGE(a.swingH.has_value(), msg);
}

static void assertScheduleEq(const Schedule& e, const Schedule& a, const char* msg) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(e.id, a.id, msg);
  TEST_ASSERT_EQUAL_MESSAGE(e.enabled, a.enabled, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(e.hour, a.hour, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(e.minute, a.minute, msg);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(e.days, a.days, msg);
  ASSERT_ENUM_MSG(e.target, a.target, msg);
  assertPatchEq(e.ac, a.ac, msg);
}

// Hub を使うテストの道具。tickAt(sec) はその時刻を FakeClock に入れて tick し、nowMs を 1000 進める
struct Rig {
  FakeIrSender ir;
  FakeClock clock;
  FakeClimateSensor sensor;
  Hub hub{ir, clock, sensor};
  uint32_t ms = 0;
  void tickAt(int64_t sec) {
    clock.r = readingAt(sec);
    hub.tick(ms);
    ms += 1000;
  }
  void tickUnsynced() {
    clock.r = unsyncedReading();
    hub.tick(ms);
    ms += 1000;
  }
  ScheduleError replace(std::initializer_list<Schedule> items, int* bad = nullptr) {
    Schedule buf[16];
    int n = 0;
    for (const Schedule& s : items) buf[n++] = s;
    int b = -99;
    const ScheduleError e = hub.replaceSchedules(buf, n, &b);
    if (bad) *bad = b;
    return e;
  }
};

// ---- JSON の道具 ----------------------------------------------------------------------------

// 1件の JSON。各欄は JSON の断片。空文字列はそのキーを書かない。extra は末尾に足す生の断片
struct J {
  std::string id = "1";
  std::string enabled = "true";
  std::string time = R"("07:00")";
  std::string days = R"(["mon","tue","wed","thu","fri"])";
  std::string target = R"("ac")";
  std::string action = R"({"power":true,"mode":"heat","temp":20})";
  std::string extra;
  std::string str() const {
    std::string s = "{";
    bool first = true;
    auto add = [&](const char* k, const std::string& v) {
      if (v.empty()) return;
      if (!first) s += ",";
      first = false;
      s += "\"";
      s += k;
      s += "\":";
      s += v;
    };
    add("id", id);
    add("enabled", enabled);
    add("time", time);
    add("days", days);
    add("target", target);
    add("action", action);
    if (!extra.empty()) {
      if (!first) s += ",";
      s += extra;
    }
    s += "}";
    return s;
  }
};

static std::string joinItems(const std::vector<std::string>& items) {
  std::string s;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) s += ",";
    s += items[i];
  }
  return s;
}
static std::string exportBody(const std::vector<std::string>& items) {
  return R"({"version":1,"exportedAt":null,"schedules":[)" + joinItems(items) + "]}";
}
static std::string listBody(const std::vector<std::string>& items) {
  return R"({"schedules":[)" + joinItems(items) + "]}";
}
static std::string exportOne(const J& j) { return exportBody({j.str()}); }
static J withAction(const std::string& a) { J j; j.action = a; return j; }

static void expectExportError(const std::string& body, const char* expected, const char* label) {
  ScheduleParseResult r;
  TEST_ASSERT_FALSE_MESSAGE(schedulesFromJson(body, ScheduleJsonKind::Export, &r), label);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, r.error.c_str(), label);
}

static bool readDoc(const std::string& s, JsonDocument& doc) {
  const DeserializationError err = deserializeJson(doc, s.c_str(), s.size());
  return !err;
}
static bool hasKey(JsonObject o, const char* k) {
  for (JsonPair kv : o) {
    if (std::string(kv.key().c_str()) == k) return true;
  }
  return false;
}
static int countOf(const std::string& hay, const std::string& needle) {
  int n = 0;
  for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
  return n;
}

// 例の3件（D-03 6.1）。id 3 の風量は F1（確定値では min）
static Schedule ex1() { return S(1, 7, 0, kWeekdays, runP(AcMode::Heat, 20)); }
static Schedule ex2() { return S(2, 23, 30, kEveryDay, stopP()); }
static Schedule ex3(AcFan f1) {
  AcPatch p;
  p.power = true;
  p.mode = AcMode::Auto;
  p.fan = f1;
  return S(3, 6, 45, kSat | kSun, p, false);
}
static std::vector<std::string> exampleItemsJson(AcFan f1) {
  J a;
  J b;
  b.id = "2";
  b.time = R"("23:30")";
  b.days = R"(["sun","mon","tue","wed","thu","fri","sat"])";
  b.action = R"({"power":false})";
  J c;
  c.id = "3";
  c.enabled = "false";
  c.time = R"("06:45")";
  c.days = R"(["sat","sun"])";
  c.action = std::string(R"({"power":true,"mode":"auto","fan":")") + toString(f1) + "\"}";
  return {a.str(), b.str(), c.str()};
}

void setUp() {}
void tearDown() {}

// =============================================================================================
// B-1. 時刻の変換と判定（純関数）
// =============================================================================================

// TC: TC-N35
// REQ: F4, N-TIME
void test_jst_friday_2000() {
  assertJst(5, 20, 0, jstFromEpochMinute(29838900), "29838900");
}

// TC: TC-N36
// REQ: F4, N-TIME
void test_jst_friday_0700() {
  assertJst(5, 7, 0, jstFromEpochMinute(M0), "M0");
}

// TC: TC-N37
// REQ: F4, N-TIME
void test_jst_day_boundary_fri_to_sat() {
  assertJst(5, 23, 59, jstFromEpochMinute(29839139), "Fri 23:59");
  assertJst(6, 0, 0, jstFromEpochMinute(29839140), "Sat 00:00");
}

// TC: TC-N38
// REQ: F4
void test_jst_week_boundary_sat_to_sun() {
  assertJst(6, 23, 59, jstFromEpochMinute(29840579), "Sat 23:59");
  assertJst(0, 0, 0, jstFromEpochMinute(29840580), "Sun 00:00");
}

// TC: TC-N39
// REQ: F4
void test_matches_exact() {
  const Schedule s = S(1, 7, 0, kWeekdays, stopP());
  TEST_ASSERT_TRUE(scheduleMatches(s, jm(5, 7, 0)));
}

// TC: TC-N40
// REQ: F4
void test_matches_wrong_time() {
  const Schedule s = S(1, 7, 0, kWeekdays, stopP());
  TEST_ASSERT_FALSE_MESSAGE(scheduleMatches(s, jm(5, 7, 1)), "07:01");
  TEST_ASSERT_FALSE_MESSAGE(scheduleMatches(s, jm(5, 8, 0)), "08:00");
  TEST_ASSERT_FALSE_MESSAGE(scheduleMatches(s, jm(5, 6, 59)), "06:59");
}

// TC: TC-N41
// REQ: F4
void test_matches_wrong_day() {
  const Schedule s = S(1, 7, 0, kWeekdays, stopP());
  TEST_ASSERT_FALSE_MESSAGE(scheduleMatches(s, jm(6, 7, 0)), "Sat");
  TEST_ASSERT_FALSE_MESSAGE(scheduleMatches(s, jm(0, 7, 0)), "Sun");
}

// TC: TC-N42
// REQ: F4
void test_matches_disabled() {
  const Schedule s = S(1, 7, 0, kWeekdays, stopP(), false);
  TEST_ASSERT_FALSE(scheduleMatches(s, jm(5, 7, 0)));
}

// TC: TC-N43
// REQ: F4, N-TIME, N-BOOT
void test_window_first_after_sync() {
  const MinuteWindow w = nextWindow(std::nullopt, M0);
  TEST_ASSERT_TRUE(w.from > w.to);
  TEST_ASSERT_EQUAL_INT64(M0, w.newLast);
}

// TC: TC-N44
// REQ: F4
void test_window_same_minute() {
  const MinuteWindow w = nextWindow(M0, M0);
  TEST_ASSERT_TRUE(w.from > w.to);
  TEST_ASSERT_EQUAL_INT64(M0, w.newLast);
}

// TC: TC-N45
// REQ: F4
void test_window_next_minute() {
  const MinuteWindow w = nextWindow(M0 - 1, M0);
  TEST_ASSERT_EQUAL_INT64(M0, w.from);
  TEST_ASSERT_EQUAL_INT64(M0, w.to);
  TEST_ASSERT_EQUAL_INT64(M0, w.newLast);
}

// TC: TC-N46
// REQ: F4
void test_window_late_2() {
  const MinuteWindow w = nextWindow(M0 - 2, M0);
  TEST_ASSERT_EQUAL_INT64(M0 - 1, w.from);
  TEST_ASSERT_EQUAL_INT64(M0, w.to);
  TEST_ASSERT_EQUAL_INT64(M0, w.newLast);
}

// TC: TC-N47
// REQ: F4
void test_window_late_catchup_max() {
  const MinuteWindow w = nextWindow(M0 - kScheduleCatchUpMinutes, M0);
  TEST_ASSERT_EQUAL_INT64(M0 - kScheduleCatchUpMinutes + 1, w.from);
  TEST_ASSERT_EQUAL_INT64(M0, w.to);
  TEST_ASSERT_EQUAL_INT64(M0, w.newLast);
}

// TC: TC-N48
// REQ: F4
void test_window_jump_beyond_catchup() {
  const MinuteWindow w = nextWindow(M0 - kScheduleCatchUpMinutes - 1, M0);
  TEST_ASSERT_EQUAL_INT64(M0, w.from);
  TEST_ASSERT_EQUAL_INT64(M0, w.to);
  TEST_ASSERT_EQUAL_INT64(M0, w.newLast);
}

// TC: TC-N49
// REQ: F4
void test_window_clock_back_1() {
  const MinuteWindow w = nextWindow(M0 + 1, M0);
  TEST_ASSERT_TRUE(w.from > w.to);
  TEST_ASSERT_EQUAL_INT64(M0 + 1, w.newLast);
}

// TC: TC-N50
// REQ: F4
void test_window_clock_back_300() {
  const MinuteWindow w = nextWindow(M0 + 300, M0);
  TEST_ASSERT_TRUE(w.from > w.to);
  TEST_ASSERT_EQUAL_INT64(M0 + 300, w.newLast);
}

// TC: TC-N51
// REQ: F4
void test_due_same_minute_list_order() {
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None,
              replaceList(l, {S(5, 7, 0, kEveryDay, PA()), S(3, 7, 0, kEveryDay, PB())}));
  DueEntry out[kScheduleMax];
  TEST_ASSERT_EQUAL_INT(2, dueSchedules(l, M0, M0, out, kScheduleMax));
  TEST_ASSERT_EQUAL_INT(5, out[0].id);
  TEST_ASSERT_EQUAL_INT64(M0, out[0].epochMin);
  TEST_ASSERT_EQUAL_INT(3, out[1].id);
  TEST_ASSERT_EQUAL_INT64(M0, out[1].epochMin);
}

// TC: TC-N52
// REQ: F4
void test_due_minute_ascending() {
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None,
              replaceList(l, {S(1, 7, 2, kEveryDay, stopP()), S(2, 7, 0, kEveryDay, stopP())}));
  DueEntry out[kScheduleMax];
  TEST_ASSERT_EQUAL_INT(2, dueSchedules(l, M0, M0 + 2, out, 10));
  TEST_ASSERT_EQUAL_INT(2, out[0].id);
  TEST_ASSERT_EQUAL_INT64(M0, out[0].epochMin);
  TEST_ASSERT_EQUAL_INT(1, out[1].id);
  TEST_ASSERT_EQUAL_INT64(M0 + 2, out[1].epochMin);
}

// TC: TC-N53
// REQ: F4
void test_due_across_midnight_weekday_switch() {
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None,
              replaceList(l, {S(1, 0, 0, kSat, stopP()), S(2, 23, 59, kFri, stopP()),
                              S(3, 0, 0, kFri, stopP()), S(4, 23, 59, kSat, stopP())}));
  DueEntry out[kScheduleMax];
  TEST_ASSERT_EQUAL_INT(2, dueSchedules(l, 29839139, 29839141, out, 10));
  TEST_ASSERT_EQUAL_INT(2, out[0].id);
  TEST_ASSERT_EQUAL_INT64(29839139, out[0].epochMin);
  TEST_ASSERT_EQUAL_INT(1, out[1].id);
  TEST_ASSERT_EQUAL_INT64(29839140, out[1].epochMin);
}

// TC: TC-N54
// REQ: F4
void test_due_cap() {
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None,
              replaceList(l, {S(1, 7, 0, kEveryDay, stopP()), S(2, 7, 0, kEveryDay, stopP()),
                              S(3, 7, 0, kEveryDay, stopP())}));
  DueEntry out[kScheduleMax];
  TEST_ASSERT_EQUAL_INT(2, dueSchedules(l, M0, M0, out, 2));
  TEST_ASSERT_EQUAL_INT(1, out[0].id);
  TEST_ASSERT_EQUAL_INT(2, out[1].id);
}

// =============================================================================================
// B-2. 1件の検証（validateSchedule）
// =============================================================================================

// TC: TC-N55
// REQ: F4
void test_validate_base_and_stop() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(baseB()), "B");
  Schedule s = baseB();
  s.ac = stopP();
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "stop");
}

// TC: TC-N56
// REQ: F4
void test_validate_time_edges_ok() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Schedule s = baseB();
  s.hour = 0; s.minute = 0;
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "00:00");
  s.hour = 23; s.minute = 59;
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "23:59");
}

// TC: TC-N57
// REQ: F4
void test_validate_time_out_of_range() {
  Schedule s = baseB();
  s.hour = -1;
  ASSERT_ENUM_MSG(ScheduleError::TimeOutOfRange, validateSchedule(s), "hour -1");
  s = baseB(); s.hour = 24;
  ASSERT_ENUM_MSG(ScheduleError::TimeOutOfRange, validateSchedule(s), "hour 24");
  s = baseB(); s.minute = -1;
  ASSERT_ENUM_MSG(ScheduleError::TimeOutOfRange, validateSchedule(s), "minute -1");
  s = baseB(); s.minute = 60;
  ASSERT_ENUM_MSG(ScheduleError::TimeOutOfRange, validateSchedule(s), "minute 60");
}

// TC: TC-N58
// REQ: F4
void test_validate_id_range() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Schedule s = baseB();
  s.id = 0;
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "id 0");
  s.id = kScheduleIdMax;
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "id max");
  s.id = kScheduleIdMax + 1;
  ASSERT_ENUM_MSG(ScheduleError::IdOutOfRange, validateSchedule(s), "id max+1");
}

// TC: TC-N59
// REQ: F4
void test_validate_days() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Schedule s = baseB();
  s.days = 0;
  ASSERT_ENUM_MSG(ScheduleError::NoDays, validateSchedule(s), "0x00");
  s.days = 0x80;
  ASSERT_ENUM_MSG(ScheduleError::NoDays, validateSchedule(s), "0x80");
  s.days = 0xFF;
  ASSERT_ENUM_MSG(ScheduleError::NoDays, validateSchedule(s), "0xFF");
  s.days = 0x01;
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "0x01");
  s.days = 0x7F;
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "0x7F");
}

// TC: TC-N60
// REQ: F4
void test_validate_power_missing() {
  Schedule s = baseB();
  s.ac.power.reset();
  ASSERT_ENUM(ScheduleError::AcPowerMissing, validateSchedule(s));
}

// TC: TC-N61
// REQ: F4
void test_validate_stop_with_other_fields() {
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Schedule s = baseB();
  s.ac = stopP(); s.ac.mode = AcMode::Cool;
  ASSERT_ENUM_MSG(ScheduleError::AcStopHasOtherFields, validateSchedule(s), "stop+mode");
  s.ac = stopP(); s.ac.tempC = 20;
  ASSERT_ENUM_MSG(ScheduleError::AcStopHasOtherFields, validateSchedule(s), "stop+temp");
  s.ac = stopP(); s.ac.fan = f1;
  ASSERT_ENUM_MSG(ScheduleError::AcStopHasOtherFields, validateSchedule(s), "stop+fan");
  s.ac = stopP();
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "stop only");
}

// TC: TC-N62
// REQ: F4
void test_validate_temp_without_mode() {
  Schedule s = baseB();
  AcPatch p; p.power = true; p.tempC = 20;
  s.ac = p;
  ASSERT_ENUM(ScheduleError::AcTempWithoutMode, validateSchedule(s));
}

// TC: TC-N63
// REQ: F4
void test_validate_temp_not_supported_mode() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  Schedule s = baseB();
  s.ac = runP(mt, cap::kTempMinC);
  ASSERT_ENUM(ScheduleError::AcTempNotSupported, validateSchedule(s));
}

// TC: TC-N64
// REQ: F4
void test_validate_temp_edges() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Schedule s = baseB();
  s.ac = runP(AcMode::Heat, cap::kTempMinC - 1);
  ASSERT_ENUM_MSG(ScheduleError::AcTempOutOfRange, validateSchedule(s), "min-1");
  s.ac = runP(AcMode::Heat, cap::kTempMinC);
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "min");
  s.ac = runP(AcMode::Heat, cap::kTempMaxC);
  ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "max");
  s.ac = runP(AcMode::Heat, cap::kTempMaxC + 1);
  ASSERT_ENUM_MSG(ScheduleError::AcTempOutOfRange, validateSchedule(s), "max+1");
}

// TC: TC-N65
// REQ: F4
void test_validate_fan_and_swing_not_supported() {
  Schedule s = baseB();
  AcPatch b; b.power = true; b.swingV = AcSwingV::Off;
  s.ac = b;
  ASSERT_ENUM_MSG(ScheduleError::AcSwingVNotSupported, validateSchedule(s), "(b) swingV=Off");
  AcPatch c; c.power = true; c.swingH = AcSwingH::Off;
  s.ac = c;
  ASSERT_ENUM_MSG(ScheduleError::AcSwingHNotSupported, validateSchedule(s), "(c) swingH=Off");
  AcFan fx;
  if (!findFx(&fx)) TEST_IGNORE_MESSAGE("(a) every AcFan is a choice (F1-VALUES)");
  AcPatch a; a.power = true; a.fan = fx;
  s.ac = a;
  ASSERT_ENUM_MSG(ScheduleError::AcFanNotSupported, validateSchedule(s), "(a) fan=Fx");
}

// TC: TC-N66
// REQ: F4
void test_validate_disabled_still_checked() {
  Schedule s = baseB();
  s.enabled = false;
  s.hour = 24;
  ASSERT_ENUM(ScheduleError::TimeOutOfRange, validateSchedule(s));
}

// TC: TC-N67
// REQ: F4
void test_validate_order_time_before_days() {
  Schedule s = baseB();
  s.hour = 24;
  s.days = 0;
  ASSERT_ENUM(ScheduleError::TimeOutOfRange, validateSchedule(s));
}

// TC: TC-N68
// REQ: F4
void test_schedule_error_messages() {
  TEST_ASSERT_EQUAL_STRING("", errorMessage(ScheduleError::None));
  TEST_ASSERT_EQUAL_STRING("too many schedules", errorMessage(ScheduleError::TooMany));
  TEST_ASSERT_EQUAL_STRING("id out of range", errorMessage(ScheduleError::IdOutOfRange));
  TEST_ASSERT_EQUAL_STRING("duplicate id", errorMessage(ScheduleError::DuplicateId));
  TEST_ASSERT_EQUAL_STRING("time out of range", errorMessage(ScheduleError::TimeOutOfRange));
  TEST_ASSERT_EQUAL_STRING("no days", errorMessage(ScheduleError::NoDays));
  TEST_ASSERT_EQUAL_STRING("power required", errorMessage(ScheduleError::AcPowerMissing));
  TEST_ASSERT_EQUAL_STRING("ac stop takes only power",
                           errorMessage(ScheduleError::AcStopHasOtherFields));
  TEST_ASSERT_EQUAL_STRING("temp requires mode", errorMessage(ScheduleError::AcTempWithoutMode));
  TEST_ASSERT_EQUAL_STRING("temp not supported in this mode",
                           errorMessage(ScheduleError::AcTempNotSupported));
  TEST_ASSERT_EQUAL_STRING("temp out of range", errorMessage(ScheduleError::AcTempOutOfRange));
  TEST_ASSERT_EQUAL_STRING("fan not supported", errorMessage(ScheduleError::AcFanNotSupported));
  TEST_ASSERT_EQUAL_STRING("swingV not supported",
                           errorMessage(ScheduleError::AcSwingVNotSupported));
  TEST_ASSERT_EQUAL_STRING("swingH not supported",
                           errorMessage(ScheduleError::AcSwingHNotSupported));
}

// TC: TC-N205
// REQ: F4
void test_validate_not_supported_before_out_of_range() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  Schedule s = baseB();
  s.ac = runP(mt, cap::kTempMaxC + 1);
  ASSERT_ENUM(ScheduleError::AcTempNotSupported, validateSchedule(s));
}

// TC: TC-N206
// REQ: F4
void test_validate_mode_without_temp_ok() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  Schedule s = baseB();
  s.ac = runModeP(mt);
  ASSERT_ENUM(ScheduleError::None, validateSchedule(s));
}

// TC: TC-N207
// REQ: F4
void test_validate_every_fan_choice() {
  for (int i = 0; i < cap::kFanChoiceCount; ++i) {
    const AcFan f = cap::kFanChoices[i];
    Schedule s = baseB();
    AcPatch p; p.power = true; p.fan = f;
    s.ac = p;
    ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), toString(f));
  }
}

// TC: TC-N208
// REQ: F4, F1
void test_registered_patch_passes_model_in_every_mode() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  std::vector<AcPatch> acs;
  acs.push_back(runP(AcMode::Heat, 20));
  acs.push_back(stopP());
  { AcPatch p; p.power = true; p.fan = f1; acs.push_back(p); }
  acs.push_back(runP(AcMode::Cool, cap::kTempMaxC));
  AcMode mt;
  if (findMt(&mt)) acs.push_back(runModeP(mt));

  // 前提：どれも 1.1 を通る
  for (size_t k = 0; k < acs.size(); ++k) {
    Schedule s = baseB();
    s.ac = acs[k];
    ASSERT_ENUM_MSG(ScheduleError::None, validateSchedule(s), "precondition: 1.1 passes");
  }
  int checked = 0;
  for (int i = 0; i < kAcModeCount; ++i) {
    for (size_t k = 0; k < acs.size(); ++k) {
      AcModel m;
      AcPatch toMode; toMode.mode = kAllModes[i];
      ASSERT_ENUM_MSG(AcError::None, m.apply(toMode), toString(kAllModes[i]));
      ASSERT_ENUM_MSG(AcError::None, m.validate(acs[k]), toString(kAllModes[i]));
      ++checked;
    }
  }
  TEST_ASSERT_EQUAL_INT(kAcModeCount * static_cast<int>(acs.size()), checked);
}

// =============================================================================================
// B-3. 一覧の置き換えと採番（ScheduleList::replaceAll）
// =============================================================================================

// TC: TC-N69
// REQ: F4-LIMIT
void test_replace_max_items_ok() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  Schedule buf[kScheduleMax];
  for (int i = 0; i < kScheduleMax; ++i) buf[i] = baseB();
  int bad = -99;
  ASSERT_ENUM(ScheduleError::None, l.replaceAll(buf, kScheduleMax, &bad));
  TEST_ASSERT_EQUAL_INT(kScheduleMax, l.size());
  for (int i = 0; i < kScheduleMax; ++i) TEST_ASSERT_EQUAL_INT(i + 1, l.at(i).id);
}

// TC: TC-N70
// REQ: F4-LIMIT
void test_replace_max_plus_one_rejected() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {0, 0}));
  Schedule buf[kScheduleMax + 1];
  for (int i = 0; i < kScheduleMax + 1; ++i) buf[i] = baseB();
  int bad = -99;
  ASSERT_ENUM(ScheduleError::TooMany, l.replaceAll(buf, kScheduleMax + 1, &bad));
  TEST_ASSERT_EQUAL_INT(-1, bad);
  assertIds(l, {1, 2}, "list unchanged");
}

// TC: TC-N71
// REQ: F4
void test_replace_invalid_item_atomic() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {0, 0}));
  Schedule bad2 = baseB();
  bad2.hour = 24;
  int bad = -99;
  ASSERT_ENUM(ScheduleError::TimeOutOfRange, replaceList(l, {baseB(), baseB(), bad2}, &bad));
  TEST_ASSERT_EQUAL_INT(2, bad);
  assertIds(l, {1, 2}, "list unchanged");
}

// TC: TC-N72
// REQ: F4
void test_replace_duplicate_id() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {0, 0}));
  int bad = -99;
  ASSERT_ENUM(ScheduleError::DuplicateId, replaceIds(l, {1, 2, 1}, &bad));
  TEST_ASSERT_EQUAL_INT(2, bad);
  assertIds(l, {1, 2}, "list unchanged");
}

// TC: TC-N73
// REQ: F4
void test_replace_failure_keeps_next_id() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {0, 0}));
  assertIds(l, {1, 2}, "first");
  ASSERT_ENUM(ScheduleError::DuplicateId, replaceIds(l, {7, 7}));
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {1, 2, 0}));
  assertIds(l, {1, 2, 3}, "next id is 3 (not advanced to 8)");
}

// TC: TC-N74
// REQ: F4
void test_replace_numbering_sequence() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {0, 0}));
  assertIds(l, {1, 2}, "first");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {1, 2, 0}));
  assertIds(l, {1, 2, 3}, "second");
}

// TC: TC-N75
// REQ: F4
void test_replace_delete_does_not_reuse_id() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {0, 0}));
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {1, 0}));
  assertIds(l, {1, 3}, "after delete+add");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {1, 3, 0}));
  assertIds(l, {1, 3, 4}, "next is 4");
}

// TC: TC-N76
// REQ: F4, F4-IO
void test_replace_import_ids_then_number() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {5, 7}));
  assertIds(l, {5, 7}, "import");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {7, 0}));
  assertIds(l, {7, 8}, "second");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {7, 8, 0}));
  assertIds(l, {7, 8, 9}, "next is 9");
}

// TC: TC-N77
// REQ: F4, F4-IO
void test_replace_advance_before_numbering() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {5, 0}));
  assertIds(l, {5, 6}, "[5, none] -> [5, 6]");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {5, 6, 0}));
  assertIds(l, {5, 6, 7}, "next is 7");
}

// TC: TC-N78
// REQ: F4
void test_replace_wrap_after_max_id() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {kScheduleIdMax}));
  assertIds(l, {kScheduleIdMax}, "[9999]");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {kScheduleIdMax, 0}));
  assertIds(l, {kScheduleIdMax, 1}, "[9999, 1]");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {kScheduleIdMax, 1, 0}));
  assertIds(l, {kScheduleIdMax, 1, 2}, "[9999, 1, 2] (skip used 1)");
}

// TC: TC-N79
// REQ: F4
void test_replace_wrap_then_continue() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {kScheduleIdMax}));
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {kScheduleIdMax, 0}));
  assertIds(l, {kScheduleIdMax, 1}, "precondition");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {kScheduleIdMax, 1, 0}));
  assertIds(l, {kScheduleIdMax, 1, 2}, "third");
  ASSERT_ENUM(ScheduleError::None, replaceIds(l, {kScheduleIdMax, 1, 2, 0}));
  assertIds(l, {kScheduleIdMax, 1, 2, 3}, "next is 3");
}

// TC: TC-N80
// REQ: F4
void test_find_by_id() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  Schedule a = baseB(); a.id = 5;
  Schedule b = baseB(); b.id = 7; b.hour = 8; b.minute = 15;
  ASSERT_ENUM(ScheduleError::None, replaceList(l, {a, b}));
  const Schedule* p = l.findById(7);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_INT(7, p->id);
  TEST_ASSERT_EQUAL_INT(8, p->hour);
  TEST_ASSERT_EQUAL_INT(15, p->minute);
  TEST_ASSERT_NULL(l.findById(6));
}

// TC: TC-N81
// REQ: F4
void test_replace_keeps_input_order() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  Schedule a = baseB(); a.hour = 9;
  Schedule b = baseB(); b.hour = 7;
  Schedule c = baseB(); c.hour = 8;
  ASSERT_ENUM(ScheduleError::None, replaceList(l, {a, b, c}));
  TEST_ASSERT_EQUAL_INT(3, l.size());
  TEST_ASSERT_EQUAL_INT(9, l.at(0).hour);
  TEST_ASSERT_EQUAL_INT(7, l.at(1).hour);
  TEST_ASSERT_EQUAL_INT(8, l.at(2).hour);
  for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_INT(0, l.at(i).minute);
}

// =============================================================================================
// B-4. Hub でのスケジュール実行
// =============================================================================================

// TC: TC-N82
// REQ: N-STATE, N-BOOT
void test_hub_starts_empty() {
  Rig g;
  TEST_ASSERT_EQUAL_INT(0, g.hub.schedules().size());
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N83
// REQ: N-TIME
void test_hub_unsynced_never_sends() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  for (int i = 0; i < 121; ++i) g.tickUnsynced();
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N84
// REQ: N-TIME, N-BOOT
void test_hub_first_synced_minute_not_run() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Rig g;
  ASSERT_ENUM(ScheduleError::None,
              g.replace({S(1, 7, 0, kEveryDay, PA()), S(2, 7, 1, kEveryDay, PB())}));
  g.tickAt(T0 + 10);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "sync minute is not run");
  g.tickAt(T0 + 60);
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  ASSERT_ENUM(AcMode::Heat, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(21, g.ir.lastAc.tempC);
}

// TC: TC-N85
// REQ: F4
void test_hub_no_double_run_in_minute() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  g.tickAt(T0 - 30);
  for (int s = 0; s < 60; ++s) g.tickAt(T0 + s);
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
}

// TC: TC-N86
// REQ: F4, N-TIME
void test_hub_check_interval() {
  Rig g;
  g.clock.r = readingAt(T0);
  g.hub.tick(0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.clock.nowCount, "tick(0)");
  g.hub.tick(999);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.clock.nowCount, "tick(999)");
  g.hub.tick(1000);
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.clock.nowCount, "tick(1000)");
}

// TC: TC-N87
// REQ: F4, N-TIME
void test_hub_check_interval_millis_wrap() {
  Rig g;
  g.clock.r = readingAt(T0);
  g.hub.tick(0xFFFFFE0Cu);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.clock.nowCount, "tick(0xFFFFFE0C)");
  g.hub.tick(0x000001F3u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.clock.nowCount, "tick(0x1F3) diff 999");
  g.hub.tick(0x000001F4u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.clock.nowCount, "tick(0x1F4) diff 1000");
}

// TC: TC-N88
// REQ: F4
void test_hub_catch_up_within_limit() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  g.tickAt(T0 - 120);  // 06:58:00
  g.tickAt(T0 + 180);  // 07:03:00（d=5）
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
}

// TC: TC-N89
// REQ: F4
void test_hub_no_catch_up_beyond_limit() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  g.tickAt(T0 - 600);  // 06:50:00
  g.tickAt(T0 + 180);  // 07:03:00（d=13）
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N90
// REQ: F4
void test_hub_clock_back_no_rerun() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  g.tickAt(T0 - 30);
  g.tickAt(T0 + 5);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "07:00:05 sent once");
  g.tickAt(T0 - 10);  // 06:59:50 に戻る
  g.tickAt(T0 + 10);  // 07:00:10
  g.tickAt(T0 + 60);  // 07:01:00
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
}

// TC: TC-N91
// REQ: F4, F1
void test_hub_runs_heat_schedule() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, runP(AcMode::Heat, 20))}));
  g.tickAt(T0 - 30);
  g.tickAt(T0);
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  AcState e;
  e.power = true;
  e.mode = AcMode::Heat;
  e.hasTemp = true;
  e.tempC = 20;
  e.fan = initialSettings(AcMode::Heat).fan;
  e.swingV = AcSwingV::Off;
  e.swingH = AcSwingH::Off;
  assertAcStateEq(e, g.ir.lastAc, "lastAc");
  assertAcStateEq(e, g.hub.acState(), "acState");
}

// TC: TC-N92
// REQ: F4, F1
void test_hub_runs_stop_while_running() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Rig g;
  const Schedule s1 = S(1, 7, 0, kEveryDay, runP(AcMode::Heat, 20));
  ASSERT_ENUM(ScheduleError::None, g.replace({s1}));
  g.tickAt(T0 - 30);
  g.tickAt(T0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "precondition (TC-N91)");
  ASSERT_ENUM(ScheduleError::None, g.replace({s1, S(2, 7, 1, kEveryDay, stopP())}));
  g.tickAt(T0 + 60);
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
  TEST_ASSERT_FALSE(g.ir.lastAc.power);
  ASSERT_ENUM(AcMode::Heat, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(20, g.ir.lastAc.tempC);
}

// TC: TC-N93
// REQ: F4, F1
void test_hub_runs_stop_while_stopped() {
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, stopP())}));
  g.tickAt(T0 - 30);
  g.tickAt(T0);
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  TEST_ASSERT_FALSE(g.ir.lastAc.power);
  ASSERT_ENUM(AcMode::Cool, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(26, g.ir.lastAc.tempC);
}

// TC: TC-N94
// REQ: F4
void test_hub_two_in_same_minute_list_order() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Rig g;
  ASSERT_ENUM(ScheduleError::None,
              g.replace({S(1, 7, 0, kEveryDay, PA()), S(2, 7, 0, kEveryDay, PB())}));
  g.tickAt(T0 - 30);
  g.tickAt(T0);
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
  ASSERT_ENUM(AcMode::Heat, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(21, g.ir.lastAc.tempC);
  ASSERT_ENUM(AcMode::Heat, g.hub.acState().mode);
  TEST_ASSERT_EQUAL_INT(21, g.hub.acState().tempC);
}

// TC: TC-N95
// REQ: F4
void test_hub_replace_does_not_read_clock_or_send() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Rig g;
  g.clock.r = readingAt(T0);
  const int before = g.clock.nowCount;
  ASSERT_ENUM(ScheduleError::None,
              g.replace({S(1, 7, 0, kEveryDay, PA()), S(2, 7, 0, kEveryDay, PB())}));
  TEST_ASSERT_EQUAL_INT(before, g.clock.nowCount);
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N96
// REQ: F4
void test_hub_replace_after_minute_judged() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  g.tickAt(T0 - 30);
  g.tickAt(T0);  // 07:00 を判定済み（一覧は空）
  g.clock.r = readingAt(T0 + 20);
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  g.tickAt(T0 + 21);
  g.tickAt(T0 + 59);
  g.tickAt(T0 + 60);
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N97
// REQ: F4
void test_hub_replace_before_minute_judged() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  g.tickAt(T0 - 1);  // 06:59:59（nowMs=N）
  ASSERT_ENUM(ScheduleError::None, g.replace({S(2, 7, 0, kEveryDay, PB())}));
  g.tickAt(T0);      // 07:00:00（nowMs=N+1000）
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  ASSERT_ENUM(AcMode::Heat, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(21, g.ir.lastAc.tempC);
}

// TC: TC-N98
// REQ: F4
void test_hub_failed_replace_keeps_old_list() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, PA())}));
  g.tickAt(T0 - 30);
  Schedule bad = S(2, 24, 0, kEveryDay, PB());
  ASSERT_ENUM(ScheduleError::TimeOutOfRange, g.replace({bad}));
  g.tickAt(T0);
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  ASSERT_ENUM(AcMode::Cool, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(25, g.ir.lastAc.tempC);
}

// TC: TC-N209
// REQ: F4, F1
void test_hub_runs_mode_without_temp() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  Rig g;
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, runModeP(mt))}));
  g.tickAt(T0 - 30);
  g.tickAt(T0);
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  ASSERT_ENUM(mt, g.ir.lastAc.mode);
  TEST_ASSERT_FALSE(g.ir.lastAc.hasTemp);
  TEST_ASSERT_TRUE(g.ir.lastAc.power);
}

// TC: TC-N210
// REQ: F4, F2
void test_hub_mode_only_restores_last_settings() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  std::string err;
  AcPatch p1; p1.mode = AcMode::Heat; p1.tempC = 22;
  TEST_ASSERT_TRUE(g.hub.applyAc(p1, &err));
  AcPatch p2; p2.fan = f1;
  TEST_ASSERT_TRUE(g.hub.applyAc(p2, &err));
  AcPatch p3; p3.mode = AcMode::Cool;
  TEST_ASSERT_TRUE(g.hub.applyAc(p3, &err));
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "stopped: no send");
  ASSERT_ENUM(ScheduleError::None, g.replace({S(1, 7, 0, kEveryDay, runModeP(AcMode::Heat))}));
  g.tickAt(T0 - 30);
  g.tickAt(T0);
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  TEST_ASSERT_TRUE(g.ir.lastAc.power);
  ASSERT_ENUM(AcMode::Heat, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(22, g.ir.lastAc.tempC);
  ASSERT_ENUM(f1, g.ir.lastAc.fan);
}

// =============================================================================================
// B-5. JSON（schedule_json）
// =============================================================================================

// TC: TC-N100
// REQ: F4-IO
void test_json_export_import_roundtrip() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  ScheduleList a;
  ASSERT_ENUM(ScheduleError::None, replaceList(a, {ex1(), ex2(), ex3(f1)}));
  const std::string body = schedulesToJson(a, ScheduleJsonKind::Export, readingAt(T20));
  ScheduleParseResult r;
  TEST_ASSERT_TRUE_MESSAGE(schedulesFromJson(body, ScheduleJsonKind::Export, &r), r.error.c_str());
  ScheduleList b;
  int bad = -99;
  ASSERT_ENUM(ScheduleError::None, b.replaceAll(r.items, r.count, &bad));
  TEST_ASSERT_EQUAL_INT(3, b.size());
  for (int i = 0; i < 3; ++i) assertScheduleEq(a.at(i), b.at(i), "roundtrip");
  TEST_ASSERT_EQUAL_INT(1, b.at(0).id);
  TEST_ASSERT_EQUAL_INT(2, b.at(1).id);
  TEST_ASSERT_EQUAL_INT(3, b.at(2).id);
}

// TC: TC-N101
// REQ: F4-IO
void test_json_export_empty() {
  ScheduleList empty;
  const std::string body = schedulesToJson(empty, ScheduleJsonKind::Export, readingAt(T20));
  JsonDocument doc;
  TEST_ASSERT_TRUE(readDoc(body, doc));
  TEST_ASSERT_TRUE(doc["version"].is<int>());
  TEST_ASSERT_EQUAL_INT(1, doc["version"].as<int>());
  TEST_ASSERT_TRUE(doc["schedules"].is<JsonArray>());
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(doc["schedules"].as<JsonArray>().size()));
  ScheduleParseResult r;
  TEST_ASSERT_TRUE(schedulesFromJson(body, ScheduleJsonKind::Export, &r));
  TEST_ASSERT_EQUAL_INT(0, r.count);
}

// TC: TC-N102
// REQ: F4
void test_json_list_reads_examples() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  ScheduleParseResult r;
  TEST_ASSERT_TRUE_MESSAGE(
      schedulesFromJson(listBody(exampleItemsJson(f1)), ScheduleJsonKind::List, &r),
      r.error.c_str());
  TEST_ASSERT_EQUAL_INT(3, r.count);
  assertScheduleEq(ex1(), r.items[0], "[0]");
  assertScheduleEq(ex2(), r.items[1], "[1]");
  assertScheduleEq(ex3(f1), r.items[2], "[2]");
  TEST_ASSERT_EQUAL_HEX8(0x3E, r.items[0].days);
  TEST_ASSERT_EQUAL_HEX8(0x7F, r.items[1].days);
  TEST_ASSERT_EQUAL_HEX8(0x41, r.items[2].days);
  TEST_ASSERT_FALSE(r.items[2].enabled);
  TEST_ASSERT_FALSE(r.items[2].ac.tempC.has_value());
  TEST_ASSERT_FALSE(r.items[0].ac.fan.has_value());
}

// TC: TC-N103
// REQ: F4
void test_json_days_output_sunday_first() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  ScheduleList l;
  Schedule s = ex1();
  s.days = kSat | kSun;
  ASSERT_ENUM(ScheduleError::None, replaceList(l, {s}));
  const std::string body = schedulesToJson(l, ScheduleJsonKind::List, unsyncedReading());
  JsonDocument doc;
  TEST_ASSERT_TRUE(readDoc(body, doc));
  JsonArray days = doc["schedules"][0]["days"].as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(days.size()));
  TEST_ASSERT_EQUAL_STRING("sun", days[0].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("sat", days[1].as<const char*>());
}

// TC: TC-N104
// REQ: F4
void test_json_action_output_keys_and_order() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceList(l, {ex1(), ex2(), ex3(f1)}));
  const std::string body = schedulesToJson(l, ScheduleJsonKind::List, unsyncedReading());
  TEST_ASSERT_TRUE_MESSAGE(
      body.find(R"("action":{"power":true,"mode":"heat","temp":20})") != std::string::npos,
      "action of id 1");
  TEST_ASSERT_TRUE_MESSAGE(body.find(R"("action":{"power":false})") != std::string::npos,
                           "action of id 2");
  const std::string a3 =
      std::string(R"("action":{"power":true,"mode":"auto","fan":")") + toString(f1) + "\"}";
  TEST_ASSERT_TRUE_MESSAGE(body.find(a3) != std::string::npos, "action of id 3");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, countOf(body, "swingV"), "no swingV");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, countOf(body, "swingH"), "no swingH");
  TEST_ASSERT_EQUAL_INT(3, countOf(body, R"("target":"ac")"));
}

// TC: TC-N105
// REQ: F4-LIMIT
void test_json_list_has_max() {
  ScheduleList empty;
  const std::string body = schedulesToJson(empty, ScheduleJsonKind::List, unsyncedReading());
  JsonDocument doc;
  TEST_ASSERT_TRUE(readDoc(body, doc));
  TEST_ASSERT_EQUAL_INT(kScheduleMax, doc["max"].as<int>());
  TEST_ASSERT_TRUE(doc["schedules"].is<JsonArray>());
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(doc["schedules"].as<JsonArray>().size()));
  TEST_ASSERT_FALSE(hasKey(doc.as<JsonObject>(), "version"));
}

// TC: TC-N106
// REQ: F4-IO
void test_json_export_exported_at() {
  ScheduleList empty;
  {
    const std::string body = schedulesToJson(empty, ScheduleJsonKind::Export, readingAt(T20));
    JsonDocument doc;
    TEST_ASSERT_TRUE(readDoc(body, doc));
    TEST_ASSERT_EQUAL_STRING("2026-09-25T20:00:00+09:00", doc["exportedAt"].as<const char*>());
    TEST_ASSERT_FALSE(hasKey(doc.as<JsonObject>(), "max"));
  }
  {
    const ClockReading c{true, LocalTime{2026, 1, 5, 7, 3, 9, 1}, 1767564189};
    const std::string body = schedulesToJson(empty, ScheduleJsonKind::Export, c);
    JsonDocument doc;
    TEST_ASSERT_TRUE(readDoc(body, doc));
    TEST_ASSERT_EQUAL_STRING("2026-01-05T07:03:09+09:00", doc["exportedAt"].as<const char*>());
    TEST_ASSERT_FALSE(hasKey(doc.as<JsonObject>(), "max"));
  }
}

// TC: TC-N107
// REQ: F4-IO, N-TIME
void test_json_export_unsynced_null() {
  ScheduleList empty;
  const std::string body = schedulesToJson(empty, ScheduleJsonKind::Export, unsyncedReading());
  JsonDocument doc;
  TEST_ASSERT_TRUE(readDoc(body, doc));
  TEST_ASSERT_TRUE_MESSAGE(hasKey(doc.as<JsonObject>(), "exportedAt"), "key exists");
  TEST_ASSERT_TRUE_MESSAGE(doc["exportedAt"].isNull(), "value is null");
}

// TC: TC-N108
// REQ: F4-IO
void test_json_export_filename() {
  TEST_ASSERT_EQUAL_STRING("irhub-schedules-20260925-2000.json",
                           exportFilename(readingAt(T20)).c_str());
  const ClockReading c{true, LocalTime{2026, 1, 5, 7, 3, 9, 1}, 1767564189};
  TEST_ASSERT_EQUAL_STRING("irhub-schedules-20260105-0703.json", exportFilename(c).c_str());
  TEST_ASSERT_EQUAL_STRING("irhub-schedules.json", exportFilename(unsyncedReading()).c_str());
}

// TC: TC-N109
// REQ: F4-IO
void test_json_body_size_limit() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  const std::string base = exportOne(J());
  TEST_ASSERT_TRUE(base.size() < kScheduleJsonMaxBytes);
  const std::string atMax =
      "{" + std::string(kScheduleJsonMaxBytes - base.size(), ' ') + base.substr(1);
  const std::string over = " " + atMax;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kScheduleJsonMaxBytes), static_cast<int>(atMax.size()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kScheduleJsonMaxBytes + 1), static_cast<int>(over.size()));
  ScheduleParseResult r;
  TEST_ASSERT_TRUE_MESSAGE(schedulesFromJson(atMax, ScheduleJsonKind::Export, &r), r.error.c_str());
  expectExportError(over, "body too large", "8193 bytes");
}

// TC: TC-N110
// REQ: F4-IO
void test_json_invalid() {
  expectExportError("{", "invalid json", "{");
  expectExportError("", "invalid json", "empty");
  expectExportError("[]", "invalid json", "[]");
}

// TC: TC-N111
// REQ: F4-IO
void test_json_version_missing() {
  expectExportError(R"({"schedules":[]})", "version missing", "no version");
}

// TC: TC-N112
// REQ: F4-IO
void test_json_unsupported_version() {
  expectExportError(R"({"version":2,"schedules":[]})", "unsupported version", "2");
  expectExportError(R"({"version":"1","schedules":[]})", "unsupported version", "\"1\"");
}

// TC: TC-N113
// REQ: F4-IO
void test_json_schedules_missing() {
  expectExportError(R"({"version":1})", "schedules missing", "no schedules");
  expectExportError(R"({"version":1,"schedules":{}})", "schedules missing", "object");
}

// TC: TC-N114
// REQ: F4-LIMIT, F4-IO
void test_json_count_limit() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  J noId;
  noId.id = "";
  std::vector<std::string> items;
  for (int i = 0; i < kScheduleMax + 1; ++i) items.push_back(noId.str());
  char msg[48];
  snprintf(msg, sizeof msg, "too many schedules (max %d)", kScheduleMax);
  expectExportError(exportBody(items), msg, "max+1 items");
  items.pop_back();
  ScheduleParseResult r;
  TEST_ASSERT_TRUE_MESSAGE(schedulesFromJson(exportBody(items), ScheduleJsonKind::Export, &r),
                           r.error.c_str());
  TEST_ASSERT_EQUAL_INT(kScheduleMax, r.count);
}

// TC: TC-N115
// REQ: F4-IO
void test_json_time_format() {
  const char* bad[] = {R"("7:00")", R"("07:00:00")", R"("24:00")", R"("07:60")"};
  for (const char* t : bad) {
    J j;
    j.time = t;
    expectExportError(exportOne(j), "schedules[0].time: must be HH:MM", t);
  }
}

// TC: TC-N116
// REQ: F4-IO
void test_json_days_errors() {
  J a; a.days = R"(["mo"])";
  expectExportError(exportOne(a), "schedules[0].days: unknown day \"mo\"", "mo");
  J b; b.days = R"(["mon","mon"])";
  expectExportError(exportOne(b), "schedules[0].days: duplicate day", "dup");
  J c; c.days = "[]";
  expectExportError(exportOne(c), "schedules[0].days: empty", "empty");
}

// TC: TC-N117
// REQ: F4-IO, F3
void test_json_unknown_target() {
  J a; a.target = R"("tv")";
  expectExportError(exportOne(a), "schedules[0].target: unknown target", "tv");
  J b; b.target = R"("light")";
  expectExportError(exportOne(b), "schedules[0].target: unknown target", "light");
}

// TC: TC-N118
// REQ: F4-IO
void test_json_action_unknown_key() {
  expectExportError(exportOne(withAction(R"({"power":true,"tmp":20})")),
                    "schedules[0].action: unknown key \"tmp\"", "tmp");
}

// TC: TC-N119
// REQ: F4-IO
void test_json_temp_not_integer() {
  expectExportError(exportOne(withAction(R"({"power":true,"mode":"heat","temp":26.5})")),
                    "schedules[0].action.temp: must be integer", "26.5");
}

// TC: TC-N120
// REQ: F4-IO, F3
void test_json_action_swing_and_button_keys() {
  expectExportError(exportOne(withAction(R"({"power":true,"swingV":"off"})")),
                    "schedules[0].action: unknown key \"swingV\"", "(a) swingV");
  expectExportError(exportOne(withAction(R"({"power":true,"swingH":"off"})")),
                    "schedules[0].action: unknown key \"swingH\"", "(b) swingH");
  expectExportError(exportOne(withAction(R"({"button":"full"})")),
                    "schedules[0].action: unknown key \"button\"", "(c) button");
}

// TC: TC-N121
// REQ: F4-IO
void test_json_missing_and_wrong_type() {
  J a; a.time = "";
  expectExportError(exportOne(a), "schedules[0].time: missing", "no time");
  J b; b.enabled = R"("yes")";
  expectExportError(exportOne(b), "schedules[0].enabled: must be boolean", "enabled yes");
}

// TC: TC-N122
// REQ: F4-IO
void test_json_item_not_object() {
  J a;
  J b; b.id = "2";
  expectExportError(exportBody({a.str(), b.str(), "5"}), "schedules[2]: not an object", "[2]=5");
}

// TC: TC-N123
// REQ: F4, F4-IO
void test_json_validate_messages() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  expectExportError(exportOne(withAction(R"({"power":true,"temp":20})")),
                    "schedules[0]: temp requires mode", "temp without mode");
  expectExportError(exportOne(withAction(R"({"power":false,"mode":"cool"})")),
                    "schedules[0]: ac stop takes only power", "stop+mode");
  const std::string a = R"({"power":true,"mode":"cool","temp":)" +
                        std::to_string(cap::kTempMaxC + 1) + "}";
  expectExportError(exportOne(withAction(a)), "schedules[0]: temp out of range", "max+1");
}

// TC: TC-N124
// REQ: F4-IO
void test_json_id_out_of_range() {
  const char* ids[] = {"0", "-1", "10000", "70000"};
  for (const char* id : ids) {
    J j;
    j.id = id;
    expectExportError(exportOne(j), "schedules[0].id: out of range", id);
  }
}

// TC: TC-N125
// REQ: F4-IO
void test_json_id_not_integer() {
  const char* ids[] = {"1.5", R"("1")", "true", "null"};
  for (const char* id : ids) {
    J j;
    j.id = id;
    expectExportError(exportOne(j), "schedules[0].id: must be integer", id);
  }
}

// TC: TC-N126
// REQ: F4-IO
void test_json_id_accepted() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  const char* ids[] = {"1", "9999", ""};
  const int expected[] = {1, 9999, 0};
  for (int i = 0; i < 3; ++i) {
    J j;
    j.id = ids[i];
    ScheduleParseResult r;
    TEST_ASSERT_TRUE_MESSAGE(schedulesFromJson(exportOne(j), ScheduleJsonKind::Export, &r),
                             r.error.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected[i], r.items[0].id, ids[i]);
  }
}

// TC: TC-N127
// REQ: F4
void test_json_list_ignores_version_and_outer_keys() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  {
    const std::string body = R"({"max":3,"schedules":[)" + J().str() + "]}";
    ScheduleParseResult r;
    TEST_ASSERT_TRUE_MESSAGE(schedulesFromJson(body, ScheduleJsonKind::List, &r), r.error.c_str());
    TEST_ASSERT_EQUAL_INT(1, r.count);
  }
  {
    const std::string body = R"({"version":1,"foo":1,"schedules":[)" + J().str() + "]}";
    ScheduleParseResult r;
    TEST_ASSERT_TRUE_MESSAGE(schedulesFromJson(body, ScheduleJsonKind::Export, &r),
                             r.error.c_str());
    TEST_ASSERT_EQUAL_INT(1, r.count);
  }
}

// TC: TC-N128
// REQ: F4-IO
void test_json_error_index_points_second() {
  J a;
  J b; b.id = "2"; b.time = R"("7:00")";
  expectExportError(exportBody({a.str(), b.str()}), "schedules[1].time: must be HH:MM", "[1]");
}

// TC: TC-N129
// REQ: F4-IO
void test_json_id_beyond_int() {
  J j;
  j.id = "10000000000";
  ScheduleParseResult r;
  TEST_ASSERT_FALSE(schedulesFromJson(exportOne(j), ScheduleJsonKind::Export, &r));
  const bool outOfRange = (r.error == "schedules[0].id: out of range");
  const bool notInteger = (r.error == "schedules[0].id: must be integer");
  TEST_ASSERT_TRUE_MESSAGE(outOfRange || notInteger, r.error.c_str());
  TEST_MESSAGE(outOfRange ? "id 10000000000 -> out of range (is<long long> available)"
                          : "id 10000000000 -> must be integer (step 4 omitted)");
}

// TC: TC-N182
// REQ: F4-IO
void test_json_power_missing() {
  expectExportError(exportOne(withAction(R"({"mode":"heat","temp":20})")),
                    "schedules[0].action.power: missing", "no power");
}

// TC: TC-N183
// REQ: F4-IO
void test_json_item_unknown_key() {
  J j;
  j.extra = R"("note":"x")";
  expectExportError(exportOne(j), "schedules[0]: unknown key \"note\"", "note");
}

// TC: TC-N197
// REQ: F4-IO, N-TIME
void test_format_jst_iso() {
  TEST_ASSERT_EQUAL_STRING("2026-01-05T07:03:09+09:00",
                           formatJstIso(LocalTime{2026, 1, 5, 7, 3, 9, 1}).c_str());
  TEST_ASSERT_EQUAL_STRING("2026-09-25T20:00:00+09:00",
                           formatJstIso(LocalTime{2026, 9, 25, 20, 0, 0, 5}).c_str());
}

// TC: TC-N198
// REQ: F4-IO
void test_json_exported_at_uses_format_jst_iso() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  ScheduleList l;
  ASSERT_ENUM(ScheduleError::None, replaceList(l, {ex1(), ex2(), ex3(f1)}));
  const ClockReading now{true, LocalTime{2026, 1, 5, 7, 3, 9, 1}, 1767564189};
  const std::string body = schedulesToJson(l, ScheduleJsonKind::Export, now);
  JsonDocument doc;
  TEST_ASSERT_TRUE(readDoc(body, doc));
  TEST_ASSERT_EQUAL_STRING(formatJstIso(now.local).c_str(), doc["exportedAt"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("2026-01-05T07:03:09+09:00", doc["exportedAt"].as<const char*>());
}

// TC: TC-N211
// REQ: F4-IO
void test_json_unknown_fan_and_mode_strings() {
  expectExportError(exportOne(withAction(R"({"power":true,"fan":"turbo"})")),
                    "schedules[0].action.fan: unknown fan", "turbo");
  expectExportError(exportOne(withAction(R"({"power":true,"fan":"quiet"})")),
                    "schedules[0].action.fan: unknown fan", "quiet");
  expectExportError(exportOne(withAction(R"({"power":true,"mode":"fan"})")),
                    "schedules[0].action.mode: unknown mode", "fan");
  expectExportError(exportOne(withAction(R"({"power":true,"mode":"Cool"})")),
                    "schedules[0].action.mode: unknown mode", "Cool");
}

// TC: TC-N212
// REQ: F4, F4-IO
void test_json_temp_in_mode_without_temp() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  const std::string m = toString(mt);
  expectExportError(
      exportOne(withAction(R"({"power":true,"mode":")" + m + R"(","temp":24})")),
      "schedules[0]: temp not supported in this mode", "Mt+24");
  expectExportError(exportOne(withAction(R"({"power":true,"mode":")" + m + R"(","temp":)" +
                                         std::to_string(cap::kTempMaxC + 1) + "}")),
                    "schedules[0]: temp not supported in this mode", "Mt+max+1");
}

// TC: TC-N213
// REQ: F4, F4-IO
void test_json_fan_not_supported() {
  AcFan fx;
  if (!findFx(&fx)) TEST_IGNORE_MESSAGE("every AcFan is a choice (F1-VALUES)");
  expectExportError(
      exportOne(withAction(std::string(R"({"power":true,"fan":")") + toString(fx) + "\"}")),
      "schedules[0]: fan not supported", toString(fx));
}

// TC: TC-N214
// REQ: F4-IO
void test_json_every_fan_choice_string() {
  for (int i = 0; i < cap::kFanChoiceCount; ++i) {
    const AcFan f = cap::kFanChoices[i];
    ScheduleParseResult r;
    const std::string body =
        exportOne(withAction(std::string(R"({"power":true,"fan":")") + toString(f) + "\"}"));
    TEST_ASSERT_TRUE_MESSAGE(schedulesFromJson(body, ScheduleJsonKind::Export, &r), toString(f));
    TEST_ASSERT_TRUE_MESSAGE(r.items[0].ac.fan.has_value(), toString(f));
    ASSERT_ENUM_MSG(f, *r.items[0].ac.fan, toString(f));
  }
}

// TC: TC-N215
// REQ: F4-IO
void test_json_unknown_key_before_time() {
  J j;
  j.time = R"("7:00")";
  j.extra = R"("note":"x")";
  expectExportError(exportOne(j), "schedules[0]: unknown key \"note\"", "note + bad time");
}

// TC: TC-N216
// REQ: F4-IO
void test_json_per_item_order() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  J a = withAction(R"({"power":true,"mode":"cool","temp":)" +
                   std::to_string(cap::kTempMaxC + 1) + "}");
  J b; b.id = "2"; b.time = R"("7:00")";
  expectExportError(exportBody({a.str(), b.str()}), "schedules[0]: temp out of range",
                    "item 0 step 8 before item 1 step 7");
}

int main(int, char**) {
  UNITY_BEGIN();
  // B-1
  RUN_TEST(test_jst_friday_2000);
  RUN_TEST(test_jst_friday_0700);
  RUN_TEST(test_jst_day_boundary_fri_to_sat);
  RUN_TEST(test_jst_week_boundary_sat_to_sun);
  RUN_TEST(test_matches_exact);
  RUN_TEST(test_matches_wrong_time);
  RUN_TEST(test_matches_wrong_day);
  RUN_TEST(test_matches_disabled);
  RUN_TEST(test_window_first_after_sync);
  RUN_TEST(test_window_same_minute);
  RUN_TEST(test_window_next_minute);
  RUN_TEST(test_window_late_2);
  RUN_TEST(test_window_late_catchup_max);
  RUN_TEST(test_window_jump_beyond_catchup);
  RUN_TEST(test_window_clock_back_1);
  RUN_TEST(test_window_clock_back_300);
  RUN_TEST(test_due_same_minute_list_order);
  RUN_TEST(test_due_minute_ascending);
  RUN_TEST(test_due_across_midnight_weekday_switch);
  RUN_TEST(test_due_cap);
  // B-2
  RUN_TEST(test_validate_base_and_stop);
  RUN_TEST(test_validate_time_edges_ok);
  RUN_TEST(test_validate_time_out_of_range);
  RUN_TEST(test_validate_id_range);
  RUN_TEST(test_validate_days);
  RUN_TEST(test_validate_power_missing);
  RUN_TEST(test_validate_stop_with_other_fields);
  RUN_TEST(test_validate_temp_without_mode);
  RUN_TEST(test_validate_temp_not_supported_mode);
  RUN_TEST(test_validate_temp_edges);
  RUN_TEST(test_validate_fan_and_swing_not_supported);
  RUN_TEST(test_validate_disabled_still_checked);
  RUN_TEST(test_validate_order_time_before_days);
  RUN_TEST(test_schedule_error_messages);
  RUN_TEST(test_validate_not_supported_before_out_of_range);
  RUN_TEST(test_validate_mode_without_temp_ok);
  RUN_TEST(test_validate_every_fan_choice);
  RUN_TEST(test_registered_patch_passes_model_in_every_mode);
  // B-3
  RUN_TEST(test_replace_max_items_ok);
  RUN_TEST(test_replace_max_plus_one_rejected);
  RUN_TEST(test_replace_invalid_item_atomic);
  RUN_TEST(test_replace_duplicate_id);
  RUN_TEST(test_replace_failure_keeps_next_id);
  RUN_TEST(test_replace_numbering_sequence);
  RUN_TEST(test_replace_delete_does_not_reuse_id);
  RUN_TEST(test_replace_import_ids_then_number);
  RUN_TEST(test_replace_advance_before_numbering);
  RUN_TEST(test_replace_wrap_after_max_id);
  RUN_TEST(test_replace_wrap_then_continue);
  RUN_TEST(test_find_by_id);
  RUN_TEST(test_replace_keeps_input_order);
  // B-4
  RUN_TEST(test_hub_starts_empty);
  RUN_TEST(test_hub_unsynced_never_sends);
  RUN_TEST(test_hub_first_synced_minute_not_run);
  RUN_TEST(test_hub_no_double_run_in_minute);
  RUN_TEST(test_hub_check_interval);
  RUN_TEST(test_hub_check_interval_millis_wrap);
  RUN_TEST(test_hub_catch_up_within_limit);
  RUN_TEST(test_hub_no_catch_up_beyond_limit);
  RUN_TEST(test_hub_clock_back_no_rerun);
  RUN_TEST(test_hub_runs_heat_schedule);
  RUN_TEST(test_hub_runs_stop_while_running);
  RUN_TEST(test_hub_runs_stop_while_stopped);
  RUN_TEST(test_hub_two_in_same_minute_list_order);
  RUN_TEST(test_hub_replace_does_not_read_clock_or_send);
  RUN_TEST(test_hub_replace_after_minute_judged);
  RUN_TEST(test_hub_replace_before_minute_judged);
  RUN_TEST(test_hub_failed_replace_keeps_old_list);
  RUN_TEST(test_hub_runs_mode_without_temp);
  RUN_TEST(test_hub_mode_only_restores_last_settings);
  // B-5
  RUN_TEST(test_json_export_import_roundtrip);
  RUN_TEST(test_json_export_empty);
  RUN_TEST(test_json_list_reads_examples);
  RUN_TEST(test_json_days_output_sunday_first);
  RUN_TEST(test_json_action_output_keys_and_order);
  RUN_TEST(test_json_list_has_max);
  RUN_TEST(test_json_export_exported_at);
  RUN_TEST(test_json_export_unsynced_null);
  RUN_TEST(test_json_export_filename);
  RUN_TEST(test_json_body_size_limit);
  RUN_TEST(test_json_invalid);
  RUN_TEST(test_json_version_missing);
  RUN_TEST(test_json_unsupported_version);
  RUN_TEST(test_json_schedules_missing);
  RUN_TEST(test_json_count_limit);
  RUN_TEST(test_json_time_format);
  RUN_TEST(test_json_days_errors);
  RUN_TEST(test_json_unknown_target);
  RUN_TEST(test_json_action_unknown_key);
  RUN_TEST(test_json_temp_not_integer);
  RUN_TEST(test_json_action_swing_and_button_keys);
  RUN_TEST(test_json_missing_and_wrong_type);
  RUN_TEST(test_json_item_not_object);
  RUN_TEST(test_json_validate_messages);
  RUN_TEST(test_json_id_out_of_range);
  RUN_TEST(test_json_id_not_integer);
  RUN_TEST(test_json_id_accepted);
  RUN_TEST(test_json_list_ignores_version_and_outer_keys);
  RUN_TEST(test_json_error_index_points_second);
  RUN_TEST(test_json_id_beyond_int);
  RUN_TEST(test_json_power_missing);
  RUN_TEST(test_json_item_unknown_key);
  RUN_TEST(test_format_jst_iso);
  RUN_TEST(test_json_exported_at_uses_format_jst_iso);
  RUN_TEST(test_json_unknown_fan_and_mode_strings);
  RUN_TEST(test_json_temp_in_mode_without_temp);
  RUN_TEST(test_json_fan_not_supported);
  RUN_TEST(test_json_every_fan_choice_string);
  RUN_TEST(test_json_unknown_key_before_time);
  RUN_TEST(test_json_per_item_order);
  return UNITY_END();
}
