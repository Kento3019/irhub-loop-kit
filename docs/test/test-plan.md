# テスト計画

作業項目：T-01／要件の原本：`docs/requirements.md` v0.3／要件ID：`docs/req-index.json`／根拠にした設計書：`docs/design/01-architecture.md`（D-01）〜`06-runtime.md`（D-06）、`docs/ops/remote-access.md`（D-07）

対象要件：[F1] [F2] [F3] [F4] [F4-LIMIT] [F4-IO] [F5] [F6] [N-AUTH] [N-WIFI] [N-STATE] [N-BOOT] [N-TIME] [N-RESP] [UI] [API] [HW-PINS]

---

## 方針

### 1. テストの種類と置き場所

| 種類 | 実行方法 | 置き場所 | 書く作業項目 | ケースID |
|---|---|---|---|---|
| native 単体テスト（Unity） | `pio test -e native` | `test/test_ac_state/`（`AcModel`・`cap::`・文字列変換） | T-02 | TC-N01〜TC-N34 |
| 〃 | 〃 | `test/test_schedule/`（スケジュールの純関数・`ScheduleList`・`schedule_json`・`Hub` のスケジュール実行） | T-03 | TC-N35〜TC-N129、TC-N182、TC-N183、TC-N197、TC-N198 |
| 〃 | 〃 | `test/test_api/`（`Hub` の基本動作・`ApiRouter`） | T-04 | TC-N130〜TC-N181、TC-N184〜TC-N196、TC-N199〜TC-N201 |
| 実機（人が行う） | 手順どおりに操作して目で確かめる | この文書の「テストケース（実機）」 | H-0〜H-6 のゲートで人が使う | TC-H01〜TC-H49、TC-H60、TC-H61、TC-H64〜TC-H66、TC-H68〜TC-H70、TC-H75、TC-H76 |
| PC とモック（人が行う） | `python tools/mock_server.py`、`python tools/embed_html.py` | 同上 | I-04 の後、H-4 の前に人が行う | TC-H50〜TC-H59、TC-H62、TC-H63、TC-H67、TC-H71〜TC-H74 |

- `Hub` を使うテストの置き場所（D-02 テスト観点で「T-01 で決める」とされた点）：`Hub::applyAc`・`Hub::pressLight`・コンストラクタ（N-BOOT）・センサー周期（F5）は `test/test_api/`、`Hub::tick` のスケジュール実行と `Hub::replaceSchedules` は `test/test_schedule/` に置く。
- N-WIFI（`src/wifi_manager`）、D-06 の `src/` の処理、画面（`web/index.html`）は native テストの対象外（D-01 1節・D-05・D-06 のテスト観点）。実機ケースと PC ケースで確かめる。
- F6 は code=false。実機ケース（フェーズ6）だけで確かめる。

### 2. 期待値の書き方

- 受信結果待ちの値（温度範囲・風量・風向の選択肢）と上限件数は、テストコードでは `cap::kTempMinC`・`cap::kTempMaxC`・`cap::fanSupported()`・`kScheduleMax`・`kScheduleIdMax` などの定数から作る（D-02・D-03 のテスト観点）。この計画では読みやすさのため、今の仮値での具体値を括弧で添える（例：`cap::kTempMaxC+1`（今は 31））。
- 選択肢の外の値は「列挙のうち `cap::fanSupported` が false の最初のもの」をループで探して使う（今の仮値なら風量 `Min`、上下 `Highest`、左右 `Auto`）。見つからなければそのテストは `TEST_IGNORE_MESSAGE`。
- 温度を指定できないモードのテスト（D-02 5節）は、`cap::tempSupported(m)` が false のモードが無ければ `TEST_IGNORE_MESSAGE` で飛ばす（今の仮値ではすべて true なので飛ぶ。D1 後に自動で有効になる）。
- エラー文言は設計書の表の文字列をそのまま比べる（`TEST_ASSERT_EQUAL_STRING`）。件数を含む文言は `snprintf(buf, sizeof buf, "too many schedules (max %d)", kScheduleMax)` で作って比べる（今は `too many schedules (max 10)`）。
- API の応答本文は ArduinoJson で読み直してキーごとに比べる（文字列全体の一致は、キーの順が設計で決まっているもの＝`schedule_json` の `action` だけで使う）。

### 3. フェイク（各テストディレクトリの `fake_ports.h`、D-01 9節の形に足す）

| フェイク | 持つもの（D-01 の例に足す分は ★） |
|---|---|
| `FakeIrSender` | `acCount`、`lastAc`、`lightCount`、`lastLight`、★`bool acResult = true`・`bool lightResult = true`（戻り値を切り替える） |
| `FakeClock` | `r`（返す `ClockReading`）、★`int nowCount`（`now()` の呼び出し回数）、★`std::vector<ClockReading> seq`（空でなければ `now()` は `r` の代わりに `seq` を先頭から1つずつ返し、使い切った後は最後の値を返し続ける。呼ぶたびに時刻が進む時計の代わり） |
| `FakeClimateSensor` | `readCount`、`next`（返す `ClimateReading`） |

### 4. 基準の時刻（test_schedule・test_api で使う）

D-03 4.1 の式（`m = epochMin + 540`、`wday = (m/1440 + 4) % 7`）で確かめた値。

| 名前 | epochSec | epochMin | JST |
|---|---|---|---|
| T0 | 1790287200 | M0 = 29838120 | 2026-09-25（金）07:00:00 |
| T20 | 1790334000 | 29838900 | 2026-09-25（金）20:00:00 |
| 金 23:59 | 1790348340 | 29839139 | 2026-09-25（金）23:59:00（UTC 14:59） |
| 土 00:00 | 1790348400 | 29839140 | 2026-09-26（土）00:00:00（UTC 15:00） |
| 土 23:59 | — | 29840579 | 2026-09-26（土）23:59 |
| 日 00:00 | — | 29840580 | 2026-09-27（日）00:00 |

- 「FakeClock を 07:00:10 にする」は `r = {true, {2026,9,25,7,0,10,5}, T0+10}` とすること。未取得は `r = {false, {}, 0}`。
- スケジュールの略記：`S(id, "HH:MM", days, 操作)`。days は曜日ビット（日=0x01 … 土=0x40、平日=0x3E、毎日=0x7F）。id を書かないものは 0（採番される）。

### 5. 境界値と異常系の必須項目（どのケースで扱うか）

| 観点 | ケース |
|---|---|
| 温度の上下限±1 | TC-N12〜N15、N64、N159、TC-H22 |
| スケジュール10件目と11件目 | TC-N69、N70、N114、N171、TC-H39 |
| 23:59→00:00（日付の境目） | TC-N37、N53 |
| 曜日の境目（土→日、金→土） | TC-N38、N41、N53 |
| 不正 JSON | TC-N110、N162、N168 |
| 範囲外 | TC-N14〜N16、N57〜N59、N64、N124、N129、N159、TC-H17 |
| 未知のボタン名 | TC-N140、N166、N120 |

---

## テストケース（native）

### A. test/test_ac_state（T-02：AcModel、cap::、文字列）

前提（全ケース共通）：`AcModel m;` を新しく作る。「状態」は `m.state()`。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N01 | [F2] [N-STATE] | 生成直後 | `m.state()` を読む | `power=false`、`mode=Cool`、`hasTemp=true`、`tempC=26`、`fan=Auto`、`swingV=cap::kSwingVDefault`（今は Off）、`swingH=cap::kSwingHDefault`（今は Off） |
| TC-N02 | [F2] | 生成直後 | `settingsFor` を4モード読む | Auto：`tempC=25`、Cool：26、Dry：26、Heat：20。4モードとも `fan=Auto`、`swingV=kSwingVDefault`、`swingH=kSwingHDefault` |
| TC-N03 | [F1] | 生成直後 | `apply({tempC=27})` | 戻り値 `None`。`tempC=27`、`power=false`・`mode=Cool`・`fan=Auto`・風向は変わらない。`settingsFor(Cool).tempC=27` |
| TC-N04 | [F1] | 生成直後 | `apply({power=true})` | `None`。`power=true`、`mode=Cool`・`tempC=26`・`fan=Auto`・風向は変わらない |
| TC-N05 | [F1] | 生成直後 | `apply({fan=High})` | `None`。`fan=High`、他は変わらない。`settingsFor(Cool).fan=High` |
| TC-N06 | [F1] | 生成直後 | `apply({swingV=Auto})` | `None`。`swingV=Auto`、他は変わらない |
| TC-N07 | [F1] | 生成直後。`cap::kSwingHChoices` のうち `cap::kSwingHDefault` と違う値 h を探す（無ければ `TEST_IGNORE_MESSAGE`。今の仮値は `Off` だけなので飛ぶ） | `apply({swingH=h})` | `None`。`swingH=h`、他は変わらない |
| TC-N08 | [F1] [F2] | 生成直後 | D-02 4節の遷移例の表の14行を上から順に `apply`（`{"temp":31}` の行は `kTempMaxC+1` で作る） | 各行の後で、結果（上から None×6、TempOutOfRange×2、None×5、EmptyPatch）、状態（power, mode, temp, fan）、`settingsFor` の変化が表どおり。停止中の2行（`{"temp":19}`→`false, Heat, 19, High`、`{"mode":"cool","fan":"low"}`→`false, Cool, 27, Low`、`settingsFor(Cool).fan=Low`）も運転中と同じに更新される。最後の状態は `true, Cool, 27, Low`（「送信」欄は `AcModel` では確かめない。TC-N191） |
| TC-N09 | [F2] | `apply({power=true})`、`apply({tempC=27})` | `apply({mode=Heat})` の後 `apply({mode=Cool})` | 暖房にしたとき `tempC=20`、冷房に戻したとき `tempC=27` |
| TC-N10 | [F2] | `apply({mode=Heat})` | `apply({fan=High})` の後 `apply({mode=Cool})` | 冷房の `fan=Auto`（暖房の High が漏れない）。`settingsFor(Heat).fan=High` |
| TC-N11 | [F1] [F2] | 生成直後（冷房） | `apply({mode=Heat, tempC=18})` | `None`。`mode=Heat`、`tempC=18`。`settingsFor(Heat).tempC=18`、`settingsFor(Cool).tempC=26` のまま |
| TC-N12 | [F1] | 生成直後 | `apply({tempC=cap::kTempMinC})`（今は 16） | `None`、`tempC=16` |
| TC-N13 | [F1] | 生成直後 | `apply({tempC=cap::kTempMaxC})`（今は 30） | `None`、`tempC=30` |
| TC-N14 | [F1] | 生成直後 | `apply({tempC=cap::kTempMinC-1})`（今は 15） | `TempOutOfRange`、`tempC=26` のまま |
| TC-N15 | [F1] | 生成直後 | `apply({tempC=cap::kTempMaxC+1})`（今は 31） | `TempOutOfRange`、`tempC=26` のまま |
| TC-N16 | [F1] | 生成直後 | `apply({tempC=300})`、`apply({tempC=-300})`、`apply({tempC=256+26})`（282。int8_t に詰めると 26 になる値） | 3回とも `TempOutOfRange`、`tempC=26` のまま |
| TC-N17 | [F1] | 生成直後。`cap::fanSupported` が false の `AcFan` を探す（今は Min） | `apply({fan=それ})` | `FanNotSupported`、`fan=Auto` のまま |
| TC-N18 | [F1] | 同様に `swingVSupported` が false の値（今は Highest） | `apply({swingV=それ})` | `SwingVNotSupported`、状態は変わらない |
| TC-N19 | [F1] | 同様に `swingHSupported` が false の値（今は Auto） | `apply({swingH=それ})` | `SwingHNotSupported`、状態は変わらない |
| TC-N20 | [F1] | `apply({power=true})`、`apply({mode=Heat, tempC=18})`、`apply({fan=High})` で `true, Heat, 18, High` | `apply({mode=Cool, tempC=40})` | `TempOutOfRange`。状態は `true, Heat, 18, High` のまま（モードも変わらない）。4モードの `settingsFor` が呼ぶ前と全項目一致 |
| TC-N21 | [F1] | 生成直後 | `apply({})` | `EmptyPatch`、状態は TC-N01 と同じ |
| TC-N22 | [F1] | 生成直後 | `apply({tempC=cap::kTempMaxC+1, fan=選択肢外})` | `TempOutOfRange`（検証の順：温度が風量より先） |
| TC-N23 | [F1] [F2] | `cap::tempSupported(m)` が false のモード m（無ければ IGNORE） | `apply({mode=m, tempC=cap::kTempMinC})` | `TempNotSupported`、モードは Cool のまま |
| TC-N24 | [F1] [F2] | TC-N23 と同じ m（無ければ IGNORE） | `apply({mode=m})` | `None`、`hasTemp=false`、`tempC` は `settingsFor(m).tempC`（初期値） |
| TC-N25 | [F1] | 生成直後（`power=false`） | `apply({tempC=24})` → `apply({mode=Heat})` → `apply({fan=High})` | 3回とも `None`、`power=false` のまま。1回目の後 `tempC=24`・`settingsFor(Cool).tempC=24`。2回目の後 `mode=Heat`・`tempC=20`。3回目の後 `fan=High`・`settingsFor(Heat).fan=High`・`settingsFor(Cool).fan=Auto`（停止中も運転中と同じ規則で状態とモード別の記憶が更新される。D-02 4節） |
| TC-N26 | [F1] | 生成直後（冷房） | `apply({mode=Cool})` | `None`、状態は TC-N01 と全項目同じ |
| TC-N27 | [F1] | 生成直後 | `validate({tempC=27})` | `None`。`state().tempC=26` のまま（validate は状態を変えない） |
| TC-N28 | [F1] | — | `auto` `cool` `dry` `heat` を `parseAcMode` → `toString` | 4つとも元の文字列に戻る。`parseAcMode("cool")==Cool` |
| TC-N29 | [F1] | — | `auto` `min` `low` `medium` `high` `max` を `parseAcFan` → `toString` | 6つとも元に戻る |
| TC-N30 | [F1] | — | `off` `auto` `highest` `high` `middle` `low` `lowest` を `parseAcSwingV` → `toString` | 7つとも元に戻る |
| TC-N31 | [F1] | — | `off` `auto` `leftMax` `left` `middle` `right` `rightMax` `wide` を `parseAcSwingH` → `toString` | 8つとも元に戻る |
| TC-N32 | [F1] | — | `""`、`"Cool"`、`"fan"`、`"turbo"` を4つの `parse*` に、`"leftmax"` を `parseAcSwingH` に渡す | すべて `nullopt`（大文字小文字を区別） |
| TC-N33 | [F1] | — | `errorMessage` を7つの `AcError` で呼ぶ | `""`、`no ac fields`、`temp out of range`、`temp not supported in this mode`、`fan not supported`、`swingV not supported`、`swingH not supported` |
| TC-N34 | [F1] | — | `cap::kTempStepC` を読む | 1（要件 F1 の決定値） |

