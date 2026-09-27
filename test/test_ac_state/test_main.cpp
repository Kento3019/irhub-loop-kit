// test_ac_state：エアコン状態モデル（AcModel）、cap::、文字列変換の単体テスト
// 根拠：docs/design/02-ac-state.md（D-02）、docs/test/test-plan.md「A. test/test_ac_state」
//       （TC-N01〜TC-N34、TC-N202〜TC-N204）
// F1-VALUES の値（温度範囲・モード別の温度指定可否・風量と風向の選択肢）は直書きせず cap:: から作る。
// F2 の初期値（D2 で決定、2026-09-27：冷房 26・暖房 22・除湿 26、風量はすべて Auto）は、
// 冷房 26℃・風量 Auto を除き initialSettings() から取る。確定値そのものは TC-N02 で1度だけ
// 直書きで確かめる（テスト計画 方針2）。
#include <unity.h>

#include <optional>

#include "ac_capabilities.h"
#include "ac_state.h"

using namespace irhub;

// ---- 補助 ---------------------------------------------------------------------------

#define ASSERT_ENUM(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))
#define ASSERT_ENUM_MSG(expected, actual, msg) \
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), msg)

// 温度付きでそのモードを使うケースの前提（方針2）。false なら IGNORE
#define REQUIRE_TEMP_MODE(m)                                                   \
  do {                                                                         \
    if (!cap::tempSupported(m)) {                                              \
      TEST_IGNORE_MESSAGE("mode does not support temp (F1-VALUES)");           \
    }                                                                          \
  } while (0)
// 風量を直書きで使うケースの前提（方針2）。false なら IGNORE
#define REQUIRE_FAN(f)                                                         \
  do {                                                                         \
    if (!cap::fanSupported(f)) {                                               \
      TEST_IGNORE_MESSAGE("fan is not a choice (F1-VALUES)");                  \
    }                                                                          \
  } while (0)

static const AcMode kAllModes[kAcModeCount] = {AcMode::Auto, AcMode::Cool, AcMode::Dry,
                                               AcMode::Heat};
// 列挙子の数。D-02 1節の enum の定義と合わせる。
// 列挙子を増やしたときは、ここと下の find* の探索範囲を一緒に直す。
static const int kAcFanCount = 6;     // AcFan    { Auto, Min, Low, Medium, High, Max }
static const int kAcSwingVCount = 7;  // AcSwingV { Off, Auto, Highest, High, Middle, Low, Lowest }
static const int kAcSwingHCount = 8;  // AcSwingH { Off, Auto, LeftMax, Left, Middle, Right, RightMax, Wide }

static AcPatch patchPower(bool v) { AcPatch p; p.power = v; return p; }
static AcPatch patchMode(AcMode v) { AcPatch p; p.mode = v; return p; }
static AcPatch patchTemp(int v) { AcPatch p; p.tempC = v; return p; }
static AcPatch patchFan(AcFan v) { AcPatch p; p.fan = v; return p; }
static AcPatch patchSwingV(AcSwingV v) { AcPatch p; p.swingV = v; return p; }
static AcPatch patchSwingH(AcSwingH v) { AcPatch p; p.swingH = v; return p; }

// ---- 方針2の表の名前を求める（find*）。見つからなければ false ----------------------------
// F1：cap::kFanChoices のうち Auto でない最初の値
static bool findF1(AcFan* out) {
  for (int i = 0; i < cap::kFanChoiceCount; ++i) {
    if (cap::kFanChoices[i] != AcFan::Auto) { *out = cap::kFanChoices[i]; return true; }
  }
  return false;
}
// Fx：AcFan の列挙の順に見て fanSupported が false の最初の値
static bool findFx(AcFan* out) {
  for (int i = 0; i < kAcFanCount; ++i) {
    AcFan f = static_cast<AcFan>(i);
    if (!cap::fanSupported(f)) { *out = f; return true; }
  }
  return false;
}
// Mt：AcMode の列挙の順に見て tempSupported が false の最初のモード
static bool findMt(AcMode* out) {
  for (int i = 0; i < kAcModeCount; ++i) {
    if (!cap::tempSupported(kAllModes[i])) { *out = kAllModes[i]; return true; }
  }
  return false;
}
// V1：cap::kSwingVChoices のうち kSwingVDefault と違う値
static bool findV1(AcSwingV* out) {
  for (int i = 0; i < cap::kSwingVChoiceCount; ++i) {
    if (cap::kSwingVChoices[i] != cap::kSwingVDefault) { *out = cap::kSwingVChoices[i]; return true; }
  }
  return false;
}
// H1：cap::kSwingHChoices のうち kSwingHDefault と違う値
static bool findH1(AcSwingH* out) {
  for (int i = 0; i < cap::kSwingHChoiceCount; ++i) {
    if (cap::kSwingHChoices[i] != cap::kSwingHDefault) { *out = cap::kSwingHChoices[i]; return true; }
  }
  return false;
}
// Vx：AcSwingV の列挙の順に swingVSupported が false の最初
static bool findVx(AcSwingV* out) {
  for (int i = 0; i < kAcSwingVCount; ++i) {
    AcSwingV v = static_cast<AcSwingV>(i);
    if (!cap::swingVSupported(v)) { *out = v; return true; }
  }
  return false;
}
// Hx：AcSwingH の列挙の順に swingHSupported が false の最初
static bool findHx(AcSwingH* out) {
  for (int i = 0; i < kAcSwingHCount; ++i) {
    AcSwingH h = static_cast<AcSwingH>(i);
    if (!cap::swingHSupported(h)) { *out = h; return true; }
  }
  return false;
}

