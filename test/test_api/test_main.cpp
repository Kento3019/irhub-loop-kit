// test_api：Hub の基本動作（N-BOOT・N-STATE・F5・F1）と ApiRouter（HTTP 非依存の API ハンドラ）の単体テスト
// 根拠：docs/design/04-api.md（D-04）、docs/design/01-architecture.md（D-01 4〜6・9節）、
//       docs/design/02-ac-state.md（D-02 4・6節）、docs/design/03-schedule.md（D-03 6節）、
//       docs/test/test-plan.md「C. test/test_api」
//       （TC-N130〜TC-N181、TC-N184〜TC-N196、TC-N199〜TC-N201、TC-N218〜TC-N220）
// 照明（F3）は対象外。照明の API・ボタン名は作らないので、ここでは「作っていないこと」（404・unknown key）だけを確かめる。
// 風向は API に出さず受け付けない（D-04 冒頭・4節）。
// F1-VALUES の値（温度範囲・温度を指定できるモード・風量の選択肢）と仮値（F4-LIMIT の件数、F2 の初期値）は
// 直書きせず、cap::・kScheduleMax・initialSettings() から作る（テスト計画 方針2）。
// フェイク（赤外線送信・時計・センサー）は D-01 4節のインターフェースに対してこのファイルの中に書く。
#include <unity.h>

#include <ArduinoJson.h>

#include <cstdio>
#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ac_capabilities.h"
#include "ac_state.h"
#include "api.h"
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
  std::vector<ClockReading> seq;  // 空でなければ r の代わりに先頭から1つずつ返し、最後の値を返し続ける
  size_t seqIdx = 0;
  void setSeq(std::vector<ClockReading> s) {
    seq = std::move(s);
    seqIdx = 0;
  }
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

#define REQUIRE_FAN(f)                                                \
  do {                                                                \
    if (!cap::fanSupported(f)) {                                      \
      TEST_IGNORE_MESSAGE("fan is not a choice (F1-VALUES)");         \
    }                                                                 \
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

// 基準の時刻（テスト計画 方針4）
static const int64_t T0 = 1790287200;   // 2026-09-25（金）07:00:00 JST
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
// 2026-01-05（月）07:03:09 JST（1桁の月・日・時・分・秒のゼロ埋めを見る）
static ClockReading reading20260105() {
  return ClockReading{true, LocalTime{2026, 1, 5, 7, 3, 9, 1}, 1767564189};
}

static AcPatch pPower(bool v) { AcPatch p; p.power = v; return p; }
static AcPatch pTemp(int t) { AcPatch p; p.tempC = t; return p; }
static AcPatch pMode(AcMode m) { AcPatch p; p.mode = m; return p; }
static AcPatch pFan(AcFan f) { AcPatch p; p.fan = f; return p; }

static void assertAcStateEq(const AcState& e, const AcState& a, const char* msg) {
  TEST_ASSERT_EQUAL_MESSAGE(e.power, a.power, msg);
  ASSERT_ENUM_MSG(e.mode, a.mode, msg);
  TEST_ASSERT_EQUAL_MESSAGE(e.hasTemp, a.hasTemp, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(e.tempC, a.tempC, msg);
  ASSERT_ENUM_MSG(e.fan, a.fan, msg);
  ASSERT_ENUM_MSG(e.swingV, a.swingV, msg);
  ASSERT_ENUM_MSG(e.swingH, a.swingH, msg);
}

static AcState makeState(bool power, AcMode mode, int tempC, AcFan fan) {
  AcState s;
  s.power = power;
  s.mode = mode;
  s.hasTemp = cap::tempSupported(mode);
  s.tempC = static_cast<int8_t>(tempC);
  s.fan = fan;
  s.swingV = AcSwingV::Off;
  s.swingH = AcSwingH::Off;
  return s;
}

// TC-N01 と同じ初期状態
static void assertInitialAc(const AcState& s, const char* msg) {
  TEST_ASSERT_FALSE_MESSAGE(s.power, msg);
  ASSERT_ENUM_MSG(AcMode::Cool, s.mode, msg);
  TEST_ASSERT_EQUAL_MESSAGE(cap::tempSupported(AcMode::Cool), s.hasTemp, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(26, s.tempC, msg);
  ASSERT_ENUM_MSG(AcFan::Auto, s.fan, msg);
  ASSERT_ENUM_MSG(cap::kSwingVDefault, s.swingV, msg);
  ASSERT_ENUM_MSG(cap::kSwingHDefault, s.swingH, msg);
}

// Hub と ApiRouter をつないだ道具（テスト計画 C の共通の前提）
struct Rig {
  FakeIrSender ir;
  FakeClock clock;
  FakeClimateSensor sensor;
  Hub hub{ir, clock, sensor};
  ApiRouter api{hub};
  ApiResponse call(HttpMethod m, const std::string& path, const std::string& body = "") {
    ApiRequest r{m, path, body};
    return api.handle(r);
  }
  ApiResponse postAc(const std::string& body) { return call(HttpMethod::Post, "/api/ac", body); }
  ApiResponse status() { return call(HttpMethod::Get, "/api/status"); }
  bool apply(const AcPatch& p, std::string* err = nullptr) {
    std::string e;
    const bool ok = hub.applyAc(p, &e);
    if (err) *err = e;
    return ok;
  }
};

// ---- JSON の道具 ----------------------------------------------------------------------------

static void parseBody(const std::string& s, JsonDocument& doc, const char* msg) {
  const DeserializationError err = deserializeJson(doc, s.c_str(), s.size());
  TEST_ASSERT_FALSE_MESSAGE(static_cast<bool>(err), msg);
}
static bool hasKey(JsonObject o, const char* k) {
  for (JsonPair kv : o) {
    if (std::string(kv.key().c_str()) == k) return true;
  }
  return false;
}
// o のキーの集合がちょうど keys であること
static void assertKeys(JsonObject o, std::initializer_list<const char*> keys, const char* msg) {
  std::set<std::string> actual;
  for (JsonPair kv : o) actual.insert(kv.key().c_str());
  std::set<std::string> expected;
  for (const char* k : keys) expected.insert(k);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected.size()), static_cast<int>(actual.size()),
                                msg);
  for (const std::string& k : expected) {
    TEST_ASSERT_TRUE_MESSAGE(actual.count(k) == 1, (std::string(msg) + " missing " + k).c_str());
  }
}
static void assertStringArray(JsonArray a, const std::vector<std::string>& e, const char* msg) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(e.size()), static_cast<int>(a.size()), msg);
  for (size_t i = 0; i < e.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(e[i].c_str(), a[i].as<const char*>(), msg);
  }
}

// 400／404／405 の本文が {"error":"<message>"} の1キーだけであること
static void assertError(const ApiResponse& res, int status, const std::string& message,
                        const std::string& label) {
  const char* l = label.c_str();
  TEST_ASSERT_EQUAL_INT_MESSAGE(status, res.status, l);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("application/json", res.contentType.c_str(), l);
  JsonDocument doc;
  parseBody(res.body, doc, l);
  TEST_ASSERT_TRUE_MESSAGE(doc.is<JsonObject>(), l);
  assertKeys(doc.as<JsonObject>(), {"error"}, l);
  TEST_ASSERT_TRUE_MESSAGE(doc["error"].is<const char*>(), l);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(message.c_str(), doc["error"].as<const char*>(), l);
}
static void assertAcError(Rig& g, const std::string& body, const std::string& message) {
  assertError(g.postAc(body), 400, message, "POST /api/ac " + body);
}