### B. test/test_schedule（T-03：スケジュール）

#### B-1. 時刻の変換と判定（純関数）

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N35 | [F4] [N-TIME] | — | `jstFromEpochMinute(29838900)` | `{wday=5, hour=20, minute=0}` |
| TC-N36 | [F4] [N-TIME] | — | `jstFromEpochMinute(29838120)`（M0） | `{5, 7, 0}` |
| TC-N37 | [F4] [N-TIME] | — | `jstFromEpochMinute(29839139)`、`(29839140)` | 金 23:59 `{5, 23, 59}`、次の分は土 00:00 `{6, 0, 0}`（日付と曜日が進む） |
| TC-N38 | [F4] | — | `jstFromEpochMinute(29840579)`、`(29840580)` | `{6, 23, 59}`、`{0, 0, 0}`（土→日で wday 6→0） |
| TC-N39 | [F4] | `s = S(1, "07:00", 0x3E, 照明 Full)`、enabled | `scheduleMatches(s, {5,7,0})` | true |
| TC-N40 | [F4] | TC-N39 の s | `{5,7,1}`、`{5,8,0}`、`{5,6,59}` | 3つとも false |
| TC-N41 | [F4] | TC-N39 の s（平日） | `{6,7,0}`（土）、`{0,7,0}`（日） | どちらも false |
| TC-N42 | [F4] | TC-N39 の s で `enabled=false` | `{5,7,0}` | false |
| TC-N43 | [F4] [N-TIME] [N-BOOT] | — | `nextWindow(nullopt, M0)` | `from > to`、`newLast = M0` |
| TC-N44 | [F4] | — | `nextWindow(M0, M0)`（d=0） | `from > to`、`newLast = M0` |
| TC-N45 | [F4] | — | `nextWindow(M0-1, M0)`（d=1） | `from = M0`、`to = M0`、`newLast = M0` |
| TC-N46 | [F4] | — | `nextWindow(M0-2, M0)`（d=2） | `from = M0-1`、`to = M0`、`newLast = M0` |
| TC-N47 | [F4] | — | `nextWindow(M0-kScheduleCatchUpMinutes, M0)`（d=5） | `from = M0-4`、`to = M0`、`newLast = M0` |
| TC-N48 | [F4] | — | `nextWindow(M0-6, M0)`（d=6） | `from = M0`、`to = M0`、`newLast = M0` |
| TC-N49 | [F4] | — | `nextWindow(M0+1, M0)`（d=-1） | `from > to`、`newLast = M0+1`（据え置き） |
| TC-N50 | [F4] | — | `nextWindow(M0+300, M0)`（d=-300） | `from > to`、`newLast = M0+300`（戻りが5分を超えても据え置き） |
| TC-N51 | [F4] | 一覧 `[S(5,"07:00",0x7F,照明 Full), S(3,"07:00",0x7F,照明 Night)]` | `dueSchedules(list, M0, M0, out, kScheduleMax)` | 戻り値 2、`out[0]={5, M0}`、`out[1]={3, M0}`（一覧の順） |
| TC-N52 | [F4] | 一覧 `[S(1,"07:02",0x7F,…), S(2,"07:00",0x7F,…)]` | `dueSchedules(list, M0, M0+2, out, 10)` | 戻り値 2、`out[0]={2, M0}`、`out[1]={1, M0+2}`（分の昇順） |
| TC-N53 | [F4] | 一覧 `[S(1,"00:00",0x40 土), S(2,"23:59",0x20 金), S(3,"00:00",0x20 金), S(4,"23:59",0x40 土)]` | `dueSchedules(list, 29839139, 29839141, out, 10)` | 戻り値 2、`out[0]={2, 29839139}`、`out[1]={1, 29839140}`（金 00:00・土 23:59 は入らない） |
| TC-N54 | [F4] | 07:00・毎日の件を3件 | `dueSchedules(list, M0, M0, out, 2)` | 戻り値 2、一覧の先頭2件の id |

#### B-2. 1件の検証（validateSchedule）

前提：基準の件 `B = {id=0, enabled=true, hour=7, minute=0, days=0x3E, target=Ac, ac={power=true, mode=Heat, tempC=20}}`。各ケースは B の一部だけを変える。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N55 | [F4] | B、および `{target=Light, light=Full}` の件 | `validateSchedule` | どちらも `None` |
| TC-N56 | [F4] | B の hour/minute を (0,0)、(23,59) に | `validateSchedule` | どちらも `None` |
| TC-N57 | [F4] | B の hour を -1、24、minute を -1、60 に（4通り） | `validateSchedule` | 4つとも `TimeOutOfRange` |
| TC-N58 | [F4] | B の id を 0、`kScheduleIdMax`（9999）、`kScheduleIdMax+1`（10000） | `validateSchedule` | `None`、`None`、`IdOutOfRange` |
| TC-N59 | [F4] | B の days を 0、0x80、0xFF、0x01、0x7F | `validateSchedule` | `NoDays`、`NoDays`、`NoDays`、`None`、`None` |
| TC-N60 | [F4] | B の `ac.power` を空に | `validateSchedule` | `AcPowerMissing` |
| TC-N61 | [F4] | `ac={power=false, mode=Cool}`、および `ac={power=false}` | `validateSchedule` | `AcStopHasOtherFields`、`None` |
| TC-N62 | [F4] | `ac={power=true, tempC=20}`（mode 無し） | `validateSchedule` | `AcTempWithoutMode` |
| TC-N63 | [F4] | `cap::tempSupported(m)` が false のモード m（無ければ IGNORE）で `ac={power=true, mode=m, tempC=kTempMinC}` | `validateSchedule` | `AcTempNotSupported` |
| TC-N64 | [F4] | `ac.tempC` を `kTempMinC-1`（15）、`kTempMinC`（16）、`kTempMaxC`（30）、`kTempMaxC+1`（31） | `validateSchedule` | `AcTempOutOfRange`、`None`、`None`、`AcTempOutOfRange` |
| TC-N65 | [F4] | `ac.fan`／`swingV`／`swingH` に選択肢外の値（方針2） | `validateSchedule` | `AcFanNotSupported`、`AcSwingVNotSupported`、`AcSwingHNotSupported` |
| TC-N66 | [F4] | B の `enabled=false`、hour=24 | `validateSchedule` | `TimeOutOfRange`（無効でも検証する） |
| TC-N67 | [F4] | B の hour=24、days=0 | `validateSchedule` | `TimeOutOfRange`（順：時刻が曜日より先） |
| TC-N68 | [F4] | — | `errorMessage(ScheduleError)` を14値で | D-03 6.4 の表どおり（`""`、`too many schedules`、`id out of range`、`duplicate id`、`time out of range`、`no days`、`power required`、`ac stop takes only power`、`temp requires mode`、`temp not supported in this mode`、`temp out of range`、`fan not supported`、`swingV not supported`、`swingH not supported`） |

#### B-3. 一覧の置き換えと採番（ScheduleList::replaceAll）

前提：`ScheduleList l;` を新しく作る（`nextId_=1`）。`nextId_` は非公開なので、次に id 無しの件を1件足したときに付く id で確かめる。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N69 | [F4-LIMIT] | 空 | id 無しの有効な件を `kScheduleMax`（10）件 `replaceAll` | `None`、`size()=10`、id は 1〜10 |
| TC-N70 | [F4-LIMIT] | 2件入れた後（id 1, 2） | 有効な件を `kScheduleMax+1`（11）件 `replaceAll` | `TooMany`、`badIndex=-1`、`size()=2`、id 1, 2 のまま |
| TC-N71 | [F4] | 2件入れた後 | 3件のうち添字2だけ hour=24 で `replaceAll` | `TimeOutOfRange`、`badIndex=2`、一覧は元の2件のまま |
| TC-N72 | [F4] | 2件入れた後 | id `[1, 2, 1]` で `replaceAll` | `DuplicateId`、`badIndex=2`、一覧は元のまま |
| TC-N73 | [F4] | `[無, 無]` で `[1, 2]`（次は 3） | `[7, 7]` で失敗させた後、`[1, 2, 無]` を `replaceAll` | 失敗は `DuplicateId`。次の件の id は 3（失敗で `nextId_` が 8 に進んでいない） |
| TC-N74 | [F4] | 空 | `[無, 無]` → 続けて `[1, 2, 無]` | 1回目 `[1, 2]`、2回目の新しい件は 3 |
| TC-N75 | [F4] | `[1, 2]`（次は 3） | `[1, 無]` → 続けて `[1, 3, 無]` | `[1, 3]`、次の新しい件は 4 |
| TC-N76 | [F4] [F4-IO] | 空 | `[5, 7]` → `[7, 無]` → `[7, 8, 無]` | `[5, 7]`、`[7, 8]`、次の新しい件は 9 |
| TC-N77 | [F4] [F4-IO] | 空 | `[5, 無]` → `[5, 6, 無]` | `[5, 6]`（入力 id の最大で先に進めてから採番）、次は 7 |
| TC-N78 | [F4] | 空 | `[9999]` → `[9999, 無]` → `[9999, 1, 無]` | `[9999]`、`[9999, 1]`、`[9999, 1, 2]`（使用中の 1 を飛ばす） |
| TC-N79 | [F4] | `[9999, 1]` を作った後 | `[9999, 1, 無]` → 続けて `[9999, 1, 2, 無]` | `[9999, 1, 2]`、次の新しい件は 3 |
| TC-N80 | [F4] | `[5, 7]` | `findById(7)`、`findById(6)` | 7 は hour/minute が入力どおりの件、6 は `nullptr` |
| TC-N81 | [F4] | — | 時刻 09:00, 07:00, 08:00 の順で `replaceAll` | `at(0)`〜`at(2)` の時刻が 09:00, 07:00, 08:00（並べ替えない） |

#### B-4. Hub でのスケジュール実行（FakeIrSender・FakeClock・FakeClimateSensor）

