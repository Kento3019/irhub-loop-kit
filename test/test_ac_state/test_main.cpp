// test_ac_state：エアコン状態モデル（AcModel）、cap::、文字列変換の単体テスト
// 根拠：docs/design/02-ac-state.md（D-02）、docs/test/test-plan.md「A. test/test_ac_state」（TC-N01〜TC-N34）
// 受信結果待ちの値（F1-VALUES）は数値を直書きせず cap:: の定数・関数から作る。
#include <unity.h>

#include <cstdio>
#include <optional>
#include <string_view>

#include "ac_capabilities.h"
#include "ac_state.h"

using namespace irhub;

// ---- 補助 ---------------------------------------------------------------------------

#define ASSERT_ENUM(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))
#define ASSERT_ENUM_MSG(expected, actual, msg) \
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(actual), msg)

static const AcMode kAllModes[kAcModeCount] = {AcMode::Auto, AcMode::Cool, AcMode::Dry,
                                               AcMode::Heat};
static const int kAcFanCount = 6;     // AcFan の列挙子の数（D-02 1節）
static const int kAcSwingVCount = 7;  // AcSwingV の列挙子の数
static const int kAcSwingHCount = 8;  // AcSwingH の列挙子の数

static AcPatch patchPower(bool v) { AcPatch p; p.power = v; return p; }
static AcPatch patchMode(AcMode v) { AcPatch p; p.mode = v; return p; }
static AcPatch patchTemp(int v) { AcPatch p; p.tempC = v; return p; }
static AcPatch patchFan(AcFan v) { AcPatch p; p.fan = v; return p; }
static AcPatch patchSwingV(AcSwingV v) { AcPatch p; p.swingV = v; return p; }
static AcPatch patchSwingH(AcSwingH v) { AcPatch p; p.swingH = v; return p; }

// 列挙のうち選択肢の外の最初の値を探す。無ければ false
static bool findUnsupportedFan(AcFan* out) {
  for (int i = 0; i < kAcFanCount; ++i) {
    AcFan f = static_cast<AcFan>(i);
    if (!cap::fanSupported(f)) { *out = f; return true; }
  }
  return false;
}
static bool findUnsupportedSwingV(AcSwingV* out) {
  for (int i = 0; i < kAcSwingVCount; ++i) {
    AcSwingV v = static_cast<AcSwingV>(i);
    if (!cap::swingVSupported(v)) { *out = v; return true; }
  }
  return false;
}
static bool findUnsupportedSwingH(AcSwingH* out) {
  for (int i = 0; i < kAcSwingHCount; ++i) {
    AcSwingH h = static_cast<AcSwingH>(i);
    if (!cap::swingHSupported(h)) { *out = h; return true; }
  }
  return false;
}
static bool findModeWithoutTemp(AcMode* out) {
  for (int i = 0; i < kAcModeCount; ++i) {
    if (!cap::tempSupported(kAllModes[i])) { *out = kAllModes[i]; return true; }
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

// TC-N01 の初期状態
static AcState initialState() {
  AcState s;
  s.power = false;
  s.mode = AcMode::Cool;
  s.hasTemp = true;
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
  TEST_ASSERT_TRUE(s.hasTemp);
  TEST_ASSERT_EQUAL_INT(26, s.tempC);
  ASSERT_ENUM(AcFan::Auto, s.fan);
  ASSERT_ENUM(cap::kSwingVDefault, s.swingV);
  ASSERT_ENUM(cap::kSwingHDefault, s.swingH);
}

// TC: TC-N02
// REQ: F2
void test_initial_settings_per_mode() {
  AcModel m;
  TEST_ASSERT_EQUAL_INT(25, m.settingsFor(AcMode::Auto).tempC);
  TEST_ASSERT_EQUAL_INT(26, m.settingsFor(AcMode::Cool).tempC);
  TEST_ASSERT_EQUAL_INT(26, m.settingsFor(AcMode::Dry).tempC);
  TEST_ASSERT_EQUAL_INT(20, m.settingsFor(AcMode::Heat).tempC);
  for (int i = 0; i < kAcModeCount; ++i) {
    const AcSettings& st = m.settingsFor(kAllModes[i]);
    ASSERT_ENUM(AcFan::Auto, st.fan);
    ASSERT_ENUM(cap::kSwingVDefault, st.swingV);
    ASSERT_ENUM(cap::kSwingHDefault, st.swingH);
  }
}

// ---- 部分更新（1項目だけ） ------------------------------------------------------------

// TC: TC-N03
// REQ: F1
void test_apply_temp_only() {
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
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchFan(AcFan::High)));
  AcState e = initialState();
  e.fan = AcFan::High;
  assertStateEq(e, m.state(), "state after {fan:high}");
  ASSERT_ENUM(AcFan::High, m.settingsFor(AcMode::Cool).fan);
}