static void assertStateEq(const AcState& e, const AcState& a, const char* msg) {
  TEST_ASSERT_EQUAL_MESSAGE(e.power, a.power, msg);
  ASSERT_ENUM_MSG(e.mode, a.mode, msg);
  TEST_ASSERT_EQUAL_MESSAGE(e.hasTemp, a.hasTemp, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(e.tempC, a.tempC, msg);
  ASSERT_ENUM_MSG(e.fan, a.fan, msg);
  ASSERT_ENUM_MSG(e.swingV, a.swingV, msg);
  ASSERT_ENUM_MSG(e.swingH, a.swingH, msg);
}

static void assertSettingsEq(const AcSettings& e, const AcSettings& a, const char* msg) {
  TEST_ASSERT_EQUAL_INT_MESSAGE(e.tempC, a.tempC, msg);
  ASSERT_ENUM_MSG(e.fan, a.fan, msg);
  ASSERT_ENUM_MSG(e.swingV, a.swingV, msg);
  ASSERT_ENUM_MSG(e.swingH, a.swingH, msg);
}

// TC-N01 の初期状態（冷房 26℃・風量 Auto は F2 の決定値）
static AcState initialState() {
  AcState s;
  s.power = false;
  s.mode = AcMode::Cool;
  s.hasTemp = cap::tempSupported(AcMode::Cool);
  s.tempC = 26;
  s.fan = AcFan::Auto;
  s.swingV = cap::kSwingVDefault;
  s.swingH = cap::kSwingHDefault;
  return s;
}

void setUp() {}
void tearDown() {}

// ---- 初期状態 -----------------------------------------------------------------------

// TC: TC-N01
// REQ: F2, N-STATE
void test_initial_state() {
  AcModel m;
  const AcState& s = m.state();
  TEST_ASSERT_FALSE(s.power);
  ASSERT_ENUM(AcMode::Cool, s.mode);
  TEST_ASSERT_EQUAL(cap::tempSupported(AcMode::Cool), s.hasTemp);
  TEST_ASSERT_EQUAL_INT(26, s.tempC);
  ASSERT_ENUM(AcFan::Auto, s.fan);
  ASSERT_ENUM(cap::kSwingVDefault, s.swingV);
  ASSERT_ENUM(cap::kSwingHDefault, s.swingH);
}

// TC: TC-N02
// REQ: F2
void test_initial_settings_per_mode() {
  AcModel m;
  for (int i = 0; i < kAcModeCount; ++i) {
    assertSettingsEq(initialSettings(kAllModes[i]), m.settingsFor(kAllModes[i]),
                     "settingsFor == initialSettings");
  }
  // F2 の確定値（D2 決定）を initialSettings に対して1度だけ直書きで確かめる。
  // 自動の tempC は温度指定なしで使わない値なので確かめない。
  TEST_ASSERT_EQUAL_INT_MESSAGE(26, initialSettings(AcMode::Cool).tempC, "Cool tempC");
  ASSERT_ENUM_MSG(AcFan::Auto, initialSettings(AcMode::Cool).fan, "Cool fan");
  TEST_ASSERT_EQUAL_INT_MESSAGE(22, initialSettings(AcMode::Heat).tempC, "Heat tempC");
  ASSERT_ENUM_MSG(AcFan::Auto, initialSettings(AcMode::Heat).fan, "Heat fan");
  TEST_ASSERT_EQUAL_INT_MESSAGE(26, initialSettings(AcMode::Dry).tempC, "Dry tempC");
  ASSERT_ENUM_MSG(AcFan::Auto, initialSettings(AcMode::Dry).fan, "Dry fan");
  ASSERT_ENUM_MSG(AcFan::Auto, initialSettings(AcMode::Auto).fan, "Auto fan");
}

// ---- 部分更新（1項目だけ） ------------------------------------------------------------

// TC: TC-N03
// REQ: F1
void test_apply_temp_only() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchTemp(27)));
  AcState e = initialState();
  e.tempC = 27;
  assertStateEq(e, m.state(), "state after {temp:27}");
  TEST_ASSERT_EQUAL_INT(27, m.settingsFor(AcMode::Cool).tempC);
}