前提：`Hub hub(ir, clock, sensor);` を新しく作る。一覧は `hub.replaceSchedules(...)` で入れる。`tick` の `nowMs` は 1000 ずつ進める（明記したものを除く）。「確定させる」は、その時刻で `tick` を1回呼び `lastScheduleMin_` を作ること。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N82 | [N-STATE] [N-BOOT] | 生成直後 | `schedules().size()`、送信回数を読む | `size()=0`、`acCount=0`、`lightCount=0` |
| TC-N83 | [N-TIME] | 一覧 `[S(1,"07:00",0x7F,照明 Full)]`、FakeClock 未取得 | T0-60 から T0+60 まで 1 秒ずつ `tick`（121回） | `lightCount=0`、`acCount=0` |
| TC-N84 | [N-TIME] [N-BOOT] | 一覧 `[S(1,"07:00",0x7F,照明 Full), S(2,"07:01",0x7F,照明 Night)]` | 07:00:10 で初めて synced にして `tick` → 07:01:00 で `tick` | 1回目の後 `lightCount=0`（取得した分は実行しない）。2回目の後 `lightCount=1`、`lastLight=Night` |
| TC-N85 | [F4] | 一覧 `[S(1,"07:00",0x7F,照明 Full)]`、06:59:30 で確定 | 07:00:00〜07:00:59 を 1 秒ずつ 60 回 `tick` | `lightCount=1`（二重実行なし） |
| TC-N86 | [F4] [N-TIME] | synced | `tick(0)`、`tick(999)`、`tick(1000)` | `clock.nowCount` が 1、1、2 |
| TC-N87 | [F4] [N-TIME] | synced | `tick(0xFFFFFE0C)`、`tick(0x000001F3)`、`tick(0x000001F4)` | `nowCount` が 1、1、2（`millis()` の一周でも 1000ms で判定） |
| TC-N88 | [F4] | 一覧 `[S(1,"07:00",0x7F,照明 Full)]`、06:58:00 で確定 | 07:03:00 で `tick`（d=5） | `lightCount=1` |
| TC-N89 | [F4] | 同じ一覧、06:50:00 で確定 | 07:03:00 で `tick`（d=13） | `lightCount=0` |
| TC-N90 | [F4] | 同じ一覧、06:59:30 で確定、07:00:05 で `tick`（1回送る） | 06:59:50 → 07:00:10 → 07:01:00 の順に `tick` | `lightCount=1` のまま |
| TC-N91 | [F4] [F1] | 一覧 `[S(1,"07:00",0x7F,エアコン {power=true, mode=Heat, tempC=20})]`、06:59:30 で確定 | 07:00:00 で `tick` | `acCount=1`。`lastAc` と `acState()` がどちらも `power=true, mode=Heat, tempC=20, fan=Auto` |
| TC-N92 | [F4] [F1] | TC-N91 の後、一覧に `S(2,"07:01",0x7F,エアコン {power=false})` を足して置き換え | 07:01:00 で `tick` | `acCount=2`。`lastAc` が `power=false, mode=Heat, tempC=20`（運転だけが変わる） |
| TC-N93 | [F4] [F3] | 一覧 `[S(1,"07:00",0x7F,照明 Full)]`、06:59:30 で確定 | 07:00:00 で `tick` | `lightCount=1`、`lastLight=Full`、`acCount=0` |
| TC-N94 | [F4] | 一覧 `[S(1,"07:00",0x7F,エアコン {power=true}), S(2,"07:00",0x7F,照明 Half)]`、06:59:30 で確定 | 07:00:00 で `tick` | その1回の `tick` で `acCount=1`、`lightCount=1`、`lastLight=Half` |
| TC-N95 | [F4] | synced、`nowCount` を控える | `replaceSchedules` を有効な2件で呼ぶ | 戻り値 `None`、`nowCount` 変化なし、`acCount=0`、`lightCount=0` |
| TC-N96 | [F4] | 一覧は空、06:59:30 で確定、07:00:00 で `tick`（07:00 を判定済み） | 07:00:20 に `[S(1,"07:00",0x7F,照明 Full)]` へ置き換え、07:00:21・07:00:59・07:01:00 で `tick` | `lightCount=0` |
| TC-N97 | [F4] | 一覧 `[S(1,"07:00",0x7F,照明 Full)]`、06:59:59 で `tick`（nowMs=N） | `[S(2,"07:00",0x7F,照明 Night)]` に置き換え、07:00:00 で `tick`（nowMs=N+1000） | `lightCount=1`、`lastLight=Night`（古い一覧の Full は送られない） |
| TC-N98 | [F4] | 一覧 `[S(1,"07:00",0x7F,照明 Full)]`、06:59:30 で確定 | hour=24 の件で `replaceSchedules`（失敗）、07:00:00 で `tick` | 置き換えは `TimeOutOfRange`。`lightCount=1`、`lastLight=Full`（元の一覧で判定） |

#### B-5. JSON（schedule_json）

前提：`ScheduleParseResult r;`。「List」「Export」は `ScheduleJsonKind`。例の3件＝D-03 6.1 の3つの実例（id 1 エアコン暖房 20℃ 平日 07:00、id 2 エアコン停止 毎日 23:30、id 3 無効 照明 全灯 土日 06:45）。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N100 | [F4-IO] | 例の3件を `replaceAll` した一覧 A | `schedulesToJson(A, Export, 同期済み T20)` → `schedulesFromJson(…, Export, &r)` → 新しい一覧 B に `replaceAll` | すべて成功。B の3件の id（1, 2, 3）、enabled、時刻、days、target、ac の各項目、light が A と一致 |
| TC-N101 | [F4-IO] | 空の一覧 | `schedulesToJson(空, Export, T20)` を読み直す | `version=1`、`schedules` は空配列。`schedulesFromJson(…, Export)` は true、`count=0` |
| TC-N102 | [F4] | 例の3件を `{"schedules":[…]}` にした文字列 | `schedulesFromJson(…, List, &r)` | true、`count=3`。[0]：id 1、days 0x3E、`ac={power=true, mode=Heat, tempC=20}`。[1]：id 2、days 0x7F、`ac.power=false` だけ。[2]：id 3、`enabled=false`、06:45、days 0x41、target Light、`light=Full` |
| TC-N103 | [F4] | days=0x41（土・日）の件 | `schedulesToJson(List)` | その件の `days` が `["sun","sat"]`（日曜始まり） |
| TC-N104 | [F4] | 例の3件 | `schedulesToJson(List)` の文字列 | `"action":{"power":true,"mode":"heat","temp":20}`、`"action":{"power":false}`、`"action":{"button":"full"}` を部分文字列として含む（値のあるキーだけ、順は power, mode, temp, fan, swingV, swingH） |
| TC-N105 | [F4-LIMIT] | 空の一覧 | `schedulesToJson(List)` を読み直す | `max=kScheduleMax`（10）、`schedules=[]`、`version` キーが無い |
| TC-N106 | [F4-IO] | 同期済み local 2026-09-25 20:00:00、および 2026-01-05 07:03:09 | `schedulesToJson(Export)` の `exportedAt` | `"2026-09-25T20:00:00+09:00"`、`"2026-01-05T07:03:09+09:00"`（ゼロ埋め）。`max` キーが無い |
| TC-N107 | [F4-IO] [N-TIME] | 未取得 `{false,{},0}` | `schedulesToJson(Export)` | `exportedAt` が null |
| TC-N108 | [F4-IO] | 同期済み 2026-09-25 20:00、2026-01-05 07:03、未取得 | `exportFilename` | `irhub-schedules-20260925-2000.json`、`irhub-schedules-20260105-0703.json`、`irhub-schedules.json` |
| TC-N109 | [F4-IO] | 正しい Export 本文を空白で 8192 バイトちょうど、8193 バイトに伸ばしたもの | `schedulesFromJson(…, Export)` | 8192：true。8193：false、`r.error="body too large"` |
| TC-N110 | [F4-IO] | — | 本文 `{`、`""`（空）、`[]` を Export で | 3つとも false、`invalid json` |
| TC-N111 | [F4-IO] | — | `{"schedules":[]}` を Export で | false、`version missing` |
| TC-N112 | [F4-IO] | — | `{"version":2,"schedules":[]}`、`{"version":"1","schedules":[]}` を Export で | どちらも false、`unsupported version` |
| TC-N113 | [F4-IO] | — | `{"version":1}`、`{"version":1,"schedules":{}}` | どちらも false、`schedules missing` |
| TC-N114 | [F4-LIMIT] [F4-IO] | 正しい件を `kScheduleMax+1`（11）件、および `kScheduleMax`（10）件 | Export で読む | 11件：false、`too many schedules (max 10)`（`kScheduleMax` から `snprintf`）。10件：true、`count=10` |
| TC-N115 | [F4-IO] | 1件目の `time` を `"7:00"`、`"07:00:00"`、`"24:00"`、`"07:60"` に | Export で読む | 4つとも false、`schedules[0].time: must be HH:MM` |
| TC-N116 | [F4-IO] | 1件目の days を `["mo"]`、`["mon","mon"]`、`[]` に | Export で読む | `schedules[0].days: unknown day "mo"`、`schedules[0].days: duplicate day`、`schedules[0].days: empty` |
| TC-N117 | [F4-IO] | 1件目の `target` を `"tv"` に | Export で読む | false、`schedules[0].target: unknown target` |
| TC-N118 | [F4-IO] | 1件目の action を `{"power":true,"tmp":20}` に | Export で読む | false、`schedules[0].action: unknown key "tmp"` |
| TC-N119 | [F4-IO] | 1件目の action を `{"power":true,"mode":"heat","temp":26.5}` に | Export で読む | false、`schedules[0].action.temp: must be integer` |
| TC-N120 | [F4-IO] | 1件だけの Export 本文を2つ作る：(a) 例の3件の id 3（照明）だけを入れ、その action を `{"button":"on"}` に、(b) 例の3件の id 1（エアコン）だけを入れ、その action を `{"power":true,"fan":"turbo"}` に | それぞれ Export で読む | (a) false、`schedules[0].action.button: unknown button`。(b) false、`schedules[0].action.fan: unknown fan`（どちらも1件だけなので添字は 0） |
| TC-N121 | [F4-IO] | 1件目から `time` を消したもの、`enabled` を `"yes"` にしたもの | Export で読む | `schedules[0].time: missing`、`schedules[0].enabled: must be boolean` |
| TC-N122 | [F4-IO] | `schedules` の添字2を数値 `5` に | Export で読む | false、`schedules[2]: not an object` |
| TC-N123 | [F4] [F4-IO] | 1件目の action を `{"power":true,"temp":20}`、`{"power":false,"mode":"cool"}`、`{"power":true,"mode":"cool","temp":31}`（`kTempMaxC+1`） | Export で読む | `schedules[0]: temp requires mode`、`schedules[0]: ac stop takes only power`、`schedules[0]: temp out of range` |
| TC-N124 | [F4-IO] | 1件目の id を `0`、`-1`、`10000`、`70000` | Export で読む | 4つとも false、`schedules[0].id: out of range`（70000 が 4464 として通らない） |
| TC-N125 | [F4-IO] | 1件目の id を `1.5`、`"1"`、`true`、`null` | Export で読む | 4つとも false、`schedules[0].id: must be integer` |
| TC-N126 | [F4-IO] | id を `1`、`9999`、キー無し | Export で読む | true。`items[0].id` が 1、9999、0 |
| TC-N127 | [F4] | `{"max":3,"schedules":[正しい件]}`（version 無し）、`{"version":1,"foo":1,"schedules":[正しい件]}` | 前者を List、後者を Export で読む | どちらも true、`count=1`（List は version 不要・max は無視、一番外側の知らないキーは無視） |
| TC-N128 | [F4-IO] | 1件目は正しく、2件目の time が `"7:00"` | Export で読む | false、`schedules[1].time: must be HH:MM`（添字が2件目を指す） |
| TC-N129 | [F4-IO] | 1件目の id を `10000000000`（`int` に入らない整数。D-03 6.1 の id の手順4、要確認の確認を兼ねる） | Export で読む | false。`r.error` が `schedules[0].id: out of range`（`is<long long>()` が使える場合）または `schedules[0].id: must be integer`（手順4を省いた場合）のどちらか。どちらだったかをテストの出力（`TEST_MESSAGE`）に残す |
| TC-N182 | [F4-IO] | エアコンの件の action を `{"mode":"heat","temp":20}`（`power` 無し） | Export で読む | false、`schedules[0].action.power: missing`（6.1 の表で `power` は必須。6.3 の順7） |
| TC-N183 | [F4-IO] | 1件目（`action` の外）に知らないキー `"note":"x"` を足す | Export で読む | false。`r.error` が `schedules[0]` で始まる（6.1「1件の中の表に無いキーは 400」。文言の残りは設計に無い：設計の不足5） |
| TC-N197 | [F4-IO] [N-TIME] | `LocalTime` を 2026-01-05 07:03:09、および 2026-09-25 20:00:00 に | `formatJstIso(t)` | `"2026-01-05T07:03:09+09:00"`（1桁の月・日・時・分・秒がゼロ埋め）、`"2026-09-25T20:00:00+09:00"`（D-03 6節） |
| TC-N198 | [F4-IO] | 同期済み local 2026-01-05 07:03:09 の `ClockReading` を now とし、例の3件の一覧 | `schedulesToJson(一覧, Export, now)` を読み直した `exportedAt` と、`formatJstIso(now.local)` を比べる | 2つが同じ文字列（`"2026-01-05T07:03:09+09:00"`。`exportedAt` は `formatJstIso` で作る。D-03 6.2） |

（TC-N99 は欠番）

### C. test/test_api（T-04：Hub の基本動作と ApiRouter）