// TC: TC-N06
// REQ: F1
void test_apply_swingv_only() {
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchSwingV(AcSwingV::Auto)));
  AcState e = initialState();
  e.swingV = AcSwingV::Auto;
  assertStateEq(e, m.state(), "state after {swingV:auto}");
}

// TC: TC-N07
// REQ: F1
void test_apply_swingh_only() {
  bool found = false;
  AcSwingH h = cap::kSwingHDefault;
  for (int i = 0; i < cap::kSwingHChoiceCount; ++i) {
    if (cap::kSwingHChoices[i] != cap::kSwingHDefault) {
      h = cap::kSwingHChoices[i];
      found = true;
      break;
    }
  }
  if (!found) {
    TEST_IGNORE_MESSAGE("no swingH choice other than kSwingHDefault (F1-VALUES provisional)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchSwingH(h)));
  AcState e = initialState();
  e.swingH = h;
  assertStateEq(e, m.state(), "state after {swingH}");
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
  AcModel m;

  AcPatch r4; r4.tempC = 22; r4.fan = AcFan::High;
  AcPatch r6; r6.mode = AcMode::Heat; r6.tempC = 18;
  AcPatch r8; r8.mode = AcMode::Cool; r8.tempC = 40;
  AcPatch r11; r11.mode = AcMode::Cool; r11.fan = AcFan::Low;

  const TransitionRow rows[] = {
    {"row1 {power:true}", patchPower(true), AcError::None, true, AcMode::Cool, 26, AcFan::Auto},
    {"row2 {temp:27}", patchTemp(27), AcError::None, true, AcMode::Cool, 27, AcFan::Auto},
    {"row3 {mode:heat}", patchMode(AcMode::Heat), AcError::None, true, AcMode::Heat, 20, AcFan::Auto},
    {"row4 {temp:22,fan:high}", r4, AcError::None, true, AcMode::Heat, 22, AcFan::High},
    {"row5 {mode:cool}", patchMode(AcMode::Cool), AcError::None, true, AcMode::Cool, 27, AcFan::Auto},
    {"row6 {mode:heat,temp:18}", r6, AcError::None, true, AcMode::Heat, 18, AcFan::High},
    {"row7 {temp:max+1}", patchTemp(cap::kTempMaxC + 1), AcError::TempOutOfRange, true, AcMode::Heat, 18, AcFan::High},
    {"row8 {mode:cool,temp:40}", r8, AcError::TempOutOfRange, true, AcMode::Heat, 18, AcFan::High},
    {"row9 {power:false}", patchPower(false), AcError::None, false, AcMode::Heat, 18, AcFan::High},
    {"row10 {temp:19}", patchTemp(19), AcError::None, false, AcMode::Heat, 19, AcFan::High},
    {"row11 {mode:cool,fan:low}", r11, AcError::None, false, AcMode::Cool, 27, AcFan::Low},
    {"row12 {power:false}", patchPower(false), AcError::None, false, AcMode::Cool, 27, AcFan::Low},
    {"row13 {power:true}", patchPower(true), AcError::None, true, AcMode::Cool, 27, AcFan::Low},
    {"row14 {}", AcPatch{}, AcError::EmptyPatch, true, AcMode::Cool, 27, AcFan::Low},
  };

  // モード別の記憶の期待値（表の「perMode_ の変化」欄を反映していく）
  AcSettings exp[kAcModeCount];
  for (int i = 0; i < kAcModeCount; ++i) {
    exp[i].fan = AcFan::Auto;
    exp[i].swingV = cap::kSwingVDefault;
    exp[i].swingH = cap::kSwingHDefault;
  }
  exp[static_cast<int>(AcMode::Auto)].tempC = 25;
  exp[static_cast<int>(AcMode::Cool)].tempC = 26;
  exp[static_cast<int>(AcMode::Dry)].tempC = 26;
  exp[static_cast<int>(AcMode::Heat)].tempC = 20;

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
  AcModel m;
  m.apply(patchPower(true));
  m.apply(patchTemp(27));
  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Heat)));
  TEST_ASSERT_EQUAL_INT(20, m.state().tempC);
  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Cool)));
  TEST_ASSERT_EQUAL_INT(27, m.state().tempC);
}