// TC: TC-N04
// REQ: F1
void test_apply_power_only() {
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchPower(true)));
  AcState e = initialState();
  e.power = true;
  assertStateEq(e, m.state(), "state after {power:true}");
}

// TC: TC-N05
// REQ: F1
void test_apply_fan_only() {
  AcFan f1;
  if (!findF1(&f1)) {
    TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchFan(f1)));
  AcState e = initialState();
  e.fan = f1;
  assertStateEq(e, m.state(), "state after {fan:F1}");
  ASSERT_ENUM(f1, m.settingsFor(AcMode::Cool).fan);
}

// TC: TC-N06
// REQ: F1
void test_apply_swingv_only() {
  AcSwingV v1;
  if (!findV1(&v1)) {
    TEST_IGNORE_MESSAGE("no swingV choice other than kSwingVDefault (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchSwingV(v1)));
  AcState e = initialState();
  e.swingV = v1;
  assertStateEq(e, m.state(), "state after {swingV:V1}");
}

// TC: TC-N07
// REQ: F1
void test_apply_swingh_only() {
  AcSwingH h1;
  if (!findH1(&h1)) {
    TEST_IGNORE_MESSAGE("no swingH choice other than kSwingHDefault (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchSwingH(h1)));
  AcState e = initialState();
  e.swingH = h1;
  assertStateEq(e, m.state(), "state after {swingH:H1}");
}

// ---- 遷移例の表（D-02 4節） ----------------------------------------------------------

struct TransitionRow {
  const char* label;
  AcPatch patch;
  AcError result;
  bool power;
  AcMode mode;
  int tempC;
  AcFan fan;
};

// TC: TC-N08
// REQ: F1, F2
void test_transition_table() {
  REQUIRE_FAN(AcFan::High);
  REQUIRE_FAN(AcFan::Low);
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcModel m;

  const int heatInit = initialSettings(AcMode::Heat).tempC;  // F2（D2 決定）で 22

  AcPatch r4; r4.tempC = 22; r4.fan = AcFan::High;
  AcPatch r6; r6.mode = AcMode::Heat; r6.tempC = 18;
  AcPatch r8; r8.mode = AcMode::Cool; r8.tempC = 40;
  AcPatch r11; r11.mode = AcMode::Cool; r11.fan = AcFan::Low;

  const TransitionRow rows[] = {
    {"#1 {power:true}", patchPower(true), AcError::None, true, AcMode::Cool, 26, AcFan::Auto},
    {"#2 {temp:27}", patchTemp(27), AcError::None, true, AcMode::Cool, 27, AcFan::Auto},
    {"#3 {mode:heat}", patchMode(AcMode::Heat), AcError::None, true, AcMode::Heat, heatInit, AcFan::Auto},
    {"#4 {temp:22,fan:high}", r4, AcError::None, true, AcMode::Heat, 22, AcFan::High},
    {"#5 {mode:cool}", patchMode(AcMode::Cool), AcError::None, true, AcMode::Cool, 27, AcFan::Auto},
    {"#6 {mode:heat,temp:18}", r6, AcError::None, true, AcMode::Heat, 18, AcFan::High},
    {"#7 {temp:max+1}", patchTemp(cap::kTempMaxC + 1), AcError::TempOutOfRange, true, AcMode::Heat, 18, AcFan::High},
    {"#8 {mode:cool,temp:40}", r8, AcError::TempOutOfRange, true, AcMode::Heat, 18, AcFan::High},
    {"#9 {power:false}", patchPower(false), AcError::None, false, AcMode::Heat, 18, AcFan::High},
    {"#10 {temp:19}", patchTemp(19), AcError::None, false, AcMode::Heat, 19, AcFan::High},
    {"#11 {mode:cool,fan:low}", r11, AcError::None, false, AcMode::Cool, 27, AcFan::Low},
    {"#12 {power:false}", patchPower(false), AcError::None, false, AcMode::Cool, 27, AcFan::Low},
    {"#13 {power:true}", patchPower(true), AcError::None, true, AcMode::Cool, 27, AcFan::Low},
    {"#14 {}", AcPatch{}, AcError::EmptyPatch, true, AcMode::Cool, 27, AcFan::Low},
  };

  // モード別の記憶の期待値（表の「perMode_ の変化」欄を反映していく）
  AcSettings exp[kAcModeCount];
  for (int i = 0; i < kAcModeCount; ++i) exp[i] = initialSettings(kAllModes[i]);

  const int cool = static_cast<int>(AcMode::Cool);
  const int heat = static_cast<int>(AcMode::Heat);
  const int n = sizeof(rows) / sizeof(rows[0]);
  TEST_ASSERT_EQUAL_INT(14, n);

  for (int i = 0; i < n; ++i) {
    const TransitionRow& r = rows[i];
    switch (i) {  // 行ごとの perMode_ の変化
      case 1: exp[cool].tempC = 27; break;
      case 3: exp[heat].tempC = 22; exp[heat].fan = AcFan::High; break;
      case 5: exp[heat].tempC = 18; break;
      case 9: exp[heat].tempC = 19; break;
      case 10: exp[cool].fan = AcFan::Low; break;
      default: break;
    }
    ASSERT_ENUM_MSG(r.result, m.apply(r.patch), r.label);
    const AcState& s = m.state();
    TEST_ASSERT_EQUAL_MESSAGE(r.power, s.power, r.label);
    ASSERT_ENUM_MSG(r.mode, s.mode, r.label);
    TEST_ASSERT_EQUAL_INT_MESSAGE(r.tempC, s.tempC, r.label);
    ASSERT_ENUM_MSG(r.fan, s.fan, r.label);
    for (int k = 0; k < kAcModeCount; ++k) {
      assertSettingsEq(exp[k], m.settingsFor(kAllModes[k]), r.label);
    }
  }
}

// ---- モード切替の復元 ----------------------------------------------------------------

// TC: TC-N09
// REQ: F2
void test_mode_switch_restores_temp() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcModel m;
  m.apply(patchPower(true));
  m.apply(patchTemp(27));
  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Heat)));
  TEST_ASSERT_EQUAL_INT(initialSettings(AcMode::Heat).tempC, m.state().tempC);
  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Cool)));
  TEST_ASSERT_EQUAL_INT(27, m.state().tempC);
}