前提（全ケース共通）：`Hub hub(ir, clock, sensor); ApiRouter api(hub);` を新しく作る。FakeClock は未取得、FakeClimateSensor の `next={true, 25.0, 50.0}`。`req(M, path, body)` は `ApiRequest{M, path, body}`。応答本文は ArduinoJson で読み直して比べる。

#### C-1. Hub（N-BOOT・N-STATE・F5・F1・F3）

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N130 | [N-BOOT] | 生成直後、FakeClock を同期済み T0 に | `tick` を 0, 1000, …, 9000 で10回 | `acCount=0`、`lightCount=0`（一覧が空なら送らない） |
| TC-N131 | [N-STATE] [F2] | 生成直後 | `acState()`、`lastClimate()` | `acState()` が TC-N01 と同じ値。`lastClimate().valid=false` |
| TC-N132 | [F5] | 生成直後 | `tick(0)`、`tick(29999)`、`tick(30000)` | `readCount` が 1、1、2（初回は即読む、`kClimateIntervalMs`=30000） |
| TC-N133 | [F5] | 生成直後 | `tick(0xFFFFFFF0)`、`tick(0x0000751F)`、`tick(0x00007520)` | `readCount` が 1、1、2（差 29999 では読まず 30000 で読む） |
| TC-N134 | [F5] | `tick(0)` で `{true, 24.5, 55.0}` を読ませる | `next={false,0,0}` にして `tick(30000)` | `lastClimate().valid=false` |
| TC-N135 | [F1] | 運転中：生成直後に `applyAc({power=true}, &err)`（`acCount=1`） | `applyAc({tempC=27}, &err)` | true、`acCount=2`。`lastAc` の全7項目が `acState()` と一致（`power=true, mode=Cool, hasTemp=true, tempC=27, fan=Auto, swingV=Off, swingH=Off`。変えていない項目も入っている） |
| TC-N136 | [F1] | 生成直後 | `applyAc({tempC=cap::kTempMaxC+1}, &err)` | false、`err="temp out of range"`、`acCount=0`、`acState()` は初期値のまま |
| TC-N137 | [F1] | 生成直後に `applyAc({power=true}, &err)`（`acCount=1`）、その後 `ir.acResult=false` | `applyAc({tempC=27}, &err)` | true、`acCount=2`、`acState().tempC=27`（戻さない） |
| TC-N138 | [F1] | 運転中：生成直後（冷房）に `applyAc({power=true}, &err)`（`acCount=1`） | `applyAc({mode=Cool}, &err)` | true、`acCount=2`（運転中は同じ値のパッチでも送信1回） |
| TC-N139 | [F3] | 生成直後 | `pressLight(Night)`。続けて `ir.lightResult=false` にして `pressLight(Full)` | 1回目：true、`lightCount=1`、`lastLight=Night`。2回目：false、`lightCount=2`、`lastLight=Full` |
| TC-N140 | [F3] | — | `parseLightButton` に `power` `night` `brighter` `dimmer` `full` `half`、`on` `Power` `""` | 6つは `Power`〜`Half` になり `toString` で元に戻る。`on`・`Power`・`""` は `nullopt` |
| TC-N185 | [F1] | 生成直後（`power=false`）。`cap::kSwingVChoices` のうち `kSwingVDefault` と違う値 v を探す（今は Auto。無ければ4回目を省く） | `applyAc({mode=Heat})`、`applyAc({tempC=27})`、`applyAc({fan=High})`、`applyAc({swingV=v})` を順に | 4回とも true。毎回の後で `acCount=0`（停止中に power を含まないパッチは送らない。D-02 6節）。最後の `acState()` が `power=false, mode=Heat, hasTemp=true, tempC=27, fan=High, swingV=v（Auto）, swingH=cap::kSwingHDefault`（今は Off） |
| TC-N186 | [F1] | TC-N185 の4回の後 | `applyAc({power=true})` | true、`acCount=1`。`lastAc` が `power=true, mode=Heat, hasTemp=true, tempC=27, fan=High, swingV=v（Auto）, swingH=cap::kSwingHDefault`（今は Off。停止中の変更がまとめて送られる）で、`acState()` と全7項目一致 |
| TC-N187 | [F1] [F2] | 生成直後（`power=false`） | `applyAc({mode=Heat})` → `({tempC=27})` → `({fan=High})` → `({mode=Cool})` → `({mode=Heat})` | 5回とも true、`acCount=0` のまま。4回目の後 `acState()` が `mode=Cool, tempC=26, fan=Auto`、5回目の後 `mode=Heat, tempC=27, fan=High`（停止中の変更がモード別の記憶に入っている。`Hub` にはモード別の記憶を読む関数が無い（D-02 テスト観点）ので、モード切替で `acState()` に出る値で確かめる） |
| TC-N188 | [F1] | 生成直後（`power=false`） | `applyAc({power=false})` → `applyAc({power=false, tempC=24})` | 1回目：true、`acCount=1`、`lastAc` が `power=false, mode=Cool, tempC=26, fan=Auto`（今と同じ値でも power を含むので送る）。2回目：true、`acCount=2`、`lastAc.power=false`、`lastAc.tempC=24` |
| TC-N189 | [F1] | 生成直後に `applyAc({power=true})`（`acCount=1`） | `applyAc({power=false})` → `applyAc({tempC=25})` | 1回目：true、`acCount=2`、`lastAc.power=false`。2回目：true、`acCount=2` のまま、`acState().tempC=25`、`lastAc.tempC=26`（停止にした後の変更は送らない） |
| TC-N190 | [F1] | 生成直後に `applyAc({power=true})`（`acCount=1`） | `applyAc({tempC=cap::kTempMaxC+1}, &err)`（今は 31） | false、`err="temp out of range"`、`acCount=1` のまま、`acState()` が `power=true, mode=Cool, tempC=26, fan=Auto`（運転中の失敗も送信 0 回。停止中の失敗は TC-N136） |
| TC-N191 | [F1] [F2] | 生成直後 | D-02 4節の遷移例の表の14行を上から順に `applyAc`（`{"temp":31}` の行は `kTempMaxC+1`）。各行の前後で `acCount` の差を取る | 差が上から `1,1,1,1,1,1,0,0,1,0,0,1,1,0`（表の「送信」欄どおり）、最後の `acCount=9`。最後の `lastAc` が `power=true, mode=Cool, tempC=27, fan=Low` |
| TC-N193 | [F1] | 生成直後（`power=false`、`acCount=0`） | `applyAc({power=true, tempC=cap::kTempMaxC+1}, &err)`（今は 31） | false、`err="temp out of range"`、`acCount=0` のまま、`acState()` が `power=false, mode=Cool, tempC=26, fan=Auto`（停止中でも power を含むパッチの検証失敗は送信 0 回・状態不変。D-02 6節） |
| TC-N194 | [F1] [F2] | 生成直後（`power=false`、冷房 26℃）。`cap::kSwingVChoices` のうち `kSwingVDefault` と違う値 v を探す（今は Auto。無ければ4回目を省く） | D-02 テスト観点の手順どおり、同じ `Hub` で `applyAc({tempC=27})` → `({mode=Heat})` → `({fan=High})` → `({swingV=v})` | 4回とも true、毎回の後で `acCount=0`。4回の後の `acState()` が `power=false, mode=Heat, hasTemp=true, tempC=20`（暖房の記憶を復元。冷房で変えた 27 は入らない）`, fan=High, swingV=v（Auto）, swingH=cap::kSwingHDefault`（今は Off） |
| TC-N195 | [F1] [F2] | TC-N194 の4回の後（同じ `Hub`） | `applyAc({power=true})` | true、`acCount=1`。`lastAc` が `power=true, mode=Heat, hasTemp=true, tempC=20, fan=High, swingV=v（Auto）, swingH=cap::kSwingHDefault`（停止中の変更がまとめて入る。27 は冷房の記憶なので入らない）で、`acState()` と全7項目一致 |
| TC-N196 | [F1] [F2] | TC-N195 の後（同じ `Hub`、運転中） | `applyAc({mode=Cool})` | true、`acCount=2`。`lastAc` が `power=true, mode=Cool, hasTemp=true, tempC=27, fan=Auto, swingV=cap::kSwingVDefault`（今は Off）`, swingH=cap::kSwingHDefault`（今は Off）（停止中に変えた冷房の 27 が残っていることを `Hub` から見える値で確かめる） |

#### C-2. ルーティング

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N141 | [API] | 生成直後 | `GET /api/status`、`POST /api/ac {"power":true}`、`POST /api/light {"button":"power"}`、`GET /api/schedules`、`PUT /api/schedules {"schedules":[]}`、`GET /api/schedules/export`、`POST /api/schedules/import {"version":1,"schedules":[]}` | 7つとも `status=200`、`contentType="application/json"` |
| TC-N142 | [API] | 生成直後 | `GET /api/foo`、`GET /api/status/`、`GET /favicon.ico` | 3つとも 404、本文 `{"error":"not found"}` |
| TC-N143 | [API] | 生成直後 | `POST /api/status`、`GET /api/ac`、`PUT /api/light`、`Other /api/schedules`、`GET /api/schedules/import`、`POST /api/schedules/export` | 6つとも 405、`{"error":"method not allowed"}`。`Other /api/foo` は 404 |
| TC-N144 | [API] | 生成直後 | TC-N142・N143 の要求をすべて送る | `acCount=0`、`lightCount=0`、`acState()` 初期値のまま、一覧 0 件 |
| TC-N200 | [API] | 生成直後 | `POST /`、`PUT /`、`Other /`（本文はどれも `{}`） | 3つとも 404（405 ではない）、本文 `{"error":"not found"}`。`acCount=0`、`lightCount=0`（`/` の GET 以外は ApiRouter に来て「それ以外」＝404。D-04 10節） |

#### C-3. GET /api/status

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N145 | [API] [F2] [N-TIME] [F5] [N-BOOT] | 生成直後（`tick` を呼ばない） | `GET /api/status` | 200。`ac={"power":false,"mode":"cool","temp":26,"fan":"auto","swingV":"off","swingH":"off"}`、`climate={"valid":false,"temperature":null,"humidity":null}`、`clock={"synced":false,"now":null}`。`acCount=0` |
| TC-N146 | [API] [F1] | 生成直後 | `GET /api/status` の `acCapabilities` | `modes=["auto","cool","dry","heat"]`、`tempMin=kTempMinC`(16)、`tempMax=kTempMaxC`(30)、`tempStep=1`、`tempModes`＝`tempSupported` が true のモード（今は4つ）、`fan`＝`kFanChoices` の順（今は `["auto","low","medium","high"]`）、`swingV`（今は `["off","auto"]`）、`swingH`（今は `["off"]`）。`timer` などのキーが無い |
| TC-N147 | [N-TIME] | FakeClock を同期済み local 2026-01-05 07:03:09 に | `GET /api/status` | `clock.synced=true`、`clock.now="2026-01-05T07:03:09+09:00"` |
| TC-N148 | [F5] | `next={true, 24.46, 55.24}` で `tick(0)` | `GET /api/status` | `climate.valid=true`、`temperature=24.5`、`humidity=55.2` |
| TC-N149 | [F5] | TC-N148 の後、`next={false,0,0}` で `tick(30000)` | `GET /api/status` | `climate.valid=false`、`temperature` と `humidity` が null |
| TC-N150 | [F1] | `tempSupported(m)` が false のモード m（無ければ IGNORE）へ `POST /api/ac {"mode":m}` | `GET /api/status` | `ac.temp` が null |
| TC-N151 | [API] | 生成直後 | `GET /api/status` を3回 | `acCount=0`、`lightCount=0`、`acState()` 初期値のまま |
| TC-N199 | [API] [N-TIME] [F4-IO] | FakeClock を同期済み local 2026-01-05 07:03:09 に固定（`seq` は空。何回呼んでも同じ時刻） | `GET /api/status` と `GET /api/schedules/export` | `clock.now` と export 本文の `exportedAt` がどちらも `"2026-01-05T07:03:09+09:00"` で同じ文字列（どちらも `formatJstIso` で作る。D-04 11節） |