// 成功の応答の "ac" を読む（200 と {"ac":{...}} を確かめる）
static void readAcResponse(const ApiResponse& res, JsonDocument& doc, const std::string& label) {
  const char* l = label.c_str();
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, res.status, l);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("application/json", res.contentType.c_str(), l);
  parseBody(res.body, doc, l);
  TEST_ASSERT_TRUE_MESSAGE(doc["ac"].is<JsonObject>(), l);
}
// "ac" の4キーと値（temp < 0 は null を期待）
static void assertAcJson(JsonObject ac, bool power, const char* mode, int temp, const char* fan,
                         const char* msg) {
  assertKeys(ac, {"power", "mode", "temp", "fan"}, msg);
  TEST_ASSERT_TRUE_MESSAGE(ac["power"].is<bool>(), msg);
  TEST_ASSERT_EQUAL_MESSAGE(power, ac["power"].as<bool>(), msg);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(mode, ac["mode"].as<const char*>(), msg);
  if (temp < 0) {
    TEST_ASSERT_TRUE_MESSAGE(ac["temp"].isNull(), msg);
  } else {
    TEST_ASSERT_TRUE_MESSAGE(ac["temp"].is<int>(), msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(temp, ac["temp"].as<int>(), msg);
  }
  TEST_ASSERT_EQUAL_STRING_MESSAGE(fan, ac["fan"].as<const char*>(), msg);
}

// スケジュールの本文
static const std::string kValidItem =
    R"({"enabled":true,"time":"07:00","days":["mon"],"target":"ac","action":{"power":true}})";
static std::string itemWithId(int id) {
  return "{\"id\":" + std::to_string(id) + "," + kValidItem.substr(1);
}
static std::string joinItems(const std::vector<std::string>& items) {
  std::string s;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) s += ",";
    s += items[i];
  }
  return s;
}
static std::string listBody(const std::vector<std::string>& items) {
  return R"({"schedules":[)" + joinItems(items) + "]}";
}
static std::string exportBody(const std::vector<std::string>& items) {
  return R"({"version":1,"schedules":[)" + joinItems(items) + "]}";
}
// 例の3件（テスト計画 B-5、D-03 6.1）。id 3 の風量は F1（確定値では min）
static std::vector<std::string> exampleItems(AcFan f1) {
  return {
      R"({"id":1,"enabled":true,"time":"07:00","days":["mon","tue","wed","thu","fri"],"target":"ac","action":{"power":true,"mode":"heat","temp":20}})",
      R"({"id":2,"enabled":true,"time":"23:30","days":["sun","mon","tue","wed","thu","fri","sat"],"target":"ac","action":{"power":false}})",
      std::string(
          R"({"id":3,"enabled":false,"time":"06:45","days":["sat","sun"],"target":"ac","action":{"power":true,"mode":"auto","fan":")") +
          toString(f1) + "\"}}",
  };
}

static ApiResponse putSchedules(Rig& g, const std::string& body) {
  return g.call(HttpMethod::Put, "/api/schedules", body);
}
static ApiResponse importSchedules(Rig& g, const std::string& body) {
  return g.call(HttpMethod::Post, "/api/schedules/import", body);
}
// GET /api/schedules の "schedules" 配列を文字列にしたもの（一覧が変わっていないことの比較用）
static std::string scheduleArray(Rig& g) {
  const ApiResponse res = g.call(HttpMethod::Get, "/api/schedules");
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, res.status, "GET /api/schedules");
  JsonDocument doc;
  parseBody(res.body, doc, "GET /api/schedules");
  TEST_ASSERT_TRUE_MESSAGE(doc["schedules"].is<JsonArray>(), "schedules array");
  std::string out;
  serializeJson(doc["schedules"], out);
  return out;
}
static void assertScheduleIds(Rig& g, std::initializer_list<int> ids, const char* msg) {
  const ApiResponse res = g.call(HttpMethod::Get, "/api/schedules");
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, res.status, msg);
  JsonDocument doc;
  parseBody(res.body, doc, msg);
  JsonArray a = doc["schedules"].as<JsonArray>();
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(ids.size()), static_cast<int>(a.size()), msg);
  int i = 0;
  for (int id : ids) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(id, a[i]["id"].as<int>(), msg);
    ++i;
  }
}
// TC-N170 の2件（id 1, 2）を入れる
static void putTwo(Rig& g) {
  const ApiResponse res = putSchedules(g, listBody({kValidItem, kValidItem}));
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, res.status, "precondition: PUT 2 items");
}

void setUp() {}
void tearDown() {}

// =============================================================================================
// C-1. Hub（N-BOOT・N-STATE・F5・F1）
// =============================================================================================

// TC: TC-N130
// REQ: N-BOOT
void test_hub_tick_empty_list_never_sends() {
  Rig g;
  g.clock.r = readingAt(T0);
  for (uint32_t ms = 0; ms <= 9000; ms += 1000) g.hub.tick(ms);
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N131
// REQ: N-STATE, F2
void test_hub_initial_state() {
  Rig g;
  assertInitialAc(g.hub.acState(), "acState");
  TEST_ASSERT_FALSE(g.hub.lastClimate().valid);
}

// TC: TC-N132
// REQ: F5
void test_hub_climate_interval() {
  Rig g;
  g.hub.tick(0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.sensor.readCount, "tick(0)");
  g.hub.tick(kClimateIntervalMs - 1);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.sensor.readCount, "tick(29999)");
  g.hub.tick(kClimateIntervalMs);
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.sensor.readCount, "tick(30000)");
}

// TC: TC-N133
// REQ: F5
void test_hub_climate_interval_millis_wrap() {
  Rig g;
  g.hub.tick(0xFFFFFFF0u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.sensor.readCount, "tick(0xFFFFFFF0)");
  g.hub.tick(0x0000751Fu);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.sensor.readCount, "diff 29999");
  g.hub.tick(0x00007520u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.sensor.readCount, "diff 30000");
}

// TC: TC-N134
// REQ: F5
void test_hub_climate_read_failure_reflected() {
  Rig g;
  g.sensor.next = ClimateReading{true, 24.5f, 55.0f};
  g.hub.tick(0);
  TEST_ASSERT_TRUE_MESSAGE(g.hub.lastClimate().valid, "precondition");
  g.sensor.next = ClimateReading{false, 0.0f, 0.0f};
  g.hub.tick(30000);
  TEST_ASSERT_FALSE(g.hub.lastClimate().valid);
}

// TC: TC-N135
// REQ: F1
void test_hub_apply_while_on_sends_full_state() {
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "precondition");
  TEST_ASSERT_TRUE(g.apply(pTemp(27)));
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
  const AcState e = makeState(true, AcMode::Cool, 27, AcFan::Auto);
  assertAcStateEq(e, g.ir.lastAc, "lastAc");
  assertAcStateEq(g.hub.acState(), g.ir.lastAc, "lastAc == acState");
}

// TC: TC-N136
// REQ: F1
void test_hub_apply_out_of_range_rejected() {
  Rig g;
  std::string err;
  TEST_ASSERT_FALSE(g.apply(pTemp(cap::kTempMaxC + 1), &err));
  TEST_ASSERT_EQUAL_STRING("temp out of range", err.c_str());
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
  assertInitialAc(g.hub.acState(), "unchanged");
}

// TC: TC-N137
// REQ: F1
void test_hub_send_failure_keeps_state() {
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  g.ir.result = false;
  TEST_ASSERT_TRUE(g.apply(pTemp(27)));
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
  TEST_ASSERT_EQUAL_INT(27, g.hub.acState().tempC);
}

// TC: TC-N138
// REQ: F1
void test_hub_same_value_patch_while_on_sends() {
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  TEST_ASSERT_TRUE(g.apply(pMode(AcMode::Cool)));
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
}

// TC: TC-N139
// REQ: F1
void test_hub_switch_to_no_temp_mode() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  Rig g;
  AcPatch p;
  p.power = true;
  p.mode = mt;
  TEST_ASSERT_TRUE(g.apply(p));
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  ASSERT_ENUM(mt, g.ir.lastAc.mode);
  TEST_ASSERT_FALSE(g.ir.lastAc.hasTemp);
  TEST_ASSERT_TRUE(g.ir.lastAc.power);
}