// TC: TC-N10
// REQ: F2
void test_mode_switch_fan_does_not_leak() {
  AcFan f1;
  if (!findF1(&f1)) {
    TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  }
  AcModel m;
  m.apply(patchMode(AcMode::Heat));
  ASSERT_ENUM(AcError::None, m.apply(patchFan(f1)));
  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Cool)));
  ASSERT_ENUM(AcFan::Auto, m.state().fan);
  ASSERT_ENUM(f1, m.settingsFor(AcMode::Heat).fan);
}

// TC: TC-N11
// REQ: F1, F2
void test_mode_and_temp_together() {
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcModel m;
  AcPatch p;
  p.mode = AcMode::Heat;
  p.tempC = 18;
  ASSERT_ENUM(AcError::None, m.apply(p));
  ASSERT_ENUM(AcMode::Heat, m.state().mode);
  TEST_ASSERT_EQUAL_INT(18, m.state().tempC);
  TEST_ASSERT_EQUAL_INT(18, m.settingsFor(AcMode::Heat).tempC);
  TEST_ASSERT_EQUAL_INT(26, m.settingsFor(AcMode::Cool).tempC);
}

// ---- 温度の境界値 --------------------------------------------------------------------

// TC: TC-N12
// REQ: F1
void test_temp_min_accepted() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchTemp(cap::kTempMinC)));
  TEST_ASSERT_EQUAL_INT(cap::kTempMinC, m.state().tempC);
}

// TC: TC-N13
// REQ: F1
void test_temp_max_accepted() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchTemp(cap::kTempMaxC)));
  TEST_ASSERT_EQUAL_INT(cap::kTempMaxC, m.state().tempC);
}

// TC: TC-N14
// REQ: F1
void test_temp_below_min_rejected() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(patchTemp(cap::kTempMinC - 1)));
  TEST_ASSERT_EQUAL_INT(26, m.state().tempC);
}

// TC: TC-N15
// REQ: F1
void test_temp_above_max_rejected() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(patchTemp(cap::kTempMaxC + 1)));
  TEST_ASSERT_EQUAL_INT(26, m.state().tempC);
}