#### C-4. POST /api/ac

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N152 | [F1] [API] | 生成直後に `POST /api/ac {"power":true}`（`acCount=1`） | `POST /api/ac {"temp":27}` | 200、`ac.power=true`、`ac.temp=27`、`mode`・`fan`・`swingV`・`swingH` は初期値。`acCount=2`、`lastAc` が `acState()` と一致 |
| TC-N153 | [F1] | 生成直後 | `{"power":true,"mode":"cool","temp":26,"fan":"auto","swingV":"auto","swingH":"off"}` | 200、応答の `ac` が送った6項目と同じ値 |
| TC-N154 | [F1] [F2] | `{"temp":27}` を送った後 | `{"mode":"heat"}` → `{"mode":"cool"}` | 1回目 `ac.mode="heat"`、`ac.temp=20`。2回目 `ac.mode="cool"`、`ac.temp=27` |
| TC-N155 | [F1] [API] | 生成直後 | `{"temp":26.5}`、`{"temp":"26"}`、`{"temp":null}`、`{"temp":true}` | 4つとも 400、`{"error":"temp: must be integer"}` |
| TC-N156 | [F1] [API] | 生成直後 | `{"power":1}`、`{"mode":3}`、`{"mode":"Cool"}` | `power: must be boolean`、`mode: must be string`、`mode: unknown mode`（400） |
| TC-N157 | [F1] [API] | 生成直後 | `{"fan":1}`、`{"fan":"turbo"}`、`{"swingV":"up"}`、`{"swingH":"x"}`、`{"swingV":2}` | `fan: must be string`、`fan: unknown fan`、`swingV: unknown swingV`、`swingH: unknown swingH`、`swingV: must be string`（400） |
| TC-N158 | [F1] [API] | 生成直後 | `{"timer":60}`、`{"power":true,"tmp":1}` | `unknown key "timer"`、`unknown key "tmp"`（400。本体タイマーを作っていない） |
| TC-N159 | [F1] [API] | 生成直後 | `{}`、`{"temp":31}`（`kTempMaxC+1`）、`{"temp":15}`（`kTempMinC-1`）、`{"fan":"min"}`、`{"swingV":"highest"}`、`{"swingH":"auto"}`（選択肢外の文字列は方針2で探す） | `no ac fields`、`temp out of range`、`temp out of range`、`fan not supported`、`swingV not supported`、`swingH not supported`（400） |
| TC-N160 | [F1] | 生成直後 | TC-N155〜N159 の要求を全部送る | `acCount=0`、`acState()` 初期値のまま |
| TC-N161 | [API] | 生成直後 | `{"x":1,"temp":"a"}`、`{"fan":"turbo","temp":"a"}` | `unknown key "x"`（知らないキーが先）、`temp: must be integer`（power, mode, temp, fan … の順） |
| TC-N162 | [API] | 生成直後 | 本文 `""`、`[1]`、`"x"`、`{"temp":` | 4つとも 400、`invalid json` |
| TC-N163 | [API] | 生成直後に `POST /api/ac {"power":true}`（`acCount=1`） | `{"temp":27}` を空白で 512 バイトにしたもの、513 バイトにしたもの | 512：200、`ac.temp=27`、`acCount=2`。513：400、`body too large`、`acCount` は 2 のまま |
| TC-N164 | [F1] | 生成直後に `POST /api/ac {"power":true}`（`acCount=1`）、その後 `ir.acResult=false` | `POST /api/ac {"temp":27}` | 200、`ac.power=true`、`ac.temp=27`、`acCount=2`（sendAc が呼ばれ false を返しても 200） |
| TC-N192 | [F1] [API] | 生成直後（`power=false`） | `POST /api/ac {"temp":27}` → `POST /api/ac {"power":true}` | 1回目：200、`ac.power=false`、`ac.temp=27`、`acCount=0`（停止中は状態だけ変えて送らない。応答の形は運転中と同じ）。2回目：200、`ac.power=true`、`ac.temp=27`、`acCount=1`、`lastAc.power=true`、`lastAc.tempC=27` |

#### C-5. POST /api/light

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N165 | [F3] [API] | 生成直後 | `{"button":"power"}`〜`{"button":"half"}` の6つを順に | 6つとも 200、本文 `{"ok":true}`。毎回 `lightCount` が1増え、`lastLight` が Power, Night, Brighter, Dimmer, Full, Half の順に一致。本文に点灯状態のキーが無い |
| TC-N166 | [F3] [API] | 生成直後 | `{"button":"on"}`、`{"button":"Night"}`、`{"button":1}`、`{}`、`{"button":"night","x":1}` | `button: unknown button`、`button: unknown button`、`button: must be string`、`button: missing`、`unknown key "x"`（400）。`lightCount=0` |
| TC-N167 | [F3] | `ir.lightResult=false` | `{"button":"full"}` | 200、`{"ok":true}` |
| TC-N168 | [API] | 生成直後 | 本文 `{"button":`、513 バイトの本文 | `invalid json`、`body too large`（400）。`lightCount=0` |

#### C-6. スケジュール API

「有効な件」＝`{"enabled":true,"time":"07:00","days":["mon"],"target":"ac","action":{"power":true}}`。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N169 | [F4] [F4-LIMIT] | 生成直後 | `GET /api/schedules` | 200、`{"max":10,"schedules":[]}`（max は `kScheduleMax`） |
| TC-N170 | [F4] | 生成直後 | `PUT` で id 無しの有効な件2件 → `GET` | PUT：200、`schedules[0].id=1`、`[1].id=2`。GET も同じ内容 |
| TC-N171 | [F4-LIMIT] | TC-N170 の2件 | `PUT` で有効な件を `kScheduleMax+1`（11）件 | 400、`too many schedules (max 10)`。`GET` は元の2件（id 1, 2） |
| TC-N172 | [F4] | TC-N170 の2件 | `PUT` で1件目の time が `"24:00"` | 400、`schedules[0].time: must be HH:MM`。一覧は変わらない |
| TC-N173 | [F4] | TC-N170 の2件 | `PUT` で id 1 の件を2つ | 400、`schedules[1]: duplicate id`。一覧は変わらない |
| TC-N174 | [F4-IO] | 例の3件（B-5）を PUT 済み。PUT の後に FakeClock の `seq` を `[同期済み local 2026-09-25 20:00:59, 同期済み local 2026-09-25 20:01:00]` にする（呼ぶたびに1秒進む時計） | `GET /api/schedules/export` | 200、`contentType="application/json"`、本文 `version=1`、3件。`downloadFilename="irhub-schedules-20260925-2000.json"` かつ `exportedAt="2026-09-25T20:00:59+09:00"`（ファイル名と `exportedAt` が同じ1回の時刻から作られる。D-04 8節。2回取ると片方が 20:01 になって一致しない） |
| TC-N175 | [F4-IO] [N-TIME] | FakeClock 未取得 | `GET /api/schedules/export` | 200、`downloadFilename="irhub-schedules.json"`、`exportedAt` が null |
| TC-N176 | [F4-IO] | TC-N174 の本文を控え、`PUT {"schedules":[]}` で空にする | 控えた本文をそのまま `POST /api/schedules/import` → `GET` | import：200、`max=10` と3件。GET の3件が id 1, 2, 3 を含め元と一致 |
| TC-N177 | [F4-IO] | 2件入った一覧 | import に `{"schedules":[]}`、`{"version":2,"schedules":[]}` | `version missing`、`unsupported version`（400）。`GET` は元の2件 |
| TC-N178 | [F4-IO] | id 1, 2 の2件 | `{"version":1,"schedules":[有効な件に "id":5]}` を import → `GET` | 200。一覧は id 5 の1件だけ（混ぜずに置き換える） |
| TC-N179 | [N-TIME] [F4] | FakeClock 未取得 | GET schedules、PUT（2件）、GET export、POST import | 4つとも 200 |
| TC-N180 | [F4] [N-BOOT] | FakeClock 同期済み | TC-N169〜N179 の要求をすべて送る | `acCount=0`、`lightCount=0` |
| TC-N184 | [F4] [F4-IO] [API] | TC-N170 の2件（id 1, 2） | `PUT` を3通り：(a) 1件目の id が `10000000000`、(b) 1件目の action が `{"mode":"heat"}`（`power` 無し）、(c) 1件目に知らないキー `"note":"x"`。それぞれの後に `GET` | 3つとも 400。(a) の `error` は `schedules[0].id: out of range` か `schedules[0].id: must be integer` のどちらか、(b) は `schedules[0].action.power: missing`、(c) は `schedules[0]` で始まる。3回とも `GET` が元の2件（id 1, 2、内容も同じ） |
| TC-N201 | [F4-IO] [API] | TC-N170 の2件（id 1, 2） | `POST /api/schedules/import` に `{"version":1,"schedules":[有効な件に "id":3, 有効な件に "id":3]}` → `GET` | 400、`{"error":"schedules[1]: duplicate id"}`（`badIndex` から ApiRouter が組み立てる。D-03 6.3 順9）。`GET` は元の2件（id 1, 2） |

#### C-7. エラー本文と認証

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N181 | [API] [N-AUTH] | 生成直後 | `POST /api/ac {"tmp":1}`（400）、`GET /api/foo`（404）、`POST /api/status`（405）の本文を ArduinoJson で読む。あわせて、どの要求も認証の情報なし（`ApiRequest` は method・path・body だけ）で送る | 3つとも JSON として読め、キーは `error` の1つだけ。値は `unknown key "tmp"`（`"` を含む）、`not found`、`method not allowed`。認証の情報なしの `GET /api/status` が 200 |

---

## テストケース（実機）

実機ケースの使い方：各ゲート（H-0〜H-6）で、人がこの表を上から順に行い、各行の「期待結果」を満たせば ✓ を付ける。1つでも満たさなければ、そのゲートは fail とし、行の ID と見えた結果をメモに残す。書き込み（upload）は人が行う。シリアルモニタは `pio device monitor`（115200）。「IP」は ESP32 の IP（シリアルの `[wifi] connected ip=` の値。固定の確認は TC-H45）。

時刻の書き方：「T+2分」「U+4分」などの T・U は、その手順を始めたときの時計の **HH:MM**（秒は切り捨て）とし、その分に2（4）を足した HH:MM を登録する（例：T が 20:58:40 なら T=20:58、T+2分=21:00）。スケジュールは分単位で動く（D-03）ので、「T+2分に光る」は「画面や時計が T+2分の HH:MM になってから、その分の中で光る」の意味。秒の余裕が足りないと感じたら（その分の残りが 30 秒未満のとき）、次の分まで待ってから T を取り直す。

### フェーズ0（H-0）：ESP32 単体の動作確認

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H01 | [HW-PINS] | ESP32-DevKitC-VE を USB（A-microB）で PC につなぐ。他の部品はつながない | `pio run -e esp32 -t upload` → `pio device monitor` を開き 10 秒待つ | 1 秒ごとに1行（H-0 の手順では `irhub phase0 tick`）が出て、10 秒で 9〜11 行。文字化けしない（115200） |

### フェーズ1（H-1）：赤外線の受信と解析

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H02 | [HW-PINS] | 受信モジュール OSRB38C9AA：OUT→IO14、VCC→3V3、GND→GND（5V につながない） | `pio run -e dump -t upload` → monitor。照明のリモコンを 30cm から受光部へ向けて1回押す | 押すたびに受信の出力が出る。押していないときは何も出ない |
| TC-H03 | [F1] | TC-H02 のまま | OAR-N9 で「運転」を押す | プロトコル名（仮の予想 `HITACHI_AC424`）と、`Mode`・`Temp` を含む解析結果が出る。出力をそのまま控える |
| TC-H04 | [F1] | 同上 | ▼を下限まで、▲を上限まで1回ずつ押す | 各押下で `Temp` が 1℃ ずつ変わる。下限と上限の値を控える（仮は 16・30） |
| TC-H05 | [F1] | 同上 | モード（自動・冷房・除湿・暖房）、風量の全段階、風向 上下・左右の全選択肢を順に押す | 押した項目の値が出力で変わる。すべての値の名前を控える。除湿と自動で `Temp` が出るかを控える |
| TC-H06 | [F1] | 同上 | 本体タイマーの入・切を押す | タイマーの項目が解析結果に出るか（出る／出ない）を控える |
| TC-H07 | [F3] | 同上 | 照明の6ボタン（切/入・常夜灯・明るく・暗く・全灯・半灯）をそれぞれ3回押す | 6ボタンそれぞれで信号が記録され、同じボタンの3回が同じ値（生データなら長さと並びが同じ）。6ボタンの値が互いに違う |
| TC-H08 | [F1] | TC-H03〜H07 の出力 | `docs/hw/phase1-capture.md` に貼り、`/decide D1` を行う | ファイルにプロトコル、温度範囲、風量・風向の選択肢、除湿・自動の温度の有無、タイマーの有無、照明6ボタンの信号がすべてある |

### フェーズ2（H-2）：赤外線の送信