// TC: TC-N10
// REQ: F2
void test_mode_switch_fan_does_not_leak() {
  AcModel m;
  m.apply(patchMode(AcMode::Heat));
  ASSERT_ENUM(AcError::None, m.apply(patchFan(AcFan::High)));
  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Cool)));
  ASSERT_ENUM(AcFan::Auto, m.state().fan);
  ASSERT_ENUM(AcFan::High, m.settingsFor(AcMode::Heat).fan);
}

// TC: TC-N11
// REQ: F1, F2
void test_mode_and_temp_together() {
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
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchTemp(cap::kTempMinC)));
  TEST_ASSERT_EQUAL_INT(cap::kTempMinC, m.state().tempC);
}

// TC: TC-N13
// REQ: F1
void test_temp_max_accepted() {
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchTemp(cap::kTempMaxC)));
  TEST_ASSERT_EQUAL_INT(cap::kTempMaxC, m.state().tempC);
}

// TC: TC-N14
// REQ: F1
void test_temp_below_min_rejected() {
  AcModel m;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(patchTemp(cap::kTempMinC - 1)));
  TEST_ASSERT_EQUAL_INT(26, m.state().tempC);
}

// TC: TC-N15
// REQ: F1
void test_temp_above_max_rejected() {
  AcModel m;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(patchTemp(cap::kTempMaxC + 1)));
  TEST_ASSERT_EQUAL_INT(26, m.state().tempC);
}