// TC: TC-N16
// REQ: F1
void test_temp_far_out_of_range_rejected() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;
  ASSERT_ENUM_MSG(AcError::TempOutOfRange, m.apply(patchTemp(300)), "300");
  ASSERT_ENUM_MSG(AcError::TempOutOfRange, m.apply(patchTemp(-300)), "-300");
  // 282 は int8_t に詰めると 26 になる。詰める前に落ちること
  ASSERT_ENUM_MSG(AcError::TempOutOfRange, m.apply(patchTemp(256 + 26)), "282");
  TEST_ASSERT_EQUAL_INT(26, m.state().tempC);
}

// ---- 選択肢の外 ----------------------------------------------------------------------

// TC: TC-N17
// REQ: F1
void test_fan_not_supported() {
  AcFan fx;
  if (!findFx(&fx)) {
    TEST_IGNORE_MESSAGE("every AcFan is a choice (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::FanNotSupported, m.apply(patchFan(fx)));
  ASSERT_ENUM(AcFan::Auto, m.state().fan);
}

// TC: TC-N18
// REQ: F1
void test_swingv_not_supported() {
  AcSwingV vx;
  if (!findVx(&vx)) {
    TEST_IGNORE_MESSAGE("every AcSwingV is a choice (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::SwingVNotSupported, m.apply(patchSwingV(vx)));
  assertStateEq(initialState(), m.state(), "state unchanged");
}

// TC: TC-N19
// REQ: F1
void test_swingh_not_supported() {
  AcSwingH hx;
  if (!findHx(&hx)) {
    TEST_IGNORE_MESSAGE("every AcSwingH is a choice (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::SwingHNotSupported, m.apply(patchSwingH(hx)));
  assertStateEq(initialState(), m.state(), "state unchanged");
}

// ---- 原子性・空・検証の順 -------------------------------------------------------------

// TC: TC-N20
// REQ: F1
void test_error_changes_nothing() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  REQUIRE_TEMP_MODE(AcMode::Heat);
  AcFan f1;
  if (!findF1(&f1)) {
    TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  }
  AcModel m;
  m.apply(patchPower(true));
  AcPatch h;
  h.mode = AcMode::Heat;
  h.tempC = 18;
  m.apply(h);
  m.apply(patchFan(f1));

  // 前提の状態が true, Heat, 18, F1 であること
  TEST_ASSERT_TRUE(m.state().power);
  ASSERT_ENUM(AcMode::Heat, m.state().mode);
  TEST_ASSERT_EQUAL_INT(18, m.state().tempC);
  ASSERT_ENUM(f1, m.state().fan);

  const AcState before = m.state();
  AcSettings beforeSettings[kAcModeCount];
  for (int i = 0; i < kAcModeCount; ++i) beforeSettings[i] = m.settingsFor(kAllModes[i]);

  AcPatch p;
  p.mode = AcMode::Cool;
  p.tempC = 40;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(p));

  assertStateEq(before, m.state(), "state unchanged");
  for (int i = 0; i < kAcModeCount; ++i) {
    assertSettingsEq(beforeSettings[i], m.settingsFor(kAllModes[i]), "settingsFor unchanged");
  }
}

// TC: TC-N21
// REQ: F1
void test_empty_patch() {
  AcModel m;
  ASSERT_ENUM(AcError::EmptyPatch, m.apply(AcPatch{}));
  assertStateEq(initialState(), m.state(), "state unchanged");
}

// TC: TC-N22
// REQ: F1
void test_validation_order_temp_before_fan() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcFan fx;
  if (!findFx(&fx)) {
    TEST_IGNORE_MESSAGE("every AcFan is a choice (F1-VALUES)");
  }
  AcModel m;
  AcPatch p;
  p.tempC = cap::kTempMaxC + 1;
  p.fan = fx;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(p));
}

// ---- 温度を指定できないモード（D-02 5節） ---------------------------------------------

// TC: TC-N23
// REQ: F1, F2
void test_temp_not_supported_in_mode() {
  AcMode mt;
  if (!findMt(&mt)) {
    TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  }
  // (a) 冷房のまま {mode:Mt, tempC:kTempMinC}
  AcModel m;
  AcPatch p;
  p.mode = mt;
  p.tempC = cap::kTempMinC;
  ASSERT_ENUM_MSG(AcError::TempNotSupported, m.apply(p), "(a)");
  ASSERT_ENUM_MSG(AcMode::Cool, m.state().mode, "(a) mode stays Cool");
  assertStateEq(initialState(), m.state(), "(a) state unchanged");

  // (b) Mt にしてから {tempC:24}
  AcModel m2;
  ASSERT_ENUM_MSG(AcError::None, m2.apply(patchMode(mt)), "(b) switch to Mt");
  const AcState before = m2.state();
  ASSERT_ENUM_MSG(AcError::TempNotSupported, m2.apply(patchTemp(24)), "(b)");
  assertStateEq(before, m2.state(), "(b) state unchanged");
}

// TC: TC-N24
// REQ: F1, F2
void test_mode_without_temp_has_no_temp() {
  AcMode mt;
  if (!findMt(&mt)) {
    TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchMode(mt)));
  ASSERT_ENUM(mt, m.state().mode);
  TEST_ASSERT_FALSE(m.state().hasTemp);
  TEST_ASSERT_EQUAL_INT(initialSettings(mt).tempC, m.state().tempC);
  TEST_ASSERT_EQUAL_INT(m.settingsFor(mt).tempC, m.state().tempC);
}

// ---- 停止中の変更・同じ値・validate ----------------------------------------------------

// TC: TC-N25
// REQ: F1
void test_changes_while_off_update_state() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcFan f1;
  if (!findF1(&f1)) {
    TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchTemp(24)));
  TEST_ASSERT_FALSE(m.state().power);
  TEST_ASSERT_EQUAL_INT(24, m.state().tempC);
  TEST_ASSERT_EQUAL_INT(24, m.settingsFor(AcMode::Cool).tempC);

  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Heat)));
  TEST_ASSERT_FALSE(m.state().power);
  ASSERT_ENUM(AcMode::Heat, m.state().mode);
  TEST_ASSERT_EQUAL_INT(initialSettings(AcMode::Heat).tempC, m.state().tempC);

  ASSERT_ENUM(AcError::None, m.apply(patchFan(f1)));
  TEST_ASSERT_FALSE(m.state().power);
  ASSERT_ENUM(f1, m.state().fan);
  ASSERT_ENUM(f1, m.settingsFor(AcMode::Heat).fan);
  ASSERT_ENUM(AcFan::Auto, m.settingsFor(AcMode::Cool).fan);
}