前提：I-06 の本物の送信層を書き込み済み。赤外線 LED 2個（IO4→1kΩ→2SC1815、5V→100Ω→LED）を配線済み。操作は `curl -X POST -H 'Content-Type: application/json' -d '<本文>' http://IP/api/ac`（照明は `/api/light`）。

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H09 | [F1] | — | `pio run -e esp32` | ビルドが成功し、`-Wswitch`（列挙の扱い漏れ）の警告が出ない |
| TC-H10 | [N-BOOT] [HW-PINS] | スマホのカメラで LED を映しておく。エアコン・照明の今の状態を控える | USB を抜き差しする（電源投入）→ 30 秒待つ。続けて EN ボタンでリセット → 30 秒待つ | どちらでも LED が一度も光らない。エアコン・照明の状態が変わらない。シリアルに `[boot]` の行とリセット理由が出る |
| TC-H11 | [F1] [HW-PINS] | エアコン停止中 | `{"power":true,"mode":"cool","temp":26}` | 応答 200。エアコンが冷房 26℃ で運転を始める |
| TC-H12 | [F1] | TC-H11 の後 | `{"temp":27}` → `{"power":false}` | 1回目でエアコンの表示が 27℃、2回目で停止する。どちらも応答 200 |
| TC-H13 | [F1] | 運転中、風向を控える | `{"temp":26}`、`{"temp":27}`、`{"temp":26}` を 5 秒おきに | 温度だけが変わり、上下風向が3回とも変わらない |
| TC-H14 | [F3] [HW-PINS] | 照明を消しておく | `/api/light` に `power`→`full`→`half`→`dimmer`→`brighter`→`night`→`power` を 3 秒おきに | 毎回応答 200 `{"ok":true}`。照明が 点灯→全灯→半灯→暗く→明るく→常夜灯→消灯 と、リモコンの同じボタンと同じに動く |
| TC-H15 | [F1] [F3] | ESP32 を実際の設置場所に置く。エアコンと照明を止めておく | エアコン：`{"power":true,"mode":"cool","temp":26}` → 10 秒後に `{"power":false}` を1組として、10 秒おきに3組（送信は計6回。どちらも power を含むので毎回送る）。照明：`/api/light` に `power` を 10 秒おきに6回 | エアコン：6回の送信のうち6回反応する（運転開始・停止を3回ずつ繰り返し、最後は停止）。照明：6回のうち6回反応する（点灯・消灯を3回ずつ繰り返し、最後は消灯）。応答はすべて 200（設置場所から両方に届く） |
| TC-H16 | [F2] | 運転中（冷房） | `{"mode":"heat"}` → `{"mode":"dry"}` → `{"mode":"auto"}` | 暖房 20℃、除湿（温度表示があれば 26℃）、自動で動く。除湿で温度が効いたか・自動の温度を控えて `/decide D2` |
| TC-H17 | [F1] [API] | 運転中 | `{"temp":99}` | 応答 400 `{"error":"temp out of range"}`。LED が光らず、エアコンは変わらない |
| TC-H68 | [F1] | エアコンと ESP32 がどちらも停止（例：TC-H12 の後）。スマホのカメラで LED を映す | `{"mode":"heat"}` → `{"temp":24}` → `{"fan":"high"}` を 5 秒おきに → 5 秒後に `{"power":true}` → 確かめたら `{"power":false}` | 最初の3回：応答 200、応答の `ac.power` が false、`ac` がそれぞれ `mode="heat"`、`temp=24`、`fan="high"` になる。LED が光らず、エアコンの受信音が鳴らず、エアコンは停止したまま（停止中に power を含まない変更は送らない。D-02 6節）。`{"power":true}`：LED が1回光り、エアコンが暖房・24℃・風量 強で運転を始める（停止中に変えた設定がまとめて届く）。最後の `{"power":false}` で停止する |
| TC-H69 | [F1] [N-BOOT] | エアコンを `{"power":true,"mode":"cool","temp":26}` で運転させておく。LED をカメラで映す | ESP32 の EN ボタンでリセット → 30 秒待つ → `curl http://IP/api/status` → `{"temp":25}` → 10 秒待つ | `/api/status` の `ac.power` が false（N-STATE）。`{"temp":25}` の応答は 200・`ac.power=false`・`ac.temp=25`。LED が光らず、エアコンは冷房 26℃ で運転を続ける（止まらない。人の判断の理由：再起動後に温度だけ変えて停止信号が出ないこと） |

### フェーズ3（H-3）：温湿度

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H18 | [F5] [HW-PINS] | DHT20：SDA→IO21、SCL→IO22（各 10kΩ で 3.3V へプルアップ）、VDD→3V3、0.1µF を VDD–GND | リセットしてシリアルを 70 秒見る | 起動から 1 秒以内に `[dht20] xx.xC xx.x% (NNms)` の1回目、その後 30 秒ごと（±1 秒）に1行。`NNms` が 100 ms 前後（D-06 3.2 の見積もり。値を記録する） |
| TC-H19 | [F5] | 室温計を DHT20 の横に 10 分置く | シリアルの値と室温計を3回（10 分おき）比べる | 室温と湿度がシリアルに出る。3回の差を数値で記録する（合否の閾値は設計に無い。設計の不足2） |
| TC-H20 | [F5] | 動いている状態 | SDA の線を抜いて 40 秒待ち、`GET http://IP/api/status` → 線を戻して 40 秒待ち、もう一度 | 抜いた後：シリアルに `[dht20] read error`、`climate.valid=false`、`temperature` と `humidity` が null。戻した後：`valid=true` で値が出る |

### フェーズ4（H-4）：画面と API（F1・F3・F5）＋起動・ネットワーク・時刻

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H21 | [UI] | スマホ（iOS Safari と Android Chrome）を家の Wi-Fi に | `http://IP/` を開く | 1ページに上から 上部バー（室温・湿度、時刻、NTP の状態）、エアコン、照明、スケジュールのカードが出る。日本語が化けない。題名が「IRハブ」。SwitchBot のロゴ・名称が無い |
| TC-H22 | [F1] [UI] | TC-H21 の画面、エアコン停止 | 運転ボタン → モード4つを順に → ＋を上限まで → −を下限まで → 風量・風向の各ボタン → 運転ボタン | 毎回エアコンが画面どおりに動き、画面の表示も変わる。温度が上限（今は 30℃）で＋、下限（今は 16℃）で−が押せなくなる。最後に停止する |
| TC-H70 | [F1] [UI] | TC-H21 の画面。エアコン停止（運転ボタンが「停止中」）。LED をカメラで映す | ＋を1回 → 5 秒後にモードの「暖房」→ 5 秒後に風量のボタンを今と違うもの1つ → 5 秒後に運転ボタン → 確かめたら運転ボタンでもう一度停止 | 最初の3回：押すたびに画面の表示が変えた値になり、`#ac-msg` に「停止中のため設定だけ変えました（運転を押すとまとめて送ります）」が出て約 3 秒で消える。LED が光らず、受信音が鳴らず、エアコンは停止したまま。運転ボタン：LED が1回光り、エアコンが画面の設定（暖房・画面の温度・選んだ風量）で運転を始める。このとき「停止中のため…」は出ない。最後の停止で止まる（D-05 のテスト観点 H-2） |
| TC-H23 | [F3] [UI] | 同上 | 照明の6ボタンを1つずつ | 照明がリモコンと同じに動き、`送信しました：<表示名>` が 3 秒出る。点灯中かどうかの表示は無い |
| TC-H24 | [F5] [UI] | 同上 | 室温・湿度を見る → DHT20 に息を 5 秒かける → 60 秒待つ | 小数1桁で表示される（例 `室温 24.5℃  湿度 55.2%`）。息をかけてから 60 秒以内に湿度の表示が上がる（センサー周期 30 秒＋画面の読み直し 30 秒の最悪値。D-05 の「30 秒以内」とは違う：設計の不足7） |
| TC-H25 | [N-RESP] | エアコンを運転中にしておく（停止中は＋で赤外線を送らない。D-02 6節）。スマホのスロー動画（240fps など）で、画面のボタンと LED を同時に映す | 照明ボタンを5回、エアコン＋を5回押す | 10回とも、指がボタンに触れたコマから LED が光り始めるコマまで 1.0 秒以内。＋の連打の後、エアコンは最後の温度になる |
| TC-H26 | [N-TIME] [UI] | ルーターがインターネットにつながっている | リセットして 60 秒後に画面と `/api/status` を見る | `clock.synced=true`、`now` がスマホの時計と ±5 秒以内で `+09:00`。画面の時刻と曜日が合い、「時刻同期済み」。警告の帯が無い |
| TC-H27 | [N-TIME] | ルーターの WAN 側を外す。LED をカメラで映す | リセットして画面を開く → スケジュールに 2 分後・毎日の照明 `full` を登録 → 予定の時刻を過ぎるまで待つ → WAN を戻し、シリアルに `[ntp] synced` が出るのを待つ | 未取得の間：上部に「時刻未取得のためスケジュールは実行されません」、`#clock` が `--:--`、予定の時刻に LED が光らない。`[ntp] synced` の後 30 秒以内に帯が消える |
| TC-H28 | [N-AUTH] | 家の LAN の PC のブラウザ | `http://IP/` を開き、照明ボタンを1つ押す | ログインやパスワードの入力なしで画面が開き、照明が動く |
| TC-H29 | [API] | PC の端末 | `curl 'http://IP/api/status?x=1'`、`curl -X PUT -H 'Content-Type: application/json' -d '{"schedules":[]}' http://IP/api/schedules`、`-H` を付けず `-d` だけで `curl -X POST -d '{"temp":27}' http://IP/api/ac`（curl が Content-Type を `application/x-www-form-urlencoded` にする。D-04 のテスト観点） | 1つ目 200（クエリは無視）、2つ目 200 `{"max":10,"schedules":[]}`、3つ目 400 `invalid json` |
| TC-H30 | [N-WIFI] [N-BOOT] | 接続中。シリアルを開き、LED をカメラで映す | ルーターの電源を切り、2 分待つ | シリアルの行の時刻（`at NNNms`）で判定する：`[wifi] lost at X ms` の直後に `attempt 1/3`、以後 `attempt 2/3`・`3/3` がそれぞれ前の `attempt` から約 10000ms 後、`giving up, restart at Y ms` の Y−X がおよそ 30000ms（D-01 6a の例：1000→31000）。続けて再起動し `[boot]` の行（リセット理由がソフトウェア再起動 `ESP_RST_SW`）。LED は光らない。（ルーターを切ってから `lost` が出るまでの遅れは判定に入れない） |
| TC-H31 | [N-WIFI] | 接続中 | ルーターを切り、`attempt 2/3` が出たら電源を戻す → つながったらもう一度切る | 再起動せずに `[wifi] connected ip=…`、画面が再び開ける。2回目の切断で `attempt 1/3` から数え直す |
| TC-H32 | [N-WIFI] | ルーターの電源を切っておく | ESP32 をリセットして 100 秒待つ → ルーターを戻す | 約 30 秒ごとに再起動を繰り返す（100 秒で3回前後）。ルーターを戻すと `connected` になり画面が開ける |
| TC-H33 | [N-WIFI] | TC-H30〜H32 のシリアルの記録 | 各 `[wifi] connected` について、その前にある直近の `[wifi]` の行を見る（間の `[dht20]`・`[ntp]` などの行は飛ばす） | すべての `connected` で、直前の `[wifi]` の行が `attempt n/3` である（`lost` の後に `attempt` を経ずに `connected` が出ていない＝自動再接続が働いていない） |
| TC-H34 | [N-STATE] | 画面でエアコンを暖房 22℃ にし、スケジュールを1件登録 | EN ボタンでリセットし、画面を開き直す | エアコンの表示が停止・冷房・26℃（初期値）、スケジュールが「スケジュールはありません」 |
| TC-H35 | [N-BOOT] | TC-H34 と同時に、LED をカメラで映す | リセット後 60 秒見る | LED が光らない。エアコン・照明が動かない |
| TC-H36 | [UI] | `web/index.html` の題名を一時的に1文字変える。別に `web/index.html` のコピーを取っておく | (1) `pio run -e esp32 -t upload` → 画面を再読み込み。(2) `include/secrets.h` を一時的に別名にして `pio run -e esp32`（終わったら戻す）。(3) `web/index.html` の途中に 0x00 のバイトを1つ入れて（例 `printf '\x00' >> web/index.html`）`pio run -e esp32` → 終了コードを `echo $?` で見る → 取っておいたコピーで元に戻して `pio run -e esp32` | (1) 変えた題名が出る（古い画面が残らない）。(2) ビルドが `#error` の「include/secrets.h がありません」で止まる。`git status` に `include/secrets.h` が出ない。(3) `embed_html.py` の検査（D-05 11節：0x00 を含む）に当たり、理由の1行が出て、コンパイルに進まずにビルドが失敗で止まる（PlatformIO の結果が `FAILED`、終了コードが 0 以外。D-05 11節 `sys.exit(1)` の要確認）。戻した後のビルドは成功する |
| TC-H60 | [N-TIME] [F4] | TC-H26 のとおり同期済み（`clock.synced=true`）。今の時刻を T とし、T+3分・毎日の照明 `full` を1件登録。照明は消しておき、LED をカメラで映す | ルーターの WAN 側のケーブルを抜く（家の Wi-Fi は生かす）→ 1 分待って `GET http://IP/api/status` と画面を見る → T+4分まで待つ | WAN を抜いた後も `clock.synced=true`、`now` が進んでいる（スマホの時計と ±5 秒以内）。画面に警告の帯が出ない。T+3分に LED が1回光り、照明が全灯になる（同期後に NTP が途切れてもスケジュールが動く。D-06 5.2） |
| TC-H61 | [N-TIME] [N-BOOT] [N-WIFI] | 同期済み（`clock.synced=true`）。シリアルを開く | ルーターの WAN 側のケーブルを抜く → ルーターの電源を切り、`[wifi] giving up, restart` と `[boot]` の行が出るのを待つ → WAN を抜いたままルーターの電源を戻す → `[wifi] connected` の後に `GET http://IP/api/status` と画面を見る → WAN を戻す | 再起動後（`ESP.restart()`、リセット理由 `ESP_RST_SW`）の `/api/status` が `clock.synced=false`、`clock.now=null`。画面に「時刻未取得のためスケジュールは実行されません」、`#clock` が `--:--`（前の起動の時刻を同期扱いにしない。D-06 5.2）。WAN を戻すと `[ntp] synced` が出て、その後 30 秒以内に帯が消える |
| TC-H65 | [N-RESP] | エアコンを運転中にしておく（D-02 6節）。TC-H25 と同じくスロー動画で画面のボタン・LED・シリアルモニタ（PC の画面）を同時に映す | (a) エアコンの＋を押し、すぐ（0.3 秒以内）に照明の `全灯` を押す、を3回。(b) 照明の `全灯` と `半灯` を 2 秒おきに交互に 70 秒押し続ける（その間に `[dht20]` の行が2回以上出る） | (a) 3回とも照明が全灯になる（送信が捨てられない）。照明の LED が光るのはエアコンの LED が消えた後。照明ボタンに触れてから照明の LED が光り始めるまでの時間を3回とも記録する（設計は「1 秒前後」で合否の値が無い：設計の不足8）。(b) すべての押下で、触れてから LED が光り始めるまで 1.0 秒以内（`[dht20]` の行が出た前後の押下も含む） |