// TC: TC-N140
// REQ: F1
void test_hub_validation_failure_no_send_on_and_off() {
  AcFan fx;
  if (!findFx(&fx)) TEST_IGNORE_MESSAGE("every AcFan is a choice (F1-VALUES)");
  AcMode mt;
  const bool hasMt = findMt(&mt);
  for (int on = 0; on < 2; ++on) {
    Rig g;
    if (on) TEST_ASSERT_TRUE(g.apply(pPower(true)));
    const int before = g.ir.acCount;
    const AcState s0 = g.hub.acState();
    const char* label = on ? "(b) on" : "(a) off";
    std::string err;
    TEST_ASSERT_FALSE_MESSAGE(g.apply(pFan(fx), &err), label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("fan not supported", err.c_str(), label);
    if (hasMt) {
      AcPatch p;
      p.mode = mt;
      p.tempC = 24;
      TEST_ASSERT_FALSE_MESSAGE(g.apply(p, &err), label);
      TEST_ASSERT_EQUAL_STRING_MESSAGE("temp not supported in this mode", err.c_str(), label);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(before, g.ir.acCount, label);
    assertAcStateEq(s0, g.hub.acState(), label);
  }
}

// TC: TC-N185
// REQ: F1
void test_hub_changes_while_off_not_sent() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pMode(AcMode::Heat)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "after mode");
  TEST_ASSERT_TRUE(g.apply(pTemp(27)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "after temp");
  TEST_ASSERT_TRUE(g.apply(pFan(f1)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "after fan");
  assertAcStateEq(makeState(false, AcMode::Heat, 27, f1), g.hub.acState(), "acState");
}

// TC: TC-N186
// REQ: F1
void test_hub_power_on_sends_changes_made_while_off() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pMode(AcMode::Heat)));
  TEST_ASSERT_TRUE(g.apply(pTemp(27)));
  TEST_ASSERT_TRUE(g.apply(pFan(f1)));
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  assertAcStateEq(makeState(true, AcMode::Heat, 27, f1), g.ir.lastAc, "lastAc");
  assertAcStateEq(g.hub.acState(), g.ir.lastAc, "lastAc == acState");
}

// TC: TC-N187
// REQ: F1, F2
void test_hub_changes_while_off_kept_per_mode() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pMode(AcMode::Heat)));
  TEST_ASSERT_TRUE(g.apply(pTemp(27)));
  TEST_ASSERT_TRUE(g.apply(pFan(f1)));
  TEST_ASSERT_TRUE(g.apply(pMode(AcMode::Cool)));
  ASSERT_ENUM_MSG(AcMode::Cool, g.hub.acState().mode, "4th");
  TEST_ASSERT_EQUAL_INT_MESSAGE(26, g.hub.acState().tempC, "4th");
  ASSERT_ENUM_MSG(AcFan::Auto, g.hub.acState().fan, "4th");
  TEST_ASSERT_TRUE(g.apply(pMode(AcMode::Heat)));
  ASSERT_ENUM_MSG(AcMode::Heat, g.hub.acState().mode, "5th");
  TEST_ASSERT_EQUAL_INT_MESSAGE(27, g.hub.acState().tempC, "5th");
  ASSERT_ENUM_MSG(f1, g.hub.acState().fan, "5th");
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N188
// REQ: F1
void test_hub_power_off_while_off_is_sent() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pPower(false)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "1st");
  assertAcStateEq(makeState(false, AcMode::Cool, 26, AcFan::Auto), g.ir.lastAc, "1st lastAc");
  AcPatch p;
  p.power = false;
  p.tempC = 24;
  TEST_ASSERT_TRUE(g.apply(p));
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.ir.acCount, "2nd");
  TEST_ASSERT_FALSE(g.ir.lastAc.power);
  TEST_ASSERT_EQUAL_INT(24, g.ir.lastAc.tempC);
}

// TC: TC-N189
// REQ: F1
void test_hub_changes_after_power_off_not_sent() {
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  TEST_ASSERT_TRUE(g.apply(pPower(false)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.ir.acCount, "power off sent");
  TEST_ASSERT_FALSE(g.ir.lastAc.power);
  TEST_ASSERT_TRUE(g.apply(pTemp(25)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.ir.acCount, "temp after off not sent");
  TEST_ASSERT_EQUAL_INT(25, g.hub.acState().tempC);
  TEST_ASSERT_EQUAL_INT(26, g.ir.lastAc.tempC);
}

// TC: TC-N190
// REQ: F1
void test_hub_out_of_range_while_on_not_sent() {
  Rig g;
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  std::string err;
  TEST_ASSERT_FALSE(g.apply(pTemp(cap::kTempMaxC + 1), &err));
  TEST_ASSERT_EQUAL_STRING("temp out of range", err.c_str());
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  assertAcStateEq(makeState(true, AcMode::Cool, 26, AcFan::Auto), g.hub.acState(), "unchanged");
}

// TC: TC-N191
// REQ: F1, F2
void test_hub_transition_table_send_counts() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_FAN(AcFan::High);
  REQUIRE_FAN(AcFan::Low);
  struct Row { AcPatch p; int sends; };
  std::vector<Row> rows;
  rows.push_back({pPower(true), 1});                                            // #1
  rows.push_back({pTemp(27), 1});                                               // #2
  rows.push_back({pMode(AcMode::Heat), 1});                                     // #3
  { AcPatch p; p.tempC = 22; p.fan = AcFan::High; rows.push_back({p, 1}); }     // #4
  rows.push_back({pMode(AcMode::Cool), 1});                                     // #5
  { AcPatch p; p.mode = AcMode::Heat; p.tempC = 18; rows.push_back({p, 1}); }   // #6
  rows.push_back({pTemp(cap::kTempMaxC + 1), 0});                               // #7
  { AcPatch p; p.mode = AcMode::Cool; p.tempC = 40; rows.push_back({p, 0}); }   // #8
  rows.push_back({pPower(false), 1});                                           // #9
  rows.push_back({pTemp(19), 0});                                               // #10
  { AcPatch p; p.mode = AcMode::Cool; p.fan = AcFan::Low; rows.push_back({p, 0}); }  // #11
  rows.push_back({pPower(false), 1});                                           // #12
  rows.push_back({pPower(true), 1});                                            // #13
  rows.push_back({AcPatch{}, 0});                                               // #14
  Rig g;
  for (size_t i = 0; i < rows.size(); ++i) {
    const int before = g.ir.acCount;
    g.apply(rows[i].p);
    char msg[16];
    snprintf(msg, sizeof msg, "row #%d", static_cast<int>(i + 1));
    TEST_ASSERT_EQUAL_INT_MESSAGE(rows[i].sends, g.ir.acCount - before, msg);
  }
  TEST_ASSERT_EQUAL_INT(9, g.ir.acCount);
  TEST_ASSERT_TRUE(g.ir.lastAc.power);
  ASSERT_ENUM(AcMode::Cool, g.ir.lastAc.mode);
  TEST_ASSERT_EQUAL_INT(27, g.ir.lastAc.tempC);
  ASSERT_ENUM(AcFan::Low, g.ir.lastAc.fan);
}

// TC: TC-N193
// REQ: F1
void test_hub_power_patch_validation_failure_while_off() {
  Rig g;
  AcPatch p;
  p.power = true;
  p.tempC = cap::kTempMaxC + 1;
  std::string err;
  TEST_ASSERT_FALSE(g.apply(p, &err));
  TEST_ASSERT_EQUAL_STRING("temp out of range", err.c_str());
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
  assertInitialAc(g.hub.acState(), "unchanged");
}

// TC-N194 の手順（TC-N195・N196 の前提でも使う）
static void offChangesCoolTempThenHeat(Rig& g, AcFan f1) {
  TEST_ASSERT_TRUE_MESSAGE(g.apply(pTemp(27)), "temp 27");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "after temp");
  TEST_ASSERT_TRUE_MESSAGE(g.apply(pMode(AcMode::Heat)), "mode heat");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "after mode");
  TEST_ASSERT_TRUE_MESSAGE(g.apply(pFan(f1)), "fan F1");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "after fan");
}