// TC: TC-N26
// REQ: F1
void test_same_mode_patch_changes_nothing() {
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Cool)));
  assertStateEq(initialState(), m.state(), "state same as initial");
}

// TC: TC-N27
// REQ: F1
void test_validate_does_not_change_state() {
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;
  ASSERT_ENUM(AcError::None, m.validate(patchTemp(27)));
  TEST_ASSERT_EQUAL_INT(26, m.state().tempC);
  TEST_ASSERT_EQUAL_INT(26, m.settingsFor(AcMode::Cool).tempC);
}

// ---- 文字列変換 ----------------------------------------------------------------------

// TC: TC-N28
// REQ: F1
void test_mode_string_roundtrip() {
  const char* names[] = {"auto", "cool", "dry", "heat"};
  for (const char* s : names) {
    std::optional<AcMode> v = parseAcMode(s);
    TEST_ASSERT_TRUE_MESSAGE(v.has_value(), s);
    TEST_ASSERT_EQUAL_STRING(s, toString(*v));
  }
  std::optional<AcMode> c = parseAcMode("cool");
  TEST_ASSERT_TRUE(c.has_value());
  ASSERT_ENUM(AcMode::Cool, *c);
  std::optional<AcMode> a = parseAcMode("auto");
  TEST_ASSERT_TRUE(a.has_value());
  ASSERT_ENUM(AcMode::Auto, *a);
}

// TC: TC-N29
// REQ: F1
void test_fan_string_roundtrip() {
  const char* names[] = {"auto", "min", "low", "medium", "high", "max"};
  for (const char* s : names) {
    std::optional<AcFan> v = parseAcFan(s);
    TEST_ASSERT_TRUE_MESSAGE(v.has_value(), s);
    TEST_ASSERT_EQUAL_STRING(s, toString(*v));
  }
  std::optional<AcFan> mn = parseAcFan("min");
  TEST_ASSERT_TRUE(mn.has_value());
  ASSERT_ENUM(AcFan::Min, *mn);
}

// TC: TC-N30
// REQ: F1
void test_swingv_string_roundtrip() {
  const char* names[] = {"off", "auto", "highest", "high", "middle", "low", "lowest"};
  for (const char* s : names) {
    std::optional<AcSwingV> v = parseAcSwingV(s);
    TEST_ASSERT_TRUE_MESSAGE(v.has_value(), s);
    TEST_ASSERT_EQUAL_STRING(s, toString(*v));
  }
}