### フェーズ5（H-5）：スケジュール

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H37 | [F4] | NTP 取得済み（帯が無い）。今の時刻を T とする | 画面で3件登録：T+2分 エアコン運転（暖房 20℃）、T+2分 照明 全灯、T+3分 エアコン停止。曜日は今日を含む | 一覧に3件が登録順で出る。T+2分の 0〜約1秒でエアコンが暖房 20℃ で動き、照明が全灯になる。T+3分でエアコンが止まる。LED はそれぞれの分で1回ずつ光り、同じ分に2回光らない。画面のエアコン表示も同じ状態になる |
| TC-H38 | [F4] | TC-H37 の3件。エアコンは停止している。LED をカメラで映す。操作を始める時刻を U とし、以下の操作は1分以内に終える | 1件目（エアコン運転）を「編集」で U+2分・冷房 26℃ に変える。2件目（照明 全灯）を「削除」。3件目（エアコン停止）を「編集」で U+4分 に変えてから、有効の切替で無効にする。U+5分まで LED とエアコンを見る | 各操作の後に一覧が描き直る。削除した件は一覧から消え、3件目は無効の表示で U+4分 になっている。U+2分に LED が1回光り、エアコンが冷房 26℃ で運転を始める。U+4分〜U+5分に LED が光らず、エアコンは運転したまま（無効にした停止の件が送られない） |
| TC-H39 | [F4-LIMIT] | 一覧を10件にする | 画面の追加ボタンを見る。続けて11件の本文で `curl -X PUT … /api/schedules` | 見出しが `10/10件`、追加ボタンが押せず「上限 10 件」。curl は 400 `{"error":"too many schedules (max 10)"}`、一覧は10件のまま |
| TC-H40 | [F4-IO] | 3件以上登録済み、NTP 取得済み | iOS Safari と Android Chrome のそれぞれで画面を開き「エクスポート」を押す（押した後はほかの操作をしない）→ 端末のファイル（iOS は「ファイル」アプリのダウンロード、Android は「ダウンロード」）を開く | 両方の端末で：「エクスポートしました：irhub-schedules-YYYYMMDD-HHMM.json」が約 3 秒出る。ページが移らず、画面に JSON の文字が表示されるだけにならない。ダウンロードに `irhub-schedules-YYYYMMDD-HHMM.json`（押した時刻の JST。画面の知らせと同じ名前）が1つ増える（応答を待った後の保存が止められない。D-05 6.5 の要確認。保存されなければ fail とし、止められたか・別名になったかを記録：設計の不足10）。中身に `"version": 1`、`exportedAt`（`+09:00`）、登録した件が id 付きである |
| TC-H41 | [F4-IO] [N-STATE] | TC-H40 のファイル | EN ボタンでリセット → 画面を開く → 「インポート」で TC-H40 のファイルを選ぶ | リセット後の一覧は空。インポート後「<n>件を読み込みました」と出て、一覧が id を含めエクスポート前と同じになる |
| TC-H42 | [F4-IO] | TC-H40 のファイルの `"version": 1` を `2` に書き換えたもの | インポートする | 「読み込めませんでした：unsupported version」。一覧は変わらない。同じファイルを続けてもう一度選べる |
| TC-H43 | [N-BOOT] [F4] | 今の時刻を T とし、T+2分・毎日の照明 `full` の1件だけを登録してエクスポートしておく。LED をカメラで映す | T+2分の 20 秒前に EN ボタンでリセット → T+2分になってから 10 秒以上たった後（同じ分の中）に、エクスポートしたファイルをインポート → T+3分まで待つ | リセットから T+3分まで LED が一度も光らない（リセット直後は一覧が空。判定済みの分にインポートした予定はその分には送られない） |
| TC-H76 | [F4-IO] [API] | NTP 取得済み（`clock.synced=true`）。スケジュールを1件以上登録。PC の端末 | `curl -D - http://IP/api/schedules/export` を実行し、そのときの時計（JST）を控える | 応答 200。応答ヘッダに `Content-Disposition` があり、その `filename` が `irhub-schedules-YYYYMMDD-HHMM.json`（実行した時刻の JST の年月日・時分）。本文の `exportedAt` が同じ日付・時分の `+09:00` の文字列（D-04 のテスト観点。スマホでの保存は URL を直接開かず TC-H40 の画面のボタンで確かめる） |

### フェーズ6（H-6）：外出先から（`docs/ops/remote-access.md` の手順0〜7を済ませてから）

期待結果を満たさない行があったら、`docs/ops/remote-access.md` の「うまくいかないとき」の表で当てはまる症状を探し、「見るところ」で見えたことと行った「対応」を、その行の ID と一緒にメモに残す。対応の後にその行をやり直して満たせば ✓ にしてよい（満たさなければ fail のまま）。

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H44 | [F6] | 手順0〜5 済み | Tailscale の管理画面で PC の Subnets を見る | 承認済みのルートが `<IP>/32` の1つだけ。`/24` などが無い |
| TC-H75 | [F6] | 手順1a（Disable key expiry）済み | Tailscale の管理画面の Machines を開き、PC の行を見る。続けて PC の行のメニュー（…）を開いて項目を見て、何も選ばずに閉じる | PC の行に Expiry disabled と出る（表示の文言・位置は D-07 の要確認8。行に無ければ PC の詳細画面で鍵の期限の欄が無効になっていればよい）。メニューの項目が Enable key expiry（期限を戻す側）になっている。スマホの行は変えていない（期限の表示が残っていてよい） |
| TC-H45 | [F6] [N-IP] | ESP32 の IP 固定（D-06 6節の方式A または B） | 方式A：ルーターの DHCP 予約一覧を見る。方式B：シリアルの `[wifi] static ip set` と `connected ip=` を見る | 方式A：`[wifi] mac` の MAC に IP が予約され、`connected ip=` と一致。方式B：`connected ip=` が `kStaticIp` と一致し、ルーターの自動割り当て範囲の外 |
| TC-H64 | [F6] [N-IP] [N-WIFI] [N-TIME] | 方式B（`kUseStaticIp=true`）の場合だけ行う（方式A なら「対象外」と書いて ✓）。シリアルを開く | 起動後の `[wifi] connected ip=` を控える → ルーターの電源を切り、`attempt 2/3` が出たら戻す → 再起動せずに `[wifi] connected` が出たら `GET http://IP/api/status` | 再試行後の `connected ip=` が起動時と同じ `kStaticIp` の値。`clock.synced=true`（起動時に `[ntp] synced` が出ている＝DNS が効いている）。`http://IP/` が開ける（D-06 4.3 の要確認） |
| TC-H66 | [F6] | 手順3（スリープしない設定）済み。変える前のスリープ時間を控えておく（例 30 分） | PC に触らずに、控えた時間より長く（例 1 時間）置く → スマホ（モバイル回線）で Tailscale の管理画面の Machines を開く → 続けて TC-H46 を行う | 管理画面の PC が Connected のまま。TC-H46 の期待結果を満たす（D-07 手順3の確認） |
| TC-H46 | [F6] | エアコンを運転中にしておく（停止中は温度だけ変えても赤外線を送らない。D-02 6節）。スマホの Wi-Fi を切り（モバイル回線）、Tailscale アプリをオン | `http://IP/` を開き、エアコンの温度を 1℃ 変え、照明ボタンを1つ押す | 家の中と同じ画面が出て、室温・湿度・時刻が表示される。エアコンと照明が実際に動く（家にいる人、または家の中でモバイル回線で確かめる） |
| TC-H47 | [F6] | TC-H46 と同じ状態 | ルーターの管理画面（例 `http://192.168.1.1/`）を開く。次に Tailscale アプリをオフにして `http://IP/` を開く | どちらも開けない（タイムアウト） |
| TC-H48 | [F6] | PC を Windows からサインアウト（シャットダウンしない） | TC-H46 をもう一度行う | 画面が開き、操作が効く |
| TC-H49 | [F6] [N-RESP] | TC-H46 の状態 | 照明ボタンを押す | 動く（外出先の遅れは N-RESP の1秒の対象外。D-06 疑問4。かかった時間を参考に記録） |

### PC とモックで人が確かめること（I-04 の後、H-4 の前）