// TC: TC-N194
// REQ: F1, F2
void test_hub_off_changes_heat_restores_initial() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  offChangesCoolTempThenHeat(g, f1);
  assertAcStateEq(makeState(false, AcMode::Heat, initialSettings(AcMode::Heat).tempC, f1),
                  g.hub.acState(), "acState");
}

// TC: TC-N195
// REQ: F1, F2
void test_hub_off_changes_then_power_on() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  offChangesCoolTempThenHeat(g, f1);
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  TEST_ASSERT_EQUAL_INT(1, g.ir.acCount);
  assertAcStateEq(makeState(true, AcMode::Heat, initialSettings(AcMode::Heat).tempC, f1),
                  g.ir.lastAc, "lastAc");
  assertAcStateEq(g.hub.acState(), g.ir.lastAc, "lastAc == acState");
}

// TC: TC-N196
// REQ: F1, F2
void test_hub_off_changes_cool_temp_kept() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  offChangesCoolTempThenHeat(g, f1);
  TEST_ASSERT_TRUE(g.apply(pPower(true)));
  TEST_ASSERT_TRUE(g.apply(pMode(AcMode::Cool)));
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
  assertAcStateEq(makeState(true, AcMode::Cool, 27, AcFan::Auto), g.ir.lastAc, "lastAc");
}

// =============================================================================================
// C-2. ルーティング
// =============================================================================================

// TC: TC-N141
// REQ: API
void test_route_all_endpoints_ok() {
  Rig g;
  struct Req { HttpMethod m; const char* path; const char* body; };
  const Req reqs[] = {
      {HttpMethod::Get, "/api/status", ""},
      {HttpMethod::Post, "/api/ac", R"({"power":true})"},
      {HttpMethod::Get, "/api/schedules", ""},
      {HttpMethod::Put, "/api/schedules", R"({"schedules":[]})"},
      {HttpMethod::Get, "/api/schedules/export", ""},
      {HttpMethod::Post, "/api/schedules/import", R"({"version":1,"schedules":[]})"},
  };
  for (const Req& r : reqs) {
    const ApiResponse res = g.call(r.m, r.path, r.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, res.status, r.path);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("application/json", res.contentType.c_str(), r.path);
  }
}

// TC: TC-N142
// REQ: API
void test_route_unknown_path_404() {
  Rig g;
  for (const char* p : {"/api/foo", "/api/status/", "/favicon.ico"}) {
    assertError(g.call(HttpMethod::Get, p), 404, "not found", std::string("GET ") + p);
  }
}

// TC: TC-N143
// REQ: API
void test_route_wrong_method_405() {
  Rig g;
  assertError(g.call(HttpMethod::Post, "/api/status"), 405, "method not allowed", "POST status");
  assertError(g.call(HttpMethod::Get, "/api/ac"), 405, "method not allowed", "GET ac");
  assertError(g.call(HttpMethod::Other, "/api/schedules"), 405, "method not allowed",
              "Other schedules");
  assertError(g.call(HttpMethod::Get, "/api/schedules/import"), 405, "method not allowed",
              "GET import");
  assertError(g.call(HttpMethod::Post, "/api/schedules/export"), 405, "method not allowed",
              "POST export");
  assertError(g.call(HttpMethod::Other, "/api/foo"), 404, "not found", "Other foo");
}

// TC: TC-N144
// REQ: API
void test_route_errors_have_no_side_effects() {
  Rig g;
  g.call(HttpMethod::Get, "/api/foo");
  g.call(HttpMethod::Get, "/api/status/");
  g.call(HttpMethod::Get, "/favicon.ico");
  g.call(HttpMethod::Post, "/api/status");
  g.call(HttpMethod::Get, "/api/ac");
  g.call(HttpMethod::Other, "/api/schedules");
  g.call(HttpMethod::Get, "/api/schedules/import");
  g.call(HttpMethod::Post, "/api/schedules/export");
  g.call(HttpMethod::Other, "/api/foo");
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
  assertInitialAc(g.hub.acState(), "unchanged");
  TEST_ASSERT_EQUAL_INT(0, g.hub.schedules().size());
}