// TC: TC-N16
// REQ: F1
void test_temp_far_out_of_range_rejected() {
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
  AcFan f;
  if (!findUnsupportedFan(&f)) {
    TEST_IGNORE_MESSAGE("every AcFan is a choice (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::FanNotSupported, m.apply(patchFan(f)));
  ASSERT_ENUM(AcFan::Auto, m.state().fan);
}

// TC: TC-N18
// REQ: F1
void test_swingv_not_supported() {
  AcSwingV v;
  if (!findUnsupportedSwingV(&v)) {
    TEST_IGNORE_MESSAGE("every AcSwingV is a choice (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::SwingVNotSupported, m.apply(patchSwingV(v)));
  assertStateEq(initialState(), m.state(), "state unchanged");
}

// TC: TC-N19
// REQ: F1
void test_swingh_not_supported() {
  AcSwingH h;
  if (!findUnsupportedSwingH(&h)) {
    TEST_IGNORE_MESSAGE("every AcSwingH is a choice (F1-VALUES)");
  }
  AcModel m;
  ASSERT_ENUM(AcError::SwingHNotSupported, m.apply(patchSwingH(h)));
  assertStateEq(initialState(), m.state(), "state unchanged");
}

// ---- 原子性・空・検証の順 -------------------------------------------------------------

// TC: TC-N20
// REQ: F1
void test_error_changes_nothing() {
  AcModel m;
  m.apply(patchPower(true));
  AcPatch h;
  h.mode = AcMode::Heat;
  h.tempC = 18;
  m.apply(h);
  m.apply(patchFan(AcFan::High));

  const AcState before = m.state();
  AcSettings beforeSettings[kAcModeCount];
  for (int i = 0; i < kAcModeCount; ++i) beforeSettings[i] = m.settingsFor(kAllModes[i]);

  AcPatch p;
  p.mode = AcMode::Cool;
  p.tempC = 40;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(p));

  TEST_ASSERT_TRUE(m.state().power);
  ASSERT_ENUM(AcMode::Heat, m.state().mode);
  TEST_ASSERT_EQUAL_INT(18, m.state().tempC);
  ASSERT_ENUM(AcFan::High, m.state().fan);
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
  AcFan f;
  if (!findUnsupportedFan(&f)) {
    TEST_IGNORE_MESSAGE("every AcFan is a choice (F1-VALUES)");
  }
  AcModel m;
  AcPatch p;
  p.tempC = cap::kTempMaxC + 1;
  p.fan = f;
  ASSERT_ENUM(AcError::TempOutOfRange, m.apply(p));
}

// ---- 温度を指定できないモード（D-02 5節） ---------------------------------------------

// TC: TC-N23
// REQ: F1, F2
void test_temp_not_supported_in_mode() {
  AcMode md;
  if (!findModeWithoutTemp(&md)) {
    TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES provisional)");
  }
  AcModel m;
  AcPatch p;
  p.mode = md;
  p.tempC = cap::kTempMinC;
  ASSERT_ENUM(AcError::TempNotSupported, m.apply(p));
  ASSERT_ENUM(AcMode::Cool, m.state().mode);
}

// TC: TC-N24
// REQ: F1, F2
void test_mode_without_temp_has_no_temp() {
  AcMode md;
  if (!findModeWithoutTemp(&md)) {
    TEST_IGNORE_MESSAGE("every mode supports temp (F1-VALUES provisional)");
  }
  AcModel m;
  const int initialTemp = m.settingsFor(md).tempC;
  ASSERT_ENUM(AcError::None, m.apply(patchMode(md)));
  TEST_ASSERT_FALSE(m.state().hasTemp);
  TEST_ASSERT_EQUAL_INT(initialTemp, m.state().tempC);
  TEST_ASSERT_EQUAL_INT(m.settingsFor(md).tempC, m.state().tempC);
}

// ---- 停止中の変更・同じ値・validate ----------------------------------------------------

// TC: TC-N25
// REQ: F1
void test_changes_while_off_update_state() {
  AcModel m;
  ASSERT_ENUM(AcError::None, m.apply(patchTemp(24)));
  TEST_ASSERT_FALSE(m.state().power);
  TEST_ASSERT_EQUAL_INT(24, m.state().tempC);
  TEST_ASSERT_EQUAL_INT(24, m.settingsFor(AcMode::Cool).tempC);

  ASSERT_ENUM(AcError::None, m.apply(patchMode(AcMode::Heat)));
  TEST_ASSERT_FALSE(m.state().power);
  ASSERT_ENUM(AcMode::Heat, m.state().mode);
  TEST_ASSERT_EQUAL_INT(20, m.state().tempC);

  ASSERT_ENUM(AcError::None, m.apply(patchFan(AcFan::High)));
  TEST_ASSERT_FALSE(m.state().power);
  ASSERT_ENUM(AcFan::High, m.state().fan);
  ASSERT_ENUM(AcFan::High, m.settingsFor(AcMode::Heat).fan);
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
  const char* bad[] = {"", "Cool", "fan", "turbo"};
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
  return UNITY_END();
}