前提：`python tools/mock_server.py`（オプションは各行）を起動し、PC のブラウザをスマホ幅（幅 390px 程度）にして `http://127.0.0.1:8000/` を開く。送った本文は開発ツールの Network で見る。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-H50 | [UI] [F1] | オプションなし | 開く。モックの風量の選択肢を1つ減らして再起動し、開き直す | 上部バー・エアコン・照明・スケジュールが描かれる。モード・風量・風向のボタンが `acCapabilities` の順に並び、選択肢を減らすとボタンも減る |
| TC-H51 | [UI] [F1] | オプションなし（冷房 26℃） | ＋を1回押す。上限まで＋、下限まで− | 送った本文が `{"temp":27}` だけ。上限で＋、下限で−が押せない |
| TC-H52 | [UI] [N-RESP] | `--delay-ms 800` | ＋を素早く3回 | `POST /api/ac` が重ならず1本ずつ出る。最後の表示が 29℃（26＋3） |
| TC-H53 | [UI] | `--delay-ms 6000` | ＋を2回押す。モックを `--delay-ms 0` で起動し直して帯を押す | 約 5 秒で「ESP32 に接続できません」の帯。2回目の＋は捨てられ、読み直した温度が描かれる。次の成功で帯が消える |
| TC-H54 | [UI] [N-TIME] | `--unsynced` | 開く。スケジュールの追加・エクスポート・インポートを押す | 上部の帯「時刻未取得のためスケジュールは実行されません」、カードの注記「時刻未取得の間は実行されません」、`#clock` が `--:--`。スケジュールの操作は押せる |
| TC-H55 | [UI] [F5] | `--no-sensor`、次にモックの室温を整数 24 に | 開く | `室温 --.-℃  湿度 --.-%`。整数のときは `室温 24.0℃` |
| TC-H56 | [UI] [F5] | オプションなし | 2 分開いたままにする → タブを隠して 1 分 → 戻す | `GET /api/status` が 30 秒ごとに1本ずつ（2 分で4本）。隠している間は出ない。戻したらすぐ1本と `GET /api/schedules` |
| TC-H57 | [UI] [F4-IO] | エクスポートしたファイル | インポート。`version` を 2 にしたファイルでもう一度 | 送った本文がファイルの中身そのもの（文字列化の二重エスケープが無い）。2回目は「読み込めませんでした：unsupported version」で一覧が変わらない |
| TC-H58 | [UI] [F4] [F4-LIMIT] | オプションなし | 追加（エアコン運転＋暖房 20℃、エアコン停止、照明 全灯）、編集、削除、有効切替。フォームでモードを「指定しない」にする。件数を max まで増やす | 各操作で `PUT /api/schedules` が1回、既存の件に id が付いている。モード未指定のとき温度が選べない。max 件で追加ボタンが押せない |
| TC-H59 | [UI] | — | `python tools/embed_html.py` を2回。`web/index.html` を消す／0x00 を入れる／80,000 バイト超にして実行 | 生成された配列（末尾の 0 を除く）が `web/index.html` のバイト列と一致し、`kIndexHtmlLen` がそのバイト数。2回目はファイルの更新時刻が変わらない。3つの異常はどれも終了コードが 0 以外 |
| TC-H62 | [UI] [F1] [F2] | `tools/mock_server.py` の `acCapabilities.tempModes` から `dry` を外して起動（D-05 のテスト観点の手順） | エアコンのモードで「除湿」を押す → 「冷房」に戻す。次にスケジュールの「追加」でエアコン運転を選び、モードを除湿にする | 除湿のとき：`POST /api/ac` の応答の `ac.temp` が null、温度の欄（＋・−と数値）が隠れ、「このモードは温度を指定できません」が出る。冷房に戻すと温度の欄が戻り、「このモードは温度を指定できません」が消える。スケジュールのフォームで除湿を選ぶと温度の選択が押せない（`disabled`、値は「指定しない」） |
| TC-H63 | [UI] [N-TIME] [F6] | オプションなし。モックの `/api/status` の `clock.now` を Network で見て控える（例 `2026-09-25T20:00:00+09:00`） | PC（OS）のタイムゾーンを UTC に変えてからブラウザを開き直して画面を開く。終わったら元に戻す | 上部バーの時刻が `clock.now` の JST の時刻と日付・曜日（例 `9/25(金) 20:00`。1 分以内の進みは可）で、UTC の `11:00` にならない（D-05：`getUTC*` と +9 時間で JST を出す） |
| TC-H67 | [UI] [F5] | `--delay-ms 3000`（D-05 のテスト観点の手順）。開発ツールの Network を開き、Waterfall（時間の帯）を出しておく | 画面を開いて最初の読み込みが終わるのを待つ → 次の `GET /api/status` が出て応答待ち（保留中）の間、出てから 1 秒以内にタブを隠し、すぐ戻す。これを3回くり返す | 3回とも、`/api/status` の保留中の要求は同時に多くて1本（Waterfall で `/api/status` の帯が時間で重ならない）。タブを戻した時点で前の要求が応答待ちなら、戻したことで2本目の `/api/status` は出ない（D-05 4節 `statusInFlight`：送信中なら新しく投げない） |
| TC-H71 | [UI] [F1] | オプションなし。画面の運転ボタンが「停止中」（運転中なら押して停止にする） | (1) ＋を1回、5 秒後にモードのボタン（今と違うもの）、5 秒後に風量のボタン（今と違うもの）。(2) 5 秒後に運転ボタン、続けて 5 秒後に＋。(3) モックを `--delay-ms 4000` で起動し直し、画面を開き直して運転ボタンが「停止中」であることを確かめる（運転中なら押して停止にし、応答を待つ）。＋を押し、その応答を待たずに（1 秒以内に）運転ボタンと＋を続けて押す | (1) 3回とも `POST /api/ac` の本文に `power` が無く（`{"temp":27}` など1項目）、200 の後に `#ac-msg` に「停止中のため設定だけ変えました（運転を押すとまとめて送ります）」が出て約 3 秒で消える。(2) 運転ボタン（本文 `{"power":true}`）と運転中の＋では出ない。(3) `POST /api/ac` が2本（2本目の本文に `power:true` と `temp` の両方）。1本目の応答（約 4 秒後）で知らせが出て、2本目の応答（約 8 秒後）の後には出ない。最後の表示が「運転中」 |
| TC-H72 | [UI] [F4-IO] | オプションなし。スケジュールを2件登録。開発ツールの Network を開く。ブラウザのダウンロード先を控える | (1)「エクスポート」を押す。(2) モックを `--delay-ms 3000` で起動し直し、押してから応答までの間に「エクスポート」をもう一度押そうとする。(3) 一覧を0件にして押す。(4) モックを `--unsynced` で起動し直して押す | (1) `GET /api/schedules/export` が `fetch`（Network の種類が fetch）で1本出る。ページが移らず、ダウンロードに1つファイルが増え、その名前がモックの応答の `Content-Disposition` の `filename` と同じ。中身が応答の本文と同じ。`#sched-msg` に「エクスポートしました：<その名前>」が出て約 3 秒で消える。(2) 応答までの間「エクスポート」が押せず（`disabled`）、要求は1本だけ。応答の後は押せる。(3) 0件でも押せて保存され、中身の `schedules` が `[]`。(4) 保存名が `Content-Disposition` のとおり（D-04 の未取得時の名前は `irhub-schedules.json`。モックがそれを返すかは D-05 12節に無いので名前の一致だけを見る） |
| TC-H73 | [UI] [F4-IO] | スケジュールを2件登録した画面 | (1) モックを止めて（Ctrl+C）「エクスポート」を押す。(2) モックを `--delay-ms 0` で起動し直して帯を押し、帯が消えたら（画面は開き直さない）モックを `--delay-ms 6000` で起動し直し、すぐに「エクスポート」を押す | (1) すぐに上部に「ESP32 に接続できません」の帯、`#sched-msg` に「送れませんでした（通信エラー）」。ファイルは保存されない。(2) 押してから約 5 秒で同じ帯と文言が出て、ファイルは保存されない。どちらも一覧の表示は2件のまま。モックを `--delay-ms 0` に戻して帯を押すと帯が消える |
| TC-H74 | [UI] [F4-IO] | オプションなしで画面を開き、開発ツールのコンソールを開く | `filenameFromDisposition('attachment; filename="irhub-schedules-20260925-2000.json"')`、`filenameFromDisposition(null)`、`filenameFromDisposition('attachment')` を入力する | 順に `"irhub-schedules-20260925-2000.json"`、`"irhub-schedules.json"`、`"irhub-schedules.json"`（D-05 6.5） |

---

## 要件カバレッジ

| 要件 | native（TC-N） | 実機・PC（TC-H） |
|---|---|---|
| [F1] | N03〜N20、N22〜N34、N91、N92、N135〜N138、N146、N150、N152〜N161、N164、N185〜N196 | H03〜H06、H08、H09、H11〜H13、H15、H17、H22、H50、H51、H62、H68〜H71 |
| [F2] | N01、N02、N08〜N11、N23、N24、N131、N145、N154、N187、N191、N194〜N196 | H16、H62 |
| [F3] | N93、N139、N140、N165〜N167 | H07、H14、H15、H23 |
| [F4] | N35〜N98、N102〜N104、N123、N127、N169〜N173、N179、N180、N184 | H37、H38、H43、H58、H60 |
| [F4-LIMIT] | N69、N70、N105、N114、N169、N171 | H39、H58 |
| [F4-IO] | N76、N77、N100、N101、N106〜N129、N174〜N178、N182〜N184、N197〜N199、N201 | H40〜H42、H57、H72〜H74、H76 |
| [F5] | N132〜N134、N145、N148、N149 | H18〜H20、H24、H55、H56、H67 |
| [F6] | 対象外（code=false。ESP32 側の対応なし） | H44〜H49、H63、H64、H66、H75 |
| [N-AUTH] | N181 | H28 |
| [N-WIFI] | 対象外（判断が `src/wifi_manager` にあり native テストを作らない。D-01 1節の人の判断） | H30〜H33、H61、H64 |
| [N-STATE] | N01、N82、N131 | H34、H41 |
| [N-BOOT] | N43、N82、N84、N130、N145、N180 | H10、H30、H35、H43、H61、H69 |
| [N-TIME] | N35〜N37、N43、N83、N84、N86、N87、N107、N145、N147、N175、N179、N197、N199 | H26、H27、H54、H60、H61、H63、H64 |
| [N-RESP] | 対象外（時間の計測は実機だけ。D-05 9節・D-06 3節） | H25、H49、H52、H65 |
| [UI] | 対象外（画面は C++ ではない。画面が頼る API の形は N145〜N181 で確かめる） | H21〜H24、H36、H50〜H59、H62、H63、H67、H70〜H74 |
| [API] | N141〜N163、N165、N166、N168、N181、N184、N192、N199〜N201 | H17、H29、H76 |
| [HW-PINS] | 対象外（`src/pins.h` は native でビルドしない） | H01、H02、H10、H11、H14、H18 |
| （参考）[N-IP] | 対象外（この作業項目の対象要件 T-01 の reqs の外。native の対象コードも無い） | H45、H64（F6 の前提として IP 固定を確かめるために付けた。カバレッジの判定には数えない） |

要件8章の完成判定との対応：フェーズ0＝TC-H01、フェーズ1＝TC-H02〜H08、フェーズ2＝TC-H11〜H15、フェーズ3＝TC-H18・H19、フェーズ4＝TC-H21〜H24、フェーズ5＝TC-H37・H40・H41、フェーズ6＝TC-H46。

### 設計の不足（テストを書くうえで設計書に無かった点）

1. フェーズ0 のファームとフェーズ1 の受信ダンプ（`env:dump`、I-HW1）の出力形式を決めた設計書が無い。TC-H01〜H08 は要件8章と state の H-0／H-1 の手順だけを根拠にした。
2. フェーズ3「室温計と大きくずれない」の許容差がどの設計書にも無い。TC-H19 は差を記録するだけにした（人が閾値を決める必要がある）。
3. スケジュールの JSON で、エアコンの `action` に `power` が無いとき、D-03 6.3 の順7（`schedules[0].action.power: missing`）と順8（`schedules[0]: power required`）のどちらになるかが明示されていない。6.1 の表で `power` が「必須」なので、TC-N115 と同じ読み方で順7 とした（TC-N182、N184）。設計がこの読み方と違えば TC-N182・N184 の文言を直す。
4. `time` の `"24:00"`・`"07:60"` が順7（`must be HH:MM`）か順8（`time out of range`）かが明示されていない。D-03 6.1 の表（HH は 00..23、`"24:00"` は不可）を形の規則と読み、順7 とした（TC-N115、N172）。
5. スケジュール1件の中（`action` の外）に知らないキーがあるときのエラー文言の形が D-03 6.4 に無い（例は `action` のものだけ）。TC-N183・N184 は「失敗する」「文言が `schedules[0]` で始まる」「一覧が変わらない」だけを確かめる。
6. `Hub::clockNow()` が `IClock::now()` を何回呼ぶかは明記されていない。TC-N174 はそれに頼らず、呼ぶたびに進む FakeClock（`seq`）で「ファイル名と `exportedAt` が同じ時刻」だけを確かめる（D-04 8節）。
7. F5 の画面への反映の時間：D-05 のテスト観点は「30 秒以内に変化が反映される」だが、センサーの周期（30 秒、D-06）と画面の読み直し（30 秒、D-05）が重なると最悪 60 秒になる。TC-H24 は 60 秒で判定した。どちらを正とするかは人が決める。
8. N-RESP で、エアコンの送信中に照明を押した場合の合否の値が無い（D-06 3.3「1 秒前後に収まる見込み。実機で測る」）。TC-H65 (a) は時間を記録するだけにした。
9. （解消済み）D-02 のテスト観点が `Hub` に無い `settingsFor` を期待値に使っていた点。D-02 のテスト観点が直り、`Hub` から見える値（`{"mode":"cool"}` の後の `lastAc.tempC=27`）で確かめる手順になった。その手順（temp→mode→fan→swingV→power→mode=cool）どおりのケースとして TC-N194〜N196 を足した。TC-N185〜N187（mode→temp の順で、暖房の記憶に入った 27 を確かめる）は別の観点としてそのまま残す。`settingsFor` そのものは `AcModel` 単体（TC-N25）で確かめる。
10. エクスポートで、応答を待った後の `a.click()` を iOS Safari・Android Chrome が止めた場合の代わりの手段が決まっていない（D-05 6.5 の要確認・疑問7）。TC-H40 は保存されなければ fail とし、どうなったかを記録するだけにした（代わりの手段は人が決める）。
11. （解消済み）D-04 のテスト観点の `{"temp":27}` が停止中の規則（D-02 6節・D-01）の前の書き方だった点。D-04 のテスト観点は「先に `{"power":true}` で運転中にしてから `{"temp":27}` → `acCount` 1→2」「生成直後（停止中）に `{"temp":27}` → `acCount==0`」に直った。TC-N152・N163・N164（運転中にしてから）と TC-N192（停止中は送信 0 回）はこの文言と一致する。
12. D-05 8.2 の「200 の後の本文の読み取り（`res.text()`・`res.blob()`）が中止・失敗したら `showConnError()` で帯を出し直し、status 0 として扱う」を PC で起こす手段が無い（D-05 12節のモックのオプションは応答全体を遅らせる `--delay-ms` だけで、ヘッダを返した後に本文だけ遅らせる・切る方法が無い）。この扱いのケースは作らず、応答そのものの失敗・タイムアウト（TC-H53、TC-H73）だけを確かめる。