// TC: TC-N200
// REQ: API
void test_route_root_non_get_404() {
  Rig g;
  assertError(g.call(HttpMethod::Post, "/", "{}"), 404, "not found", "POST /");
  assertError(g.call(HttpMethod::Put, "/", "{}"), 404, "not found", "PUT /");
  assertError(g.call(HttpMethod::Other, "/", "{}"), 404, "not found", "Other /");
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// =============================================================================================
// C-3. GET /api/status
// =============================================================================================

// TC: TC-N145
// REQ: API, F2, N-TIME, F5, N-BOOT
void test_status_initial() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  const ApiResponse res = g.status();
  TEST_ASSERT_EQUAL_INT(200, res.status);
  JsonDocument doc;
  parseBody(res.body, doc, "status");
  assertKeys(doc.as<JsonObject>(), {"ac", "acCapabilities", "climate", "clock"}, "top");
  assertAcJson(doc["ac"].as<JsonObject>(), false, "cool", 26, "auto", "ac");
  JsonObject climate = doc["climate"].as<JsonObject>();
  assertKeys(climate, {"valid", "temperature", "humidity"}, "climate");
  TEST_ASSERT_TRUE(climate["valid"].is<bool>());
  TEST_ASSERT_FALSE(climate["valid"].as<bool>());
  TEST_ASSERT_TRUE(climate["temperature"].isNull());
  TEST_ASSERT_TRUE(climate["humidity"].isNull());
  JsonObject clock = doc["clock"].as<JsonObject>();
  assertKeys(clock, {"synced", "now"}, "clock");
  TEST_ASSERT_TRUE(clock["synced"].is<bool>());
  TEST_ASSERT_FALSE(clock["synced"].as<bool>());
  TEST_ASSERT_TRUE(clock["now"].isNull());
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N146
// REQ: API, F1
void test_status_capabilities_from_cap() {
  Rig g;
  JsonDocument doc;
  parseBody(g.status().body, doc, "status");
  JsonObject c = doc["acCapabilities"].as<JsonObject>();
  assertKeys(c, {"modes", "tempMin", "tempMax", "tempStep", "tempModes", "fan"}, "acCapabilities");
  assertStringArray(c["modes"].as<JsonArray>(), {"auto", "cool", "dry", "heat"}, "modes");
  TEST_ASSERT_EQUAL_INT(cap::kTempMinC, c["tempMin"].as<int>());
  TEST_ASSERT_EQUAL_INT(cap::kTempMaxC, c["tempMax"].as<int>());
  TEST_ASSERT_EQUAL_INT(cap::kTempStepC, c["tempStep"].as<int>());
  std::vector<std::string> tempModes;
  for (AcMode m : kAllModes) {
    if (cap::tempSupported(m)) tempModes.push_back(toString(m));
  }
  assertStringArray(c["tempModes"].as<JsonArray>(), tempModes, "tempModes");
  std::vector<std::string> fans;
  for (int i = 0; i < cap::kFanChoiceCount; ++i) fans.push_back(toString(cap::kFanChoices[i]));
  assertStringArray(c["fan"].as<JsonArray>(), fans, "fan");
}

// TC: TC-N147
// REQ: N-TIME
void test_status_clock_synced() {
  Rig g;
  g.clock.r = reading20260105();
  JsonDocument doc;
  parseBody(g.status().body, doc, "status");
  TEST_ASSERT_TRUE(doc["clock"]["synced"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("2026-01-05T07:03:09+09:00", doc["clock"]["now"].as<const char*>());
}

// TC: TC-N148
// REQ: F5
void test_status_climate_rounded() {
  Rig g;
  g.sensor.next = ClimateReading{true, 24.46f, 55.24f};
  g.hub.tick(0);
  JsonDocument doc;
  parseBody(g.status().body, doc, "status");
  TEST_ASSERT_TRUE(doc["climate"]["valid"].as<bool>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.5f, doc["climate"]["temperature"].as<float>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.2f, doc["climate"]["humidity"].as<float>());
}

// TC: TC-N149
// REQ: F5
void test_status_climate_failure_null() {
  Rig g;
  g.sensor.next = ClimateReading{true, 24.46f, 55.24f};
  g.hub.tick(0);
  g.sensor.next = ClimateReading{false, 0.0f, 0.0f};
  g.hub.tick(30000);
  JsonDocument doc;
  parseBody(g.status().body, doc, "status");
  JsonObject climate = doc["climate"].as<JsonObject>();
  TEST_ASSERT_FALSE(climate["valid"].as<bool>());
  TEST_ASSERT_TRUE(hasKey(climate, "temperature"));
  TEST_ASSERT_TRUE(climate["temperature"].isNull());
  TEST_ASSERT_TRUE(hasKey(climate, "humidity"));
  TEST_ASSERT_TRUE(climate["humidity"].isNull());
}

// TC: TC-N150
// REQ: F1
void test_status_no_temp_mode_temp_null() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  Rig g;
  const std::string body = std::string(R"({"power":true,"mode":")") + toString(mt) + "\"}";
  JsonDocument d1;
  readAcResponse(g.postAc(body), d1, body);
  JsonObject a1 = d1["ac"].as<JsonObject>();
  TEST_ASSERT_EQUAL_STRING(toString(mt), a1["mode"].as<const char*>());
  TEST_ASSERT_TRUE_MESSAGE(hasKey(a1, "temp"), "POST response has temp key");
  TEST_ASSERT_TRUE_MESSAGE(a1["temp"].isNull(), "POST response temp null");
  JsonDocument d2;
  parseBody(g.status().body, d2, "status");
  JsonObject a2 = d2["ac"].as<JsonObject>();
  TEST_ASSERT_EQUAL_STRING(toString(mt), a2["mode"].as<const char*>());
  TEST_ASSERT_TRUE_MESSAGE(hasKey(a2, "temp"), "status has temp key");
  TEST_ASSERT_TRUE_MESSAGE(a2["temp"].isNull(), "status temp null");
}

// TC: TC-N151
// REQ: API
void test_status_has_no_side_effects() {
  Rig g;
  for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_INT(200, g.status().status);
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
  assertInitialAc(g.hub.acState(), "unchanged");
}

// TC: TC-N199
// REQ: API, N-TIME, F4-IO
void test_status_now_matches_export_exported_at() {
  Rig g;
  g.clock.r = reading20260105();
  JsonDocument ds;
  parseBody(g.status().body, ds, "status");
  const ApiResponse ex = g.call(HttpMethod::Get, "/api/schedules/export");
  TEST_ASSERT_EQUAL_INT(200, ex.status);
  JsonDocument de;
  parseBody(ex.body, de, "export");
  TEST_ASSERT_EQUAL_STRING("2026-01-05T07:03:09+09:00", ds["clock"]["now"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(ds["clock"]["now"].as<const char*>(),
                           de["exportedAt"].as<const char*>());
}

// =============================================================================================
// C-4. POST /api/ac
// =============================================================================================

// TC: TC-N152
// REQ: F1, API
void test_ac_temp_while_on() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  TEST_ASSERT_EQUAL_INT(200, g.postAc(R"({"power":true})").status);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "precondition");
  JsonDocument doc;
  readAcResponse(g.postAc(R"({"temp":27})"), doc, "temp 27");
  assertAcJson(doc["ac"].as<JsonObject>(), true, "cool", 27, "auto", "ac");
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
  assertAcStateEq(g.hub.acState(), g.ir.lastAc, "lastAc == acState");
}

// TC: TC-N153
// REQ: F1
void test_ac_all_four_keys() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  JsonDocument doc;
  readAcResponse(g.postAc(R"({"power":true,"mode":"cool","temp":26,"fan":"auto"})"), doc, "4 keys");
  assertAcJson(doc["ac"].as<JsonObject>(), true, "cool", 26, "auto", "ac");
}

// TC: TC-N154
// REQ: F1, F2
void test_ac_mode_switch_restores_via_api() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  Rig g;
  TEST_ASSERT_EQUAL_INT(200, g.postAc(R"({"temp":27})").status);
  JsonDocument d1;
  readAcResponse(g.postAc(R"({"mode":"heat"})"), d1, "heat");
  TEST_ASSERT_EQUAL_STRING("heat", d1["ac"]["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(initialSettings(AcMode::Heat).tempC, d1["ac"]["temp"].as<int>());
  JsonDocument d2;
  readAcResponse(g.postAc(R"({"mode":"cool"})"), d2, "cool");
  TEST_ASSERT_EQUAL_STRING("cool", d2["ac"]["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(27, d2["ac"]["temp"].as<int>());
}

// TC: TC-N155
// REQ: F1, API
void test_ac_temp_must_be_integer() {
  Rig g;
  for (const char* b : {R"({"temp":26.5})", R"({"temp":"26"})", R"({"temp":null})",
                        R"({"temp":true})"}) {
    assertAcError(g, b, "temp: must be integer");
  }
}

// TC: TC-N156
// REQ: F1, API
void test_ac_power_and_mode_shape_errors() {
  Rig g;
  assertAcError(g, R"({"power":1})", "power: must be boolean");
  assertAcError(g, R"({"mode":3})", "mode: must be string");
  assertAcError(g, R"({"mode":"Cool"})", "mode: unknown mode");
  assertAcError(g, R"({"mode":"fan"})", "mode: unknown mode");
}

// TC: TC-N157
// REQ: F1, API
void test_ac_fan_shape_errors() {
  Rig g;
  assertAcError(g, R"({"fan":1})", "fan: must be string");
  assertAcError(g, R"({"fan":"turbo"})", "fan: unknown fan");
  assertAcError(g, R"({"fan":"quiet"})", "fan: unknown fan");
}

// TC: TC-N158
// REQ: F1, API
void test_ac_unknown_keys() {
  Rig g;
  assertAcError(g, R"({"timer":60})", "unknown key \"timer\"");
  assertAcError(g, R"({"power":true,"tmp":1})", "unknown key \"tmp\"");
}

// TC: TC-N159
// REQ: F1, API
void test_ac_value_errors() {
  Rig g;
  assertAcError(g, "{}", "no ac fields");
  assertAcError(g, "{\"temp\":" + std::to_string(cap::kTempMaxC + 1) + "}", "temp out of range");
  assertAcError(g, "{\"temp\":" + std::to_string(cap::kTempMinC - 1) + "}", "temp out of range");
  AcFan fx;
  if (findFx(&fx)) {
    assertAcError(g, std::string("{\"fan\":\"") + toString(fx) + "\"}", "fan not supported");
  }
}

// TC: TC-N160
// REQ: F1
void test_ac_errors_do_not_send_or_change() {
  Rig g;
  std::vector<std::string> bodies = {
      R"({"temp":26.5})", R"({"temp":"26"})", R"({"temp":null})", R"({"temp":true})",
      R"({"power":1})", R"({"mode":3})", R"({"mode":"Cool"})", R"({"mode":"fan"})",
      R"({"fan":1})", R"({"fan":"turbo"})", R"({"fan":"quiet"})",
      R"({"timer":60})", R"({"power":true,"tmp":1})",
      "{}",
      "{\"temp\":" + std::to_string(cap::kTempMaxC + 1) + "}",
      "{\"temp\":" + std::to_string(cap::kTempMinC - 1) + "}",
  };
  AcFan fx;
  if (findFx(&fx)) bodies.push_back(std::string("{\"fan\":\"") + toString(fx) + "\"}");
  for (const std::string& b : bodies) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, g.postAc(b).status, b.c_str());
  }
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
  assertInitialAc(g.hub.acState(), "unchanged");
}

// TC: TC-N161
// REQ: API
void test_ac_check_order() {
  Rig g;
  assertAcError(g, R"({"x":1,"temp":"a"})", "unknown key \"x\"");
  assertAcError(g, R"({"fan":"turbo","temp":"a"})", "temp: must be integer");
}

// TC: TC-N162
// REQ: API
void test_ac_invalid_json() {
  Rig g;
  for (const char* b : {"", "[1]", "\"x\"", "{\"temp\":"}) {
    assertAcError(g, b, "invalid json");
  }
}

// TC: TC-N163
// REQ: API
void test_ac_body_size_limit() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  TEST_ASSERT_EQUAL_INT(200, g.postAc(R"({"power":true})").status);
  const std::string tail = R"("temp":27})";
  const std::string atMax = "{" + std::string(kApiSmallBodyMaxBytes - 1 - tail.size(), ' ') + tail;
  const std::string over = "{" + std::string(kApiSmallBodyMaxBytes - tail.size(), ' ') + tail;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kApiSmallBodyMaxBytes), static_cast<int>(atMax.size()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kApiSmallBodyMaxBytes + 1), static_cast<int>(over.size()));
  JsonDocument doc;
  readAcResponse(g.postAc(atMax), doc, "512 bytes");
  TEST_ASSERT_EQUAL_INT(27, doc["ac"]["temp"].as<int>());
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.ir.acCount, "512 bytes sent");
  assertAcError(g, over, "body too large");
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, g.ir.acCount, "513 bytes not sent");
}