// TC: TC-N31
// REQ: F1
void test_swingh_string_roundtrip() {
  const char* names[] = {"off", "auto", "leftMax", "left", "middle", "right", "rightMax", "wide"};
  for (const char* s : names) {
    std::optional<AcSwingH> v = parseAcSwingH(s);
    TEST_ASSERT_TRUE_MESSAGE(v.has_value(), s);
    TEST_ASSERT_EQUAL_STRING(s, toString(*v));
  }
}

// TC: TC-N32
// REQ: F1
void test_unknown_strings_are_nullopt() {
  const char* bad[] = {"", "Cool", "fan", "turbo", "quiet"};
  for (const char* s : bad) {
    TEST_ASSERT_FALSE_MESSAGE(parseAcMode(s).has_value(), s);
    TEST_ASSERT_FALSE_MESSAGE(parseAcFan(s).has_value(), s);
    TEST_ASSERT_FALSE_MESSAGE(parseAcSwingV(s).has_value(), s);
    TEST_ASSERT_FALSE_MESSAGE(parseAcSwingH(s).has_value(), s);
  }
  TEST_ASSERT_FALSE(parseAcSwingH("leftmax").has_value());
}

// ---- エラー文言・定数 ----------------------------------------------------------------

// TC: TC-N33
// REQ: F1
void test_error_messages() {
  TEST_ASSERT_EQUAL_STRING("", errorMessage(AcError::None));
  TEST_ASSERT_EQUAL_STRING("no ac fields", errorMessage(AcError::EmptyPatch));
  TEST_ASSERT_EQUAL_STRING("temp out of range", errorMessage(AcError::TempOutOfRange));
  TEST_ASSERT_EQUAL_STRING("temp not supported in this mode",
                           errorMessage(AcError::TempNotSupported));
  TEST_ASSERT_EQUAL_STRING("fan not supported", errorMessage(AcError::FanNotSupported));
  TEST_ASSERT_EQUAL_STRING("swingV not supported", errorMessage(AcError::SwingVNotSupported));
  TEST_ASSERT_EQUAL_STRING("swingH not supported", errorMessage(AcError::SwingHNotSupported));
}

// TC: TC-N34
// REQ: F1
void test_temp_step_is_one() {
  TEST_ASSERT_EQUAL_INT(1, cap::kTempStepC);
}

// ---- 自動モード（温度指定なし）の例・選択肢の中すべて ------------------------------------

// TC: TC-N202
// REQ: F1, F2
void test_auto_mode_example_table() {
  AcMode mt;
  if (!findMt(&mt)) {
    TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  }
  AcFan f1;
  if (!findF1(&f1)) {
    TEST_IGNORE_MESSAGE("no fan choice other than Auto (F1-VALUES)");
  }
  AcFan fx;
  if (!findFx(&fx)) {
    TEST_IGNORE_MESSAGE("every AcFan is a choice (F1-VALUES)");
  }
  REQUIRE_TEMP_MODE(AcMode::Cool);
  AcModel m;

  // A1 {power:true, mode:Mt}
  AcPatch a1; a1.power = true; a1.mode = mt;
  ASSERT_ENUM_MSG(AcError::None, m.apply(a1), "A1");
  TEST_ASSERT_TRUE_MESSAGE(m.state().power, "A1");
  ASSERT_ENUM_MSG(mt, m.state().mode, "A1");
  TEST_ASSERT_FALSE_MESSAGE(m.state().hasTemp, "A1");
  TEST_ASSERT_EQUAL_INT_MESSAGE(initialSettings(mt).tempC, m.state().tempC, "A1");
  ASSERT_ENUM_MSG(AcFan::Auto, m.state().fan, "A1");

  // A2 {tempC:24}
  AcState before = m.state();
  ASSERT_ENUM_MSG(AcError::TempNotSupported, m.apply(patchTemp(24)), "A2");
  assertStateEq(before, m.state(), "A2 unchanged");

  // A3 {fan:F1}
  ASSERT_ENUM_MSG(AcError::None, m.apply(patchFan(f1)), "A3");
  ASSERT_ENUM_MSG(f1, m.state().fan, "A3");
  ASSERT_ENUM_MSG(f1, m.settingsFor(mt).fan, "A3");

  // A4 {mode:Cool, tempC:24}
  AcPatch a4; a4.mode = AcMode::Cool; a4.tempC = 24;
  ASSERT_ENUM_MSG(AcError::None, m.apply(a4), "A4");
  TEST_ASSERT_TRUE_MESSAGE(m.state().power, "A4");
  ASSERT_ENUM_MSG(AcMode::Cool, m.state().mode, "A4");
  TEST_ASSERT_TRUE_MESSAGE(m.state().hasTemp, "A4");
  TEST_ASSERT_EQUAL_INT_MESSAGE(24, m.state().tempC, "A4");
  ASSERT_ENUM_MSG(AcFan::Auto, m.state().fan, "A4");
  TEST_ASSERT_EQUAL_INT_MESSAGE(24, m.settingsFor(AcMode::Cool).tempC, "A4");

  // A5 {mode:Mt, tempC:24}
  before = m.state();
  AcPatch a5; a5.mode = mt; a5.tempC = 24;
  ASSERT_ENUM_MSG(AcError::TempNotSupported, m.apply(a5), "A5");
  assertStateEq(before, m.state(), "A5 unchanged (still Cool)");

  // A6 {fan:Fx}
  ASSERT_ENUM_MSG(AcError::FanNotSupported, m.apply(patchFan(fx)), "A6");
  assertStateEq(before, m.state(), "A6 unchanged (still Cool)");

  // A7 {mode:Mt}：Mt の最後の設定（fan=F1）を復元
  ASSERT_ENUM_MSG(AcError::None, m.apply(patchMode(mt)), "A7");
  TEST_ASSERT_TRUE_MESSAGE(m.state().power, "A7");
  ASSERT_ENUM_MSG(mt, m.state().mode, "A7");
  TEST_ASSERT_FALSE_MESSAGE(m.state().hasTemp, "A7");
  ASSERT_ENUM_MSG(f1, m.state().fan, "A7");
}