// TC: TC-N164
// REQ: F1
void test_ac_send_failure_still_200() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  TEST_ASSERT_EQUAL_INT(200, g.postAc(R"({"power":true})").status);
  g.ir.result = false;
  JsonDocument doc;
  readAcResponse(g.postAc(R"({"temp":27})"), doc, "temp 27");
  TEST_ASSERT_TRUE(doc["ac"]["power"].as<bool>());
  TEST_ASSERT_EQUAL_INT(27, doc["ac"]["temp"].as<int>());
  TEST_ASSERT_EQUAL_INT(2, g.ir.acCount);
}

// TC: TC-N192
// REQ: F1, API
void test_ac_change_while_off_then_power_on() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  JsonDocument d1;
  readAcResponse(g.postAc(R"({"temp":27})"), d1, "1st");
  TEST_ASSERT_FALSE(d1["ac"]["power"].as<bool>());
  TEST_ASSERT_EQUAL_INT(27, d1["ac"]["temp"].as<int>());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "1st not sent");
  JsonDocument d2;
  readAcResponse(g.postAc(R"({"power":true})"), d2, "2nd");
  TEST_ASSERT_TRUE(d2["ac"]["power"].as<bool>());
  TEST_ASSERT_EQUAL_INT(27, d2["ac"]["temp"].as<int>());
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "2nd sent");
  TEST_ASSERT_TRUE(g.ir.lastAc.power);
  TEST_ASSERT_EQUAL_INT(27, g.ir.lastAc.tempC);
}

// TC: TC-N218
// REQ: F1, API
void test_ac_no_temp_mode_rejects_temp() {
  AcMode mt;
  if (!findMt(&mt)) TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  Rig g;
  const std::string m = toString(mt);
  assertAcError(g, "{\"mode\":\"" + m + "\",\"temp\":24}", "temp not supported in this mode");
  JsonDocument ds;
  parseBody(g.status().body, ds, "status");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("cool", ds["ac"]["mode"].as<const char*>(), "(1) mode unchanged");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, g.ir.acCount, "(1)");
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, g.postAc("{\"power\":true,\"mode\":\"" + m + "\"}").status,
                                "(2)");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "(2)");
  assertAcError(g, R"({"temp":24})", "temp not supported in this mode");
  assertAcError(g, "{\"temp\":" + std::to_string(cap::kTempMaxC + 1) + "}",
                "temp not supported in this mode");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, g.ir.acCount, "(3)(4)");
}

// TC: TC-N219
// REQ: F1, API
void test_ac_every_fan_choice_accepted() {
  Rig g;
  TEST_ASSERT_EQUAL_INT(200, g.postAc(R"({"power":true})").status);
  for (int i = 0; i < cap::kFanChoiceCount; ++i) {
    const char* f = toString(cap::kFanChoices[i]);
    const int before = g.ir.acCount;
    JsonDocument doc;
    readAcResponse(g.postAc(std::string("{\"fan\":\"") + f + "\"}"), doc, f);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(f, doc["ac"]["fan"].as<const char*>(), f);
    TEST_ASSERT_EQUAL_INT_MESSAGE(before + 1, g.ir.acCount, f);
  }
}

// =============================================================================================
// C-5. 照明と風向を作っていないこと
// =============================================================================================

// TC: TC-N165
// REQ: F3, API
void test_light_path_is_404() {
  Rig g;
  assertError(g.call(HttpMethod::Post, "/api/light", R"({"button":"power"})"), 404, "not found",
              "POST /api/light");
  assertError(g.call(HttpMethod::Get, "/api/light"), 404, "not found", "GET /api/light");
  assertError(g.call(HttpMethod::Put, "/api/light", "{}"), 404, "not found", "PUT /api/light");
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
  assertInitialAc(g.hub.acState(), "unchanged");
}

// TC: TC-N166
// REQ: F3, F1, API
void test_ac_rejects_light_button_key() {
  Rig g;
  assertAcError(g, R"({"button":"power"})", "unknown key \"button\"");
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N167
// REQ: F1, API
void test_ac_rejects_swing_keys() {
  Rig g;
  assertAcError(g, R"({"swingV":"auto"})", "unknown key \"swingV\"");
  assertAcError(g, R"({"swingV":"off"})", "unknown key \"swingV\"");
  assertAcError(g, R"({"swingH":"off"})", "unknown key \"swingH\"");
  assertAcError(g, R"({"power":true,"swingV":"off"})", "unknown key \"swingV\"");
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
  TEST_ASSERT_FALSE(g.hub.acState().power);
}

// TC: TC-N168
// REQ: F1, API
void test_ac_response_has_no_swing_keys() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  Rig g;
  JsonDocument d1;
  readAcResponse(g.postAc(R"({"power":true})"), d1, "power");
  assertKeys(d1["ac"].as<JsonObject>(), {"power", "mode", "temp", "fan"}, "1st ac keys");
  JsonDocument d2;
  readAcResponse(g.postAc(R"({"temp":27})"), d2, "temp");
  assertKeys(d2["ac"].as<JsonObject>(), {"power", "mode", "temp", "fan"}, "2nd ac keys");
  ASSERT_ENUM(AcSwingV::Off, g.ir.lastAc.swingV);
  ASSERT_ENUM(AcSwingH::Off, g.ir.lastAc.swingH);
}

// =============================================================================================
// C-6. スケジュール API
// =============================================================================================

// TC: TC-N169
// REQ: F4, F4-LIMIT
void test_schedules_initial_empty() {
  Rig g;
  const ApiResponse res = g.call(HttpMethod::Get, "/api/schedules");
  TEST_ASSERT_EQUAL_INT(200, res.status);
  JsonDocument doc;
  parseBody(res.body, doc, "GET");
  TEST_ASSERT_EQUAL_INT(kScheduleMax, doc["max"].as<int>());
  TEST_ASSERT_TRUE(doc["schedules"].is<JsonArray>());
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(doc["schedules"].as<JsonArray>().size()));
}

// TC: TC-N170
// REQ: F4
void test_schedules_put_numbers_ids() {
  Rig g;
  const ApiResponse res = putSchedules(g, listBody({kValidItem, kValidItem}));
  TEST_ASSERT_EQUAL_INT(200, res.status);
  JsonDocument doc;
  parseBody(res.body, doc, "PUT");
  JsonArray a = doc["schedules"].as<JsonArray>();
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(a.size()));
  TEST_ASSERT_EQUAL_INT(1, a[0]["id"].as<int>());
  TEST_ASSERT_EQUAL_INT(2, a[1]["id"].as<int>());
  std::string putArray;
  serializeJson(doc["schedules"], putArray);
  TEST_ASSERT_EQUAL_STRING(putArray.c_str(), scheduleArray(g).c_str());
}

// TC: TC-N171
// REQ: F4-LIMIT
void test_schedules_put_too_many() {
  Rig g;
  putTwo(g);
  std::vector<std::string> items(kScheduleMax + 1, kValidItem);
  char msg[48];
  snprintf(msg, sizeof msg, "too many schedules (max %d)", kScheduleMax);
  assertError(putSchedules(g, listBody(items)), 400, msg, "PUT max+1");
  assertScheduleIds(g, {1, 2}, "unchanged");
}

// TC: TC-N172
// REQ: F4
void test_schedules_put_bad_time() {
  Rig g;
  putTwo(g);
  const std::string before = scheduleArray(g);
  const std::string bad =
      R"({"enabled":true,"time":"24:00","days":["mon"],"target":"ac","action":{"power":true}})";
  assertError(putSchedules(g, listBody({bad, kValidItem})), 400,
              "schedules[0].time: must be HH:MM", "PUT 24:00");
  TEST_ASSERT_EQUAL_STRING(before.c_str(), scheduleArray(g).c_str());
}

// TC: TC-N173
// REQ: F4
void test_schedules_put_duplicate_id() {
  Rig g;
  putTwo(g);
  const std::string before = scheduleArray(g);
  assertError(putSchedules(g, listBody({itemWithId(1), itemWithId(1)})), 400,
              "schedules[1]: duplicate id", "PUT dup");
  TEST_ASSERT_EQUAL_STRING(before.c_str(), scheduleArray(g).c_str());
}

// TC-N174 の前提：例の3件を PUT し、その後に呼ぶたびに1秒進む時計にする
static void putExamplesThenTickingClock(Rig& g, AcFan f1) {
  const ApiResponse res = putSchedules(g, listBody(exampleItems(f1)));
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, res.status, "precondition: PUT examples");
  g.clock.setSeq({readingAt(T20 + 59), readingAt(T20 + 60)});
}

// TC: TC-N174
// REQ: F4-IO
void test_schedules_export_single_clock_reading() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  putExamplesThenTickingClock(g, f1);
  const ApiResponse res = g.call(HttpMethod::Get, "/api/schedules/export");
  TEST_ASSERT_EQUAL_INT(200, res.status);
  TEST_ASSERT_EQUAL_STRING("application/json", res.contentType.c_str());
  JsonDocument doc;
  parseBody(res.body, doc, "export");
  TEST_ASSERT_EQUAL_INT(kScheduleExportVersion, doc["version"].as<int>());
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(doc["schedules"].as<JsonArray>().size()));
  TEST_ASSERT_EQUAL_STRING("irhub-schedules-20260925-2000.json", res.downloadFilename.c_str());
  TEST_ASSERT_EQUAL_STRING("2026-09-25T20:00:59+09:00", doc["exportedAt"].as<const char*>());
}

// TC: TC-N175
// REQ: F4-IO, N-TIME
void test_schedules_export_unsynced() {
  Rig g;
  const ApiResponse res = g.call(HttpMethod::Get, "/api/schedules/export");
  TEST_ASSERT_EQUAL_INT(200, res.status);
  TEST_ASSERT_EQUAL_STRING("irhub-schedules.json", res.downloadFilename.c_str());
  JsonDocument doc;
  parseBody(res.body, doc, "export");
  TEST_ASSERT_TRUE_MESSAGE(hasKey(doc.as<JsonObject>(), "exportedAt"), "key exists");
  TEST_ASSERT_TRUE_MESSAGE(doc["exportedAt"].isNull(), "value is null");
}

// TC: TC-N176
// REQ: F4-IO
void test_schedules_export_import_round_trip() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  Rig g;
  putExamplesThenTickingClock(g, f1);
  const std::string original = scheduleArray(g);
  const ApiResponse ex = g.call(HttpMethod::Get, "/api/schedules/export");
  TEST_ASSERT_EQUAL_INT(200, ex.status);
  TEST_ASSERT_EQUAL_INT(200, putSchedules(g, R"({"schedules":[]})").status);
  const ApiResponse im = importSchedules(g, ex.body);
  TEST_ASSERT_EQUAL_INT(200, im.status);
  JsonDocument doc;
  parseBody(im.body, doc, "import");
  TEST_ASSERT_EQUAL_INT(kScheduleMax, doc["max"].as<int>());
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(doc["schedules"].as<JsonArray>().size()));
  assertScheduleIds(g, {1, 2, 3}, "ids after import");
  TEST_ASSERT_EQUAL_STRING(original.c_str(), scheduleArray(g).c_str());
}

// TC: TC-N177
// REQ: F4-IO
void test_schedules_import_version_errors() {
  Rig g;
  putTwo(g);
  const std::string before = scheduleArray(g);
  assertError(importSchedules(g, R"({"schedules":[]})"), 400, "version missing", "no version");
  assertError(importSchedules(g, R"({"version":2,"schedules":[]})"), 400, "unsupported version",
              "version 2");
  TEST_ASSERT_EQUAL_STRING(before.c_str(), scheduleArray(g).c_str());
}

// TC: TC-N178
// REQ: F4-IO
void test_schedules_import_replaces_not_merges() {
  Rig g;
  putTwo(g);
  TEST_ASSERT_EQUAL_INT(200, importSchedules(g, exportBody({itemWithId(5)})).status);
  assertScheduleIds(g, {5}, "only id 5");
}

// TC: TC-N179
// REQ: N-TIME, F4
void test_schedules_api_accepts_before_ntp() {
  Rig g;
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, g.call(HttpMethod::Get, "/api/schedules").status, "GET");
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, putSchedules(g, listBody({kValidItem, kValidItem})).status,
                                "PUT");
  const ApiResponse ex = g.call(HttpMethod::Get, "/api/schedules/export");
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, ex.status, "export");
  TEST_ASSERT_EQUAL_INT_MESSAGE(200, importSchedules(g, ex.body).status, "import");
}

// TC: TC-N180
// REQ: F4, N-BOOT
void test_schedules_api_never_sends() {
  Rig g;
  g.clock.r = readingAt(T20);
  AcFan f1 = AcFan::Auto;
  const bool hasF1 = findF1(&f1);
  g.call(HttpMethod::Get, "/api/schedules");
  putSchedules(g, listBody({kValidItem, kValidItem}));
  putSchedules(g, listBody(std::vector<std::string>(kScheduleMax + 1, kValidItem)));
  putSchedules(g, listBody({itemWithId(1), itemWithId(1)}));
  if (hasF1) putSchedules(g, listBody(exampleItems(f1)));
  const ApiResponse ex = g.call(HttpMethod::Get, "/api/schedules/export");
  putSchedules(g, R"({"schedules":[]})");
  importSchedules(g, ex.body);
  importSchedules(g, R"({"schedules":[]})");
  importSchedules(g, R"({"version":2,"schedules":[]})");
  importSchedules(g, exportBody({itemWithId(5)}));
  g.call(HttpMethod::Get, "/api/schedules");
  TEST_ASSERT_EQUAL_INT(0, g.ir.acCount);
}