// TC: TC-N203
// REQ: F1
void test_temp_not_supported_before_out_of_range() {
  AcMode mt;
  if (!findMt(&mt)) {
    TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchMode(mt)));
  const AcState before = m.state();
  ASSERT_ENUM_MSG(AcError::TempNotSupported, m.apply(patchTemp(cap::kTempMaxC + 1)), "max+1");
  ASSERT_ENUM_MSG(AcError::TempNotSupported, m.apply(patchTemp(99)), "99");
  assertStateEq(before, m.state(), "state unchanged");
}

// TC: TC-N204
// REQ: F1
void test_every_fan_choice_accepted() {
  AcModel m;
  for (int i = 0; i < cap::kFanChoiceCount; ++i) {
    const AcFan f = cap::kFanChoices[i];
    const char* label = toString(f);
    ASSERT_ENUM_MSG(AcError::None, m.apply(patchFan(f)), label);
    ASSERT_ENUM_MSG(f, m.state().fan, label);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state);
  RUN_TEST(test_initial_settings_per_mode);
  RUN_TEST(test_apply_temp_only);
  RUN_TEST(test_apply_power_only);
  RUN_TEST(test_apply_fan_only);
  RUN_TEST(test_apply_swingv_only);
  RUN_TEST(test_apply_swingh_only);
  RUN_TEST(test_transition_table);
  RUN_TEST(test_mode_switch_restores_temp);
  RUN_TEST(test_mode_switch_fan_does_not_leak);
  RUN_TEST(test_mode_and_temp_together);
  RUN_TEST(test_temp_min_accepted);
  RUN_TEST(test_temp_max_accepted);
  RUN_TEST(test_temp_below_min_rejected);
  RUN_TEST(test_temp_above_max_rejected);
  RUN_TEST(test_temp_far_out_of_range_rejected);
  RUN_TEST(test_fan_not_supported);
  RUN_TEST(test_swingv_not_supported);
  RUN_TEST(test_swingh_not_supported);
  RUN_TEST(test_error_changes_nothing);
  RUN_TEST(test_empty_patch);
  RUN_TEST(test_validation_order_temp_before_fan);
  RUN_TEST(test_temp_not_supported_in_mode);
  RUN_TEST(test_mode_without_temp_has_no_temp);
  RUN_TEST(test_changes_while_off_update_state);
  RUN_TEST(test_same_mode_patch_changes_nothing);
  RUN_TEST(test_validate_does_not_change_state);
  RUN_TEST(test_mode_string_roundtrip);
  RUN_TEST(test_fan_string_roundtrip);
  RUN_TEST(test_swingv_string_roundtrip);
  RUN_TEST(test_swingh_string_roundtrip);
  RUN_TEST(test_unknown_strings_are_nullopt);
  RUN_TEST(test_error_messages);
  RUN_TEST(test_temp_step_is_one);
  RUN_TEST(test_auto_mode_example_table);
  RUN_TEST(test_temp_not_supported_before_out_of_range);
  RUN_TEST(test_every_fan_choice_accepted);
  return UNITY_END();
}