// TC: TC-N184
// REQ: F4, F4-IO, API
void test_schedules_put_shape_errors_keep_list() {
  Rig g;
  putTwo(g);
  const std::string before = scheduleArray(g);
  {
    const std::string a = "{\"id\":10000000000," + kValidItem.substr(1);
    const ApiResponse res = putSchedules(g, listBody({a, kValidItem}));
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, res.status, "(a)");
    JsonDocument doc;
    parseBody(res.body, doc, "(a)");
    const std::string e = doc["error"].as<const char*>();
    TEST_ASSERT_TRUE_MESSAGE(e == "schedules[0].id: out of range" ||
                                 e == "schedules[0].id: must be integer",
                             e.c_str());
    TEST_MESSAGE(("(a) error: " + e).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), scheduleArray(g).c_str(), "(a) unchanged");
  }
  {
    const std::string b =
        R"({"enabled":true,"time":"07:00","days":["mon"],"target":"ac","action":{"mode":"heat"}})";
    assertError(putSchedules(g, listBody({b, kValidItem})), 400,
                "schedules[0].action.power: missing", "(b)");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), scheduleArray(g).c_str(), "(b) unchanged");
  }
  {
    const std::string c = "{\"note\":\"x\"," + kValidItem.substr(1);
    assertError(putSchedules(g, listBody({c, kValidItem})), 400,
                "schedules[0]: unknown key \"note\"", "(c)");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), scheduleArray(g).c_str(), "(c) unchanged");
  }
}

// TC: TC-N201
// REQ: F4-IO, API
void test_schedules_import_duplicate_id() {
  Rig g;
  putTwo(g);
  const std::string before = scheduleArray(g);
  assertError(importSchedules(g, exportBody({itemWithId(3), itemWithId(3)})), 400,
              "schedules[1]: duplicate id", "import dup");
  TEST_ASSERT_EQUAL_STRING(before.c_str(), scheduleArray(g).c_str());
}

// TC: TC-N220
// REQ: F4, F3, API
void test_schedules_put_rejects_light_and_swing() {
  Rig g;
  putTwo(g);
  const std::string before = scheduleArray(g);
  const std::string a =
      R"({"enabled":true,"time":"07:00","days":["mon"],"target":"light","action":{"power":true}})";
  assertError(putSchedules(g, listBody({a, kValidItem})), 400,
              "schedules[0].target: unknown target", "(a) light");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), scheduleArray(g).c_str(), "(a) unchanged");
  const std::string b =
      R"({"enabled":true,"time":"07:00","days":["mon"],"target":"ac","action":{"power":true,"swingV":"off"}})";
  assertError(putSchedules(g, listBody({b, kValidItem})), 400,
              "schedules[0].action: unknown key \"swingV\"", "(b) swingV");
  TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), scheduleArray(g).c_str(), "(b) unchanged");
}

// =============================================================================================
// C-7. エラー本文と認証
// =============================================================================================

// TC: TC-N181
// REQ: API, N-AUTH
void test_error_bodies_are_single_key_json() {
  Rig g;
  assertError(g.postAc(R"({"swingV":"auto"})"), 400, "unknown key \"swingV\"", "400");
  assertError(g.call(HttpMethod::Get, "/api/foo"), 404, "not found", "404");
  assertError(g.call(HttpMethod::Post, "/api/status"), 405, "method not allowed", "405");
  // ApiRequest は method・path・body だけ（認証の情報を持たない）で 200 になる
  ApiRequest r{HttpMethod::Get, "/api/status", ""};
  TEST_ASSERT_EQUAL_INT(200, g.api.handle(r).status);
}

int main(int, char**) {
  UNITY_BEGIN();
  // C-1
  RUN_TEST(test_hub_tick_empty_list_never_sends);
  RUN_TEST(test_hub_initial_state);
  RUN_TEST(test_hub_climate_interval);
  RUN_TEST(test_hub_climate_interval_millis_wrap);
  RUN_TEST(test_hub_climate_read_failure_reflected);
  RUN_TEST(test_hub_apply_while_on_sends_full_state);
  RUN_TEST(test_hub_apply_out_of_range_rejected);
  RUN_TEST(test_hub_send_failure_keeps_state);
  RUN_TEST(test_hub_same_value_patch_while_on_sends);
  RUN_TEST(test_hub_switch_to_no_temp_mode);
  RUN_TEST(test_hub_validation_failure_no_send_on_and_off);
  RUN_TEST(test_hub_changes_while_off_not_sent);
  RUN_TEST(test_hub_power_on_sends_changes_made_while_off);
  RUN_TEST(test_hub_changes_while_off_kept_per_mode);
  RUN_TEST(test_hub_power_off_while_off_is_sent);
  RUN_TEST(test_hub_changes_after_power_off_not_sent);
  RUN_TEST(test_hub_out_of_range_while_on_not_sent);
  RUN_TEST(test_hub_transition_table_send_counts);
  RUN_TEST(test_hub_power_patch_validation_failure_while_off);
  RUN_TEST(test_hub_off_changes_heat_restores_initial);
  RUN_TEST(test_hub_off_changes_then_power_on);
  RUN_TEST(test_hub_off_changes_cool_temp_kept);
  // C-2
  RUN_TEST(test_route_all_endpoints_ok);
  RUN_TEST(test_route_unknown_path_404);
  RUN_TEST(test_route_wrong_method_405);
  RUN_TEST(test_route_errors_have_no_side_effects);
  RUN_TEST(test_route_root_non_get_404);
  // C-3
  RUN_TEST(test_status_initial);
  RUN_TEST(test_status_capabilities_from_cap);
  RUN_TEST(test_status_clock_synced);
  RUN_TEST(test_status_climate_rounded);
  RUN_TEST(test_status_climate_failure_null);
  RUN_TEST(test_status_no_temp_mode_temp_null);
  RUN_TEST(test_status_has_no_side_effects);
  RUN_TEST(test_status_now_matches_export_exported_at);
  // C-4
  RUN_TEST(test_ac_temp_while_on);
  RUN_TEST(test_ac_all_four_keys);
  RUN_TEST(test_ac_mode_switch_restores_via_api);
  RUN_TEST(test_ac_temp_must_be_integer);
  RUN_TEST(test_ac_power_and_mode_shape_errors);
  RUN_TEST(test_ac_fan_shape_errors);
  RUN_TEST(test_ac_unknown_keys);
  RUN_TEST(test_ac_value_errors);
  RUN_TEST(test_ac_errors_do_not_send_or_change);
  RUN_TEST(test_ac_check_order);
  RUN_TEST(test_ac_invalid_json);
  RUN_TEST(test_ac_body_size_limit);
  RUN_TEST(test_ac_send_failure_still_200);
  RUN_TEST(test_ac_change_while_off_then_power_on);
  RUN_TEST(test_ac_no_temp_mode_rejects_temp);
  RUN_TEST(test_ac_every_fan_choice_accepted);
  // C-5
  RUN_TEST(test_light_path_is_404);
  RUN_TEST(test_ac_rejects_light_button_key);
  RUN_TEST(test_ac_rejects_swing_keys);
  RUN_TEST(test_ac_response_has_no_swing_keys);
  // C-6
  RUN_TEST(test_schedules_initial_empty);
  RUN_TEST(test_schedules_put_numbers_ids);
  RUN_TEST(test_schedules_put_too_many);
  RUN_TEST(test_schedules_put_bad_time);
  RUN_TEST(test_schedules_put_duplicate_id);
  RUN_TEST(test_schedules_export_single_clock_reading);
  RUN_TEST(test_schedules_export_unsynced);
  RUN_TEST(test_schedules_export_import_round_trip);
  RUN_TEST(test_schedules_import_version_errors);
  RUN_TEST(test_schedules_import_replaces_not_merges);
  RUN_TEST(test_schedules_api_accepts_before_ntp);
  RUN_TEST(test_schedules_api_never_sends);
  RUN_TEST(test_schedules_put_shape_errors_keep_list);
  RUN_TEST(test_schedules_import_duplicate_id);
  RUN_TEST(test_schedules_put_rejects_light_and_swing);
  // C-7
  RUN_TEST(test_error_bodies_are_single_key_json);
  return UNITY_END();
}
