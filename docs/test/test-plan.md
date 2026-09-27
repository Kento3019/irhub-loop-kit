# テスト計画

作業項目：T-01（D1 確定・F3 対象外に合わせた作り直し）／要件の原本：`docs/requirements.md` v0.4／要件ID：`docs/req-index.json`（D1 closed 後）／根拠にした設計書：`docs/design/01-architecture.md`（D-01）〜`06-runtime.md`（D-06）、`docs/ops/remote-access.md`（D-07）（いずれもコミット 5812e70 の版）／受信記録：`docs/hw/phase1-capture.md`

対象要件：[F1] [F2] [F3] [F4] [F4-LIMIT] [F4-IO] [F5] [F6] [N-AUTH] [N-WIFI] [N-STATE] [N-BOOT] [N-TIME] [N-RESP] [UI] [API] [HW-PINS]

D1 の確定（この計画に関わるもの）：プロトコル HITACHI_AC296（IRac で送れるのは運転・モード・温度・風量）。モード 自動・冷房・除湿・暖房（**自動は温度指定なし**）。温度 16〜30℃。風量 5 段階（自動・静音・弱・中・強。API の文字列は `auto` `min` `low` `medium` `high`）。風向と本体タイマーは作らない（状態モデルの `swingV`・`swingH` は残り、選択肢は `Off` だけ）。照明（F3）は対象外で、照明のケースは作らない（作っていないことを確かめるケースだけ置く）。

---

## 方針

### 1. テストの種類と置き場所

| 種類 | 実行方法 | 置き場所 | 書く作業項目 | ケースID |
|---|---|---|---|---|
| native 単体テスト（Unity） | `pio test -e native` | `test/test_ac_state/`（`AcModel`・`cap::`・文字列変換） | T-02 | TC-N01〜TC-N34、TC-N202〜TC-N204 |
| 〃 | 〃 | `test/test_schedule/`（スケジュールの純関数・`ScheduleList`・`schedule_json`・`Hub` のスケジュール実行） | T-03 | TC-N35〜TC-N129、TC-N182、TC-N183、TC-N197、TC-N198、TC-N205〜TC-N216 |
| 〃 | 〃 | `test/test_api/`（`Hub` の基本動作・`ApiRouter`） | T-04 | TC-N130〜TC-N181、TC-N184〜TC-N196、TC-N199〜TC-N201、TC-N218〜TC-N220 |
| 実機（人が行う） | 手順どおりに操作して目で確かめる | この文書の「テストケース（実機）」 | H-0〜H-6 のゲートで人が使う | TC-H01〜TC-H06、TC-H08〜TC-H49、TC-H60、TC-H61、TC-H64〜TC-H66、TC-H68〜TC-H70、TC-H75〜TC-H82 |
| PC とモック（人が行う） | `python tools/mock_server.py`、`python tools/embed_html.py` | 同上 | I-04 の後、H-4 の前に人が行う | TC-H50〜TC-H59、TC-H62、TC-H63、TC-H67、TC-H71〜TC-H74、TC-H83〜TC-H85 |

- 欠番：TC-N99、TC-N217、TC-H07（旧：照明6ボタンの受信）。旧版で照明を扱っていた TC-N93・N94・N139・N140・N165〜N168・TC-H13・H14・H23 は、照明を除いたうえで同じ番号を別の観点に使い直した（T-03・T-04 はまだ書かれていないので番号を保つ方を選んだ。T-02 の範囲の番号は意味を変えていない）。
- `Hub` を使うテストの置き場所（D-02 テスト観点で「T-01 で決める」とされた点）：`Hub::applyAc`・コンストラクタ（N-BOOT）・センサー周期（F5）は `test/test_api/`、`Hub::tick` のスケジュール実行と `Hub::replaceSchedules` は `test/test_schedule/` に置く。
- N-WIFI（`src/wifi_manager`）、D-06 の `src/` の処理、画面（`web/index.html`）は native テストの対象外（D-01 1節・D-05・D-06 のテスト観点）。実機ケースと PC ケースで確かめる。
- F6 は code=false。実機ケース（フェーズ6）だけで確かめる。F3 は対象外（code=false）。照明を作っていないことだけを native と実機で確かめる。

### 2. 期待値の書き方（`cap::` から作る）

T-02 の差し戻し（人の判断）と D-02・D-03・D-04 のテスト観点に従い、`ac_capabilities.h` の値（温度範囲・モード別の温度指定可否・風量と風向の選択肢）と仮値（件数上限）、F2 の暖房・除湿・自動の初期値（D2 で決定。表の1か所だけを正とするため）を**テストに直書きしない**（F2 の初期値の直書きは下の例外だけ）。`cap::` と `kScheduleMax`・`initialSettings(m)` から作る。こうすると I-06 で `ac_capabilities.h` を確定値にする前も後もテストが通る。

テストの中で使う名前（各テストファイルの先頭で `cap::` から求める）：

| 名前 | 求め方 | I-06 前（今の仮値） | I-06 後（D1 の確定値） | 見つからないとき |
|---|---|---|---|---|
| `F1` | `cap::kFanChoices` のうち `Auto` でない最初の値 | `Low` | `Min`（静音） | `TEST_IGNORE_MESSAGE` |
| `Fx` | `AcFan` の列挙（Auto, Min, Low, Medium, High, Max）の順に見て `cap::fanSupported` が false の最初の値 | `Min` | `Max` | `TEST_IGNORE_MESSAGE` |
| `Mt` | `AcMode` の列挙の順に見て `cap::tempSupported` が false の最初のモード | 無し（→ IGNORE） | `Auto` | `TEST_IGNORE_MESSAGE` |
| `V1` | `cap::kSwingVChoices` のうち `cap::kSwingVDefault` と違う値 | `Auto` | 無し（→ IGNORE） | `TEST_IGNORE_MESSAGE` |
| `H1` | `cap::kSwingHChoices` のうち `cap::kSwingHDefault` と違う値 | 無し | 無し | `TEST_IGNORE_MESSAGE` |
| `Vx` | `AcSwingV` の列挙の順に `cap::swingVSupported` が false の最初 | `Highest` | `Auto` | `TEST_IGNORE_MESSAGE` |
| `Hx` | `AcSwingH` の列挙の順に `cap::swingHSupported` が false の最初 | `Auto` | `Auto` | `TEST_IGNORE_MESSAGE` |

- 温度の範囲の端は `cap::kTempMinC`／`cap::kTempMaxC` から作る（確定値 16／30。この計画では読みやすさのため `kTempMaxC+1`（31）のように確定値を括弧で添える）。
- 範囲の端以外の温度（27、24、22、21、20、19、18、25）は `cap::tempInRange` の中である前提で直書きしてよい（D-02 テスト観点）。温度付きで冷房・暖房・除湿を使うケースは、先頭で `cap::tempSupported(そのモード)` が true であることを確かめ、false なら `TEST_IGNORE_MESSAGE`。
- 風量 `High`・`Low` を直書きで使うケース（D-02 4節の遷移例の表を流すケースなど）は、先頭で `cap::fanSupported(High)`・`fanSupported(Low)` が true であることを確かめ、false なら IGNORE（仮値・確定値とも true）。
- F2 の初期値（D2 で決定、2026-09-27）：冷房 26℃・風量 自動は直書きする。暖房・除湿・自動の値は、ふだんのケースでは `initialSettings(m)`（`AcModel` 単体では `settingsFor(m)` でもよい）から取る（表の1か所だけを正とする。D-02 テスト観点）。確定値そのもの（暖房 22・除湿 26・風量はすべて自動）が表に入っていることは TC-N02 で1度だけ直書きで確かめる。この計画では確定値（暖房 22、除湿 26、自動 25＝温度指定なしのため使われない値）を括弧で添える。
- 文字列は `toString(F1)` などで作る（`F1` の文字列は I-06 前 `"low"`、後 `"min"`）。
- エラー文言は設計書の表の文字列をそのまま比べる（`TEST_ASSERT_EQUAL_STRING`）。件数を含む文言は `snprintf(buf, sizeof buf, "too many schedules (max %d)", kScheduleMax)` で作る（今は `too many schedules (max 10)`）。
- API の応答本文は ArduinoJson で読み直してキーごとに比べる。「キーが〜の N 個だけ」は `JsonObject` を走査してキーの集合を比べる。文字列全体の一致は、キーの順が設計で決まっているもの（`schedule_json` の `action`）だけで使う。

### 3. フェイク（各テストディレクトリの `fake_ports.h`、D-01 9節の形に足す）

| フェイク | 持つもの（D-01 9節の例に足す分は ★） |
|---|---|
| `FakeIrSender` | `acCount`、`lastAc`、`result`（`true`。false にすると `sendAc` が false を返す） |
| `FakeClock` | `r`（返す `ClockReading`）、★`int nowCount`（`now()` の呼び出し回数）、★`std::vector<ClockReading> seq`（空でなければ `now()` は `r` の代わりに `seq` を先頭から1つずつ返し、使い切った後は最後の値を返し続ける。呼ぶたびに時刻が進む時計の代わり） |
| `FakeClimateSensor` | `readCount`、`next`（返す `ClimateReading`） |

`IIrSender` にあるのは `sendAc` だけ（照明のメソッドは無い）。フェイクにも照明の数を持たせない。

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
- スケジュールの略記：`S(id, "HH:MM", days, 操作)`。days は曜日ビット（日=0x01 … 土=0x40、平日=0x3E、毎日=0x7F）。id を書かないものは 0（採番される）。操作の略記：`運転(m,t)` ＝ `{power=true, mode=m, tempC=t}`、`停止` ＝ `{power=false}`。予定を見分けるため、主に `PA = 運転(Cool,25)` と `PB = 運転(Heat,21)` を使う（送られたのがどちらかを `lastAc.mode`・`lastAc.tempC` で見分ける）。

### 5. 境界値と異常系の必須項目（どのケースで扱うか）

| 観点 | ケース |
|---|---|
| 温度の上下限±1 | TC-N12〜N15、N64、N159、TC-H13（下限・上限の送信）、TC-H22（画面の＋−の端） |
| スケジュール10件目と11件目 | TC-N69、N70、N114、N171、TC-H39 |
| 23:59→00:00（日付の境目） | TC-N37、N53 |
| 曜日の境目（土→日、金→土） | TC-N38、N41、N53 |
| 不正 JSON | TC-N110、N162 |
| 範囲外 | TC-N14〜N16、N57〜N59、N64、N124、N129、N159、N203、N205、N218、TC-H17 |
| 未知のボタン名（照明のボタンのキー `button`、知らないモード・風量の文字列、照明のパス） | TC-N120、N156、N157、N165、N166、N211、TC-H80 |
| 温度を指定できないモード（自動）＋温度 | TC-N23、N203、N205、N212、N218、TC-H17 |

---

## テストケース（native）

### A. test/test_ac_state（T-02：AcModel、cap::、文字列）

前提（全ケース共通）：`AcModel m;` を新しく作る。「状態」は `m.state()`。`F1`・`Fx`・`Mt`・`V1`・`H1`・`Vx`・`Hx` は方針2の表。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N01 | [F2] [N-STATE] | 生成直後 | `m.state()` を読む | `power=false`、`mode=Cool`、`hasTemp==cap::tempSupported(Cool)`（true）、`tempC=26`、`fan=Auto`、`swingV=cap::kSwingVDefault`（Off）、`swingH=cap::kSwingHDefault`（Off） |
| TC-N02 | [F2] | 生成直後 | `settingsFor` を4モード読む | 4モードとも `settingsFor(m)` の全項目が `initialSettings(m)` と一致（D2 確定値：自動 25（使わない値）・冷房 26・除湿 26・暖房 22、風量はすべて Auto、風向は Off）。加えて初期値の確定値を1度だけ直書きで確かめる：`initialSettings(Cool)` が `tempC=26`・`fan=Auto`、`initialSettings(Heat)` が `tempC=22`・`fan=Auto`、`initialSettings(Dry)` が `tempC=26`・`fan=Auto`、`initialSettings(Auto).fan=Auto`（自動の `tempC` は使わない値なので確かめない） |
| TC-N03 | [F1] | 生成直後 | `apply({tempC=27})` | 戻り値 `None`。`tempC=27`、`power=false`・`mode=Cool`・`fan=Auto`・風向は変わらない。`settingsFor(Cool).tempC=27` |
| TC-N04 | [F1] | 生成直後 | `apply({power=true})` | `None`。`power=true`、`mode=Cool`・`tempC=26`・`fan=Auto`・風向は変わらない |
| TC-N05 | [F1] | 生成直後 | `apply({fan=F1})` | `None`。`fan=F1`、他は変わらない。`settingsFor(Cool).fan=F1` |
| TC-N06 | [F1] | 生成直後。`V1` を探す（確定値では無いので IGNORE） | `apply({swingV=V1})` | `None`。`swingV=V1`、他は変わらない |
| TC-N07 | [F1] | 生成直後。`H1` を探す（仮値・確定値とも無いので IGNORE） | `apply({swingH=H1})` | `None`。`swingH=H1`、他は変わらない |
| TC-N08 | [F1] [F2] | 生成直後。`fanSupported(High)`・`(Low)` が true | D-02 4節の遷移例の表の14行（#1〜#14）を上から順に `apply`（`{"temp":31}` の行は `kTempMaxC+1` で作る） | 各行の後で、結果（上から None×6、TempOutOfRange×2、None×5、EmptyPatch）、状態（power, mode, temp, fan）、4モードの `settingsFor` の変化が表どおり。暖房の復元値（#3）は `initialSettings(Heat).tempC`（22）。#4 `{"temp":22,"fan":"high"}` の温度も `initialSettings(Heat).tempC` から作る（同じ値を受け付けてそのまま。D-02 4節）。停止中の2行（#10 `{"temp":19}`→`false, Heat, 19, High`、#11 `{"mode":"cool","fan":"low"}`→`false, Cool, 27, Low`、`settingsFor(Cool).fan=Low`）も運転中と同じに更新される。最後の状態は `true, Cool, 27, Low`（「送信」欄は `AcModel` では確かめない。TC-N191） |
| TC-N09 | [F2] | `apply({power=true})`、`apply({tempC=27})` | `apply({mode=Heat})` の後 `apply({mode=Cool})` | 暖房にしたとき `tempC=initialSettings(Heat).tempC`（22）、冷房に戻したとき `tempC=27` |
| TC-N10 | [F2] | `apply({mode=Heat})` | `apply({fan=F1})` の後 `apply({mode=Cool})` | 冷房の `fan=Auto`（暖房の F1 が漏れない）。`settingsFor(Heat).fan=F1` |
| TC-N11 | [F1] [F2] | 生成直後（冷房） | `apply({mode=Heat, tempC=18})` | `None`。`mode=Heat`、`tempC=18`。`settingsFor(Heat).tempC=18`、`settingsFor(Cool).tempC=26` のまま |
| TC-N12 | [F1] | 生成直後 | `apply({tempC=cap::kTempMinC})`（16） | `None`、`tempC=16` |
| TC-N13 | [F1] | 生成直後 | `apply({tempC=cap::kTempMaxC})`（30） | `None`、`tempC=30` |
| TC-N14 | [F1] | 生成直後 | `apply({tempC=cap::kTempMinC-1})`（15） | `TempOutOfRange`、`tempC=26` のまま |
| TC-N15 | [F1] | 生成直後 | `apply({tempC=cap::kTempMaxC+1})`（31） | `TempOutOfRange`、`tempC=26` のまま |
| TC-N16 | [F1] | 生成直後 | `apply({tempC=300})`、`apply({tempC=-300})`、`apply({tempC=256+26})`（282。int8_t に詰めると 26 になる値） | 3回とも `TempOutOfRange`、`tempC=26` のまま |
| TC-N17 | [F1] | 生成直後。`Fx` を探す（確定値 Max） | `apply({fan=Fx})` | `FanNotSupported`、`fan=Auto` のまま |
| TC-N18 | [F1] | 生成直後。`Vx` を探す（確定値 Auto） | `apply({swingV=Vx})` | `SwingVNotSupported`、状態は変わらない |
| TC-N19 | [F1] | 生成直後。`Hx` を探す（Auto） | `apply({swingH=Hx})` | `SwingHNotSupported`、状態は変わらない |
| TC-N20 | [F1] | `apply({power=true})`、`apply({mode=Heat, tempC=18})`、`apply({fan=F1})` で `true, Heat, 18, F1` | `apply({mode=Cool, tempC=40})` | `TempOutOfRange`。状態は `true, Heat, 18, F1` のまま（モードも変わらない）。4モードの `settingsFor` が呼ぶ前と全項目一致 |
| TC-N21 | [F1] | 生成直後 | `apply({})` | `EmptyPatch`、状態は TC-N01 と同じ |
| TC-N22 | [F1] | 生成直後 | `apply({tempC=cap::kTempMaxC+1, fan=Fx})` | `TempOutOfRange`（検証の順：温度が風量より先） |
| TC-N23 | [F1] [F2] | `Mt` を探す（確定値 Auto。無ければ IGNORE） | (a) 冷房のまま `apply({mode=Mt, tempC=cap::kTempMinC})`。(b) `apply({mode=Mt})` の後に `apply({tempC=24})` | (a) `TempNotSupported`、モードは Cool のまま、状態不変。(b) 2回目が `TempNotSupported`、状態は Mt のまま不変（D-02 4節の A2・A5） |
| TC-N24 | [F1] [F2] | TC-N23 と同じ `Mt`（無ければ IGNORE） | `apply({mode=Mt})` | `None`、`mode=Mt`、`hasTemp=false`、`tempC=settingsFor(Mt).tempC`（= `initialSettings(Mt).tempC`、今は 25） |
| TC-N25 | [F1] | 生成直後（`power=false`） | `apply({tempC=24})` → `apply({mode=Heat})` → `apply({fan=F1})` | 3回とも `None`、`power=false` のまま。1回目の後 `tempC=24`・`settingsFor(Cool).tempC=24`。2回目の後 `mode=Heat`・`tempC=initialSettings(Heat).tempC`（22）。3回目の後 `fan=F1`・`settingsFor(Heat).fan=F1`・`settingsFor(Cool).fan=Auto`（停止中も運転中と同じ規則で状態とモード別の記憶が更新される。D-02 4節） |
| TC-N26 | [F1] | 生成直後（冷房） | `apply({mode=Cool})` | `None`、状態は TC-N01 と全項目同じ |
| TC-N27 | [F1] | 生成直後 | `validate({tempC=27})` | `None`。`state().tempC=26` のまま（validate は状態を変えない） |
| TC-N28 | [F1] | — | `auto` `cool` `dry` `heat` を `parseAcMode` → `toString` | 4つとも元の文字列に戻る。`parseAcMode("cool")==Cool`、`parseAcMode("auto")==Auto` |
| TC-N29 | [F1] | — | `auto` `min` `low` `medium` `high` `max` を `parseAcFan` → `toString` | 6つとも元に戻る。`parseAcFan("min")==Min`（静音） |
| TC-N30 | [F1] | — | `off` `auto` `highest` `high` `middle` `low` `lowest` を `parseAcSwingV` → `toString` | 7つとも元に戻る（列挙と文字列は残る。D-02 2節） |
| TC-N31 | [F1] | — | `off` `auto` `leftMax` `left` `middle` `right` `rightMax` `wide` を `parseAcSwingH` → `toString` | 8つとも元に戻る |
| TC-N32 | [F1] | — | `""`、`"Cool"`、`"fan"`、`"turbo"`、`"quiet"` を4つの `parse*` に、`"leftmax"` を `parseAcSwingH` に渡す | すべて `nullopt`（大文字小文字を区別。静音の文字列は `min` で、`quiet` は無い） |
| TC-N33 | [F1] | — | `errorMessage` を7つの `AcError` で呼ぶ | `""`、`no ac fields`、`temp out of range`、`temp not supported in this mode`、`fan not supported`、`swingV not supported`、`swingH not supported` |
| TC-N34 | [F1] | — | `cap::kTempStepC` を読む | 1（要件 F1 の決定値） |
| TC-N202 | [F1] [F2] | `Mt` を探す（無ければ IGNORE）。生成直後 | D-02 4節の「自動モードの例」A1〜A7 を上から順に `apply`。表の `auto` は `Mt`、`min` は `F1`、`max` は `Fx` に置き換える：A1 `{power=true, mode=Mt}`、A2 `{tempC=24}`、A3 `{fan=F1}`、A4 `{mode=Cool, tempC=24}`、A5 `{mode=Mt, tempC=24}`、A6 `{fan=Fx}`、A7 `{mode=Mt}` | 結果は上から `None, TempNotSupported, None, None, TempNotSupported, FanNotSupported, None`。A1 の後 `true, Mt, hasTemp=false, tempC=initialSettings(Mt).tempC, fan=Auto`。A3 の後 `settingsFor(Mt).fan=F1`。A4 の後 `true, Cool, hasTemp=true, 24, Auto`、`settingsFor(Cool).tempC=24`。A5・A6 の後は冷房のまま不変。A7 の後 `true, Mt, hasTemp=false, fan=F1`（Mt の最後の設定を復元） |
| TC-N203 | [F1] | `Mt` を探す（無ければ IGNORE）。`apply({mode=Mt})` 済み | `apply({tempC=cap::kTempMaxC+1})`（31）、`apply({tempC=99})` | 2回とも `TempNotSupported`（`TempOutOfRange` ではない。検証の順2が順3より先）。状態不変 |
| TC-N204 | [F1] | 生成直後 | `cap::kFanChoices` の各値 f を順に `apply({fan=f})` | すべて `None`、毎回 `state().fan==f`（確定値では 静音 `Min` を含む5つ） |

### B. test/test_schedule（T-03：スケジュール）

#### B-1. 時刻の変換と判定（純関数）

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N35 | [F4] [N-TIME] | — | `jstFromEpochMinute(29838900)` | `{wday=5, hour=20, minute=0}` |
| TC-N36 | [F4] [N-TIME] | — | `jstFromEpochMinute(29838120)`（M0） | `{5, 7, 0}` |
| TC-N37 | [F4] [N-TIME] | — | `jstFromEpochMinute(29839139)`、`(29839140)` | 金 23:59 `{5, 23, 59}`、次の分は土 00:00 `{6, 0, 0}`（日付と曜日が進む） |
| TC-N38 | [F4] | — | `jstFromEpochMinute(29840579)`、`(29840580)` | `{6, 23, 59}`、`{0, 0, 0}`（土→日で wday 6→0） |
| TC-N39 | [F4] | `s = S(1, "07:00", 0x3E, 停止)`、enabled | `scheduleMatches(s, {5,7,0})` | true |
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
| TC-N51 | [F4] | 一覧 `[S(5,"07:00",0x7F,PA), S(3,"07:00",0x7F,PB)]` | `dueSchedules(list, M0, M0, out, kScheduleMax)` | 戻り値 2、`out[0]={5, M0}`、`out[1]={3, M0}`（一覧の順） |
| TC-N52 | [F4] | 一覧 `[S(1,"07:02",0x7F,停止), S(2,"07:00",0x7F,停止)]` | `dueSchedules(list, M0, M0+2, out, 10)` | 戻り値 2、`out[0]={2, M0}`、`out[1]={1, M0+2}`（分の昇順） |
| TC-N53 | [F4] | 一覧 `[S(1,"00:00",0x40 土), S(2,"23:59",0x20 金), S(3,"00:00",0x20 金), S(4,"23:59",0x40 土)]`（操作はすべて停止） | `dueSchedules(list, 29839139, 29839141, out, 10)` | 戻り値 2、`out[0]={2, 29839139}`、`out[1]={1, 29839140}`（金 00:00・土 23:59 は入らない） |
| TC-N54 | [F4] | 07:00・毎日・停止の件を3件（id 1, 2, 3） | `dueSchedules(list, M0, M0, out, 2)` | 戻り値 2、`out[0].id=1`、`out[1].id=2` |

#### B-2. 1件の検証（validateSchedule）

前提：基準の件 `B = {id=0, enabled=true, hour=7, minute=0, days=0x3E, target=Ac, ac={power=true, mode=Heat, tempC=20}}`。各ケースは B の一部だけを変える。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N55 | [F4] | B、および B の `ac` を `{power=false}` にした件（エアコン停止） | `validateSchedule` | どちらも `None` |
| TC-N56 | [F4] | B の hour/minute を (0,0)、(23,59) に | `validateSchedule` | どちらも `None` |
| TC-N57 | [F4] | B の hour を -1、24、minute を -1、60 に（4通り） | `validateSchedule` | 4つとも `TimeOutOfRange` |
| TC-N58 | [F4] | B の id を 0、`kScheduleIdMax`（9999）、`kScheduleIdMax+1`（10000） | `validateSchedule` | `None`、`None`、`IdOutOfRange` |
| TC-N59 | [F4] | B の days を 0、0x80、0xFF、0x01、0x7F | `validateSchedule` | `NoDays`、`NoDays`、`NoDays`、`None`、`None` |
| TC-N60 | [F4] | B の `ac.power` を空に | `validateSchedule` | `AcPowerMissing` |
| TC-N61 | [F4] | `ac` を `{power=false, mode=Cool}`、`{power=false, tempC=20}`、`{power=false, fan=F1}`、`{power=false}` | `validateSchedule` | `AcStopHasOtherFields`×3、`None` |
| TC-N62 | [F4] | `ac={power=true, tempC=20}`（mode 無し） | `validateSchedule` | `AcTempWithoutMode` |
| TC-N63 | [F4] | `Mt`（無ければ IGNORE）で `ac={power=true, mode=Mt, tempC=kTempMinC}` | `validateSchedule` | `AcTempNotSupported` |
| TC-N64 | [F4] | `ac.tempC` を `kTempMinC-1`（15）、`kTempMinC`（16）、`kTempMaxC`（30）、`kTempMaxC+1`（31）（モードは Heat） | `validateSchedule` | `AcTempOutOfRange`、`None`、`None`、`AcTempOutOfRange` |
| TC-N65 | [F4] | (a) `ac={power=true, fan=Fx}`（無ければ (a) だけ IGNORE）、(b) `ac={power=true, swingV=Off}`、(c) `ac={power=true, swingH=Off}` | `validateSchedule` | (a) `AcFanNotSupported`、(b) `AcSwingVNotSupported`、(c) `AcSwingHNotSupported`（`cap::swingVSupported(Off)` は true だが、スケジュールは風向を値によらず持たない。D-03 1.1 の規則10・11） |
| TC-N66 | [F4] | B の `enabled=false`、hour=24 | `validateSchedule` | `TimeOutOfRange`（無効でも検証する） |
| TC-N67 | [F4] | B の hour=24、days=0 | `validateSchedule` | `TimeOutOfRange`（順：時刻が曜日より先） |
| TC-N68 | [F4] | — | `errorMessage(ScheduleError)` を14値で | D-03 6.4 の表どおり（`""`、`too many schedules`、`id out of range`、`duplicate id`、`time out of range`、`no days`、`power required`、`ac stop takes only power`、`temp requires mode`、`temp not supported in this mode`、`temp out of range`、`fan not supported`、`swingV not supported`、`swingH not supported`） |
| TC-N205 | [F4] | `Mt`（無ければ IGNORE）で `ac={power=true, mode=Mt, tempC=kTempMaxC+1}` | `validateSchedule` | `AcTempNotSupported`（規則7 が規則8 より先。`AcTempOutOfRange` ではない） |
| TC-N206 | [F4] | `Mt`（無ければ IGNORE）で `ac={power=true, mode=Mt}`（温度なし） | `validateSchedule` | `None` |
| TC-N207 | [F4] | `cap::kFanChoices` の各値 f について `ac={power=true, fan=f}` | `validateSchedule` | すべて `None`（確定値では静音 `Min` を含む5つ） |
| TC-N208 | [F4] [F1] | 1.1 を通る件の `ac` 5つ：`{power=true, mode=Heat, tempC=20}`、`{power=false}`、`{power=true, fan=F1}`、`{power=true, mode=Cool, tempC=kTempMaxC}`、`{power=true, mode=Mt}`（Mt が無ければこれだけ省く） | 4モード c それぞれについて新しい `AcModel` に `apply({mode=c})` してから、5つの `ac` を `validate` | 20通り（Mt が無ければ 16通り）すべて `None`（登録時に通った件は実行時にどのモードでも落ちない。D-03 1.1） |

#### B-3. 一覧の置き換えと採番（ScheduleList::replaceAll）

前提：`ScheduleList l;` を新しく作る（`nextId_=1`）。`nextId_` は非公開なので、次に id 無しの件を1件足したときに付く id で確かめる。件の中身は B（有効な件）。

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
| TC-N80 | [F4] | `[5, 7]`（id 7 の件は 08:15） | `findById(7)`、`findById(6)` | 7 は hour=8・minute=15 の件、6 は `nullptr` |
| TC-N81 | [F4] | — | 時刻 09:00, 07:00, 08:00 の順で `replaceAll` | `at(0)`〜`at(2)` の時刻が 09:00, 07:00, 08:00（並べ替えない） |

#### B-4. Hub でのスケジュール実行（FakeIrSender・FakeClock・FakeClimateSensor）

前提：`Hub hub(ir, clock, sensor);` を新しく作る。一覧は `hub.replaceSchedules(...)` で入れる。`tick` の `nowMs` は 1000 ずつ進める（明記したものを除く）。「確定させる」は、その時刻で `tick` を1回呼び `lastScheduleMin_` を作ること。`PA = 運転(Cool,25)`、`PB = 運転(Heat,21)`。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N82 | [N-STATE] [N-BOOT] | 生成直後 | `schedules().size()`、送信回数を読む | `size()=0`、`acCount=0` |
| TC-N83 | [N-TIME] | 一覧 `[S(1,"07:00",0x7F,PA)]`、FakeClock 未取得 | T0-60 から T0+60 まで 1 秒ずつ `tick`（121回） | `acCount=0` |
| TC-N84 | [N-TIME] [N-BOOT] | 一覧 `[S(1,"07:00",0x7F,PA), S(2,"07:01",0x7F,PB)]` | 07:00:10 で初めて synced にして `tick` → 07:01:00 で `tick` | 1回目の後 `acCount=0`（取得した分は実行しない）。2回目の後 `acCount=1`、`lastAc.mode=Heat`、`lastAc.tempC=21` |
| TC-N85 | [F4] | 一覧 `[S(1,"07:00",0x7F,PA)]`、06:59:30 で確定 | 07:00:00〜07:00:59 を 1 秒ずつ 60 回 `tick` | `acCount=1`（二重実行なし） |
| TC-N86 | [F4] [N-TIME] | synced | `tick(0)`、`tick(999)`、`tick(1000)` | `clock.nowCount` が 1、1、2 |
| TC-N87 | [F4] [N-TIME] | synced | `tick(0xFFFFFE0C)`、`tick(0x000001F3)`、`tick(0x000001F4)` | `nowCount` が 1、1、2（`millis()` の一周でも 1000ms で判定） |
| TC-N88 | [F4] | 一覧 `[S(1,"07:00",0x7F,PA)]`、06:58:00 で確定 | 07:03:00 で `tick`（d=5） | `acCount=1` |
| TC-N89 | [F4] | 同じ一覧、06:50:00 で確定 | 07:03:00 で `tick`（d=13） | `acCount=0` |
| TC-N90 | [F4] | 同じ一覧、06:59:30 で確定、07:00:05 で `tick`（1回送る） | 06:59:50 → 07:00:10 → 07:01:00 の順に `tick` | `acCount=1` のまま（時計が戻っても 07:00 を二度送らない） |
| TC-N91 | [F4] [F1] | 一覧 `[S(1,"07:00",0x7F,{power=true, mode=Heat, tempC=20})]`、06:59:30 で確定 | 07:00:00 で `tick` | `acCount=1`。`lastAc` と `acState()` がどちらも `power=true, mode=Heat, hasTemp=true, tempC=20, fan=initialSettings(Heat).fan`（Auto）`, swingV=Off, swingH=Off` |
| TC-N92 | [F4] [F1] | TC-N91 の後、一覧に `S(2,"07:01",0x7F,停止)` を足して置き換え | 07:01:00 で `tick` | `acCount=2`。`lastAc` が `power=false, mode=Heat, tempC=20`（運転だけが変わる。運転中の停止も1回送る） |
| TC-N93 | [F4] [F1] | 生成直後（停止中）。一覧 `[S(1,"07:00",0x7F,停止)]`、06:59:30 で確定 | 07:00:00 で `tick` | `acCount=1`、`lastAc.power=false`、`lastAc.mode=Cool`、`lastAc.tempC=26`（停止の予定は今が停止中でも1回送る。D-03 5.1） |
| TC-N94 | [F4] | 一覧 `[S(1,"07:00",0x7F,PA), S(2,"07:00",0x7F,PB)]`、06:59:30 で確定 | 07:00:00 で `tick` | その1回の `tick` で `acCount=2`。`lastAc` と `acState()` がどちらも `mode=Heat, tempC=21`（同じ分は一覧の順に続けて送り、後の件が最後の状態） |
| TC-N95 | [F4] | synced、`nowCount` を控える | `replaceSchedules` を有効な2件で呼ぶ | 戻り値 `None`、`nowCount` 変化なし、`acCount=0` |
| TC-N96 | [F4] | 一覧は空、06:59:30 で確定、07:00:00 で `tick`（07:00 を判定済み） | 07:00:20 に `[S(1,"07:00",0x7F,PA)]` へ置き換え、07:00:21・07:00:59・07:01:00 で `tick` | `acCount=0` |
| TC-N97 | [F4] | 一覧 `[S(1,"07:00",0x7F,PA)]`、06:59:59 で `tick`（nowMs=N） | `[S(2,"07:00",0x7F,PB)]` に置き換え、07:00:00 で `tick`（nowMs=N+1000） | `acCount=1`、`lastAc.mode=Heat`・`tempC=21`（古い一覧の PA は送られない） |
| TC-N98 | [F4] | 一覧 `[S(1,"07:00",0x7F,PA)]`、06:59:30 で確定 | hour=24 の件で `replaceSchedules`（失敗）、07:00:00 で `tick` | 置き換えは `TimeOutOfRange`。`acCount=1`、`lastAc.mode=Cool`・`tempC=25`（元の一覧で判定） |
| TC-N209 | [F4] [F1] | `Mt`（無ければ IGNORE）。一覧 `[S(1,"07:00",0x7F,{power=true, mode=Mt})]`、06:59:30 で確定 | 07:00:00 で `tick` | `acCount=1`、`lastAc.mode=Mt`、`lastAc.hasTemp=false`、`lastAc.power=true` |
| TC-N210 | [F4] [F2] | 生成直後（停止中）に `applyAc({mode=Heat, tempC=22})`、`applyAc({fan=F1})`、`applyAc({mode=Cool})`（どれも送らない。`acCount=0`）。一覧 `[S(1,"07:00",0x7F,{power=true, mode=Heat})]`、06:59:30 で確定 | 07:00:00 で `tick` | `acCount=1`、`lastAc` が `power=true, mode=Heat, tempC=22, fan=F1`（モードだけの予定は、そのモードの最後の設定で運転する。D-03 5.1） |

#### B-5. JSON（schedule_json）

前提：`ScheduleParseResult r;`。「List」「Export」は `ScheduleJsonKind`。**例の3件**＝D-03 6.1 の3つの実例を `cap::` から作ったもの：id 1（有効、07:00、平日、`{"power":true,"mode":"heat","temp":20}`）、id 2（有効、23:30、毎日、`{"power":false}`）、id 3（無効、06:45、土日、`{"power":true,"mode":"auto","fan":"<toString(F1)>"}`。確定値では `"min"` で D-03 の例そのもの）。「正しい件」＝例の id 1 の件。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N100 | [F4-IO] | 例の3件を `replaceAll` した一覧 A | `schedulesToJson(A, Export, 同期済み T20)` → `schedulesFromJson(…, Export, &r)` → 新しい一覧 B に `replaceAll` | すべて成功。B の3件の id（1, 2, 3）、enabled、時刻、days、target、ac の各項目（power・mode・tempC・fan の有無と値）が A と一致 |
| TC-N101 | [F4-IO] | 空の一覧 | `schedulesToJson(空, Export, T20)` を読み直す | `version=1`、`schedules` は空配列。`schedulesFromJson(…, Export)` は true、`count=0` |
| TC-N102 | [F4] | 例の3件を `{"schedules":[…]}` にした文字列 | `schedulesFromJson(…, List, &r)` | true、`count=3`。[0]：id 1、days 0x3E、`ac={power=true, mode=Heat, tempC=20}`（fan 無し）。[1]：id 2、days 0x7F、`ac.power=false` だけ。[2]：id 3、`enabled=false`、06:45、days 0x41、target Ac、`ac={power=true, mode=Auto, fan=F1}`（tempC 無し） |
| TC-N103 | [F4] | days=0x41（土・日）の件 | `schedulesToJson(List)` | その件の `days` が `["sun","sat"]`（日曜始まり） |
| TC-N104 | [F4] | 例の3件 | `schedulesToJson(List)` の文字列 | `"action":{"power":true,"mode":"heat","temp":20}`、`"action":{"power":false}`、`"action":{"power":true,"mode":"auto","fan":"<toString(F1)>"}` を部分文字列として含む（値のあるキーだけ、順は power, mode, temp, fan）。`swingV`・`swingH` の文字列を含まない。全件の `"target":"ac"` |
| TC-N105 | [F4-LIMIT] | 空の一覧 | `schedulesToJson(List)` を読み直す | `max=kScheduleMax`（10）、`schedules=[]`、`version` キーが無い |
| TC-N106 | [F4-IO] | 同期済み local 2026-09-25 20:00:00、および 2026-01-05 07:03:09 | `schedulesToJson(Export)` の `exportedAt` | `"2026-09-25T20:00:00+09:00"`、`"2026-01-05T07:03:09+09:00"`（ゼロ埋め）。`max` キーが無い |
| TC-N107 | [F4-IO] [N-TIME] | 未取得 `{false,{},0}` | `schedulesToJson(Export)` | `exportedAt` が null |
| TC-N108 | [F4-IO] | 同期済み 2026-09-25 20:00、2026-01-05 07:03、未取得 | `exportFilename` | `irhub-schedules-20260925-2000.json`、`irhub-schedules-20260105-0703.json`、`irhub-schedules.json` |
| TC-N109 | [F4-IO] | 正しい Export 本文を空白で 8192 バイトちょうど、8193 バイトに伸ばしたもの | `schedulesFromJson(…, Export)` | 8192：true。8193：false、`r.error="body too large"` |
| TC-N110 | [F4-IO] | — | 本文 `{`、`""`（空）、`[]` を Export で | 3つとも false、`invalid json` |
| TC-N111 | [F4-IO] | — | `{"schedules":[]}` を Export で | false、`version missing` |
| TC-N112 | [F4-IO] | — | `{"version":2,"schedules":[]}`、`{"version":"1","schedules":[]}` を Export で | どちらも false、`unsupported version` |
| TC-N113 | [F4-IO] | — | `{"version":1}`、`{"version":1,"schedules":{}}` | どちらも false、`schedules missing` |
| TC-N114 | [F4-LIMIT] [F4-IO] | 正しい件を `kScheduleMax+1`（11）件、および `kScheduleMax`（10）件（id は付けない） | Export で読む | 11件：false、`too many schedules (max 10)`（`kScheduleMax` から `snprintf`）。10件：true、`count=10` |
| TC-N115 | [F4-IO] | 1件目の `time` を `"7:00"`、`"07:00:00"`、`"24:00"`、`"07:60"` に | Export で読む | 4つとも false、`schedules[0].time: must be HH:MM`（D-03 6.1：`"24:00"` は形の規則で不可） |
| TC-N116 | [F4-IO] | 1件目の days を `["mo"]`、`["mon","mon"]`、`[]` に | Export で読む | `schedules[0].days: unknown day "mo"`、`schedules[0].days: duplicate day`、`schedules[0].days: empty` |
| TC-N117 | [F4-IO] [F3] | 1件目の `target` を `"tv"`、`"light"` に | Export で読む | どちらも false、`schedules[0].target: unknown target`（照明の予定は受け付けない） |
| TC-N118 | [F4-IO] | 1件目の action を `{"power":true,"tmp":20}` に | Export で読む | false、`schedules[0].action: unknown key "tmp"` |
| TC-N119 | [F4-IO] | 1件目の action を `{"power":true,"mode":"heat","temp":26.5}` に | Export で読む | false、`schedules[0].action.temp: must be integer`（`is<int>()` の要確認を兼ねる） |
| TC-N120 | [F4-IO] [F3] | 1件目の action を (a) `{"power":true,"swingV":"off"}`、(b) `{"power":true,"swingH":"off"}`、(c) `{"button":"full"}` に | それぞれ Export で読む | (a) `schedules[0].action: unknown key "swingV"`、(b) `schedules[0].action: unknown key "swingH"`、(c) `schedules[0].action: unknown key "button"`（知らないキーが `power` の有無より先）。3つとも false |
| TC-N121 | [F4-IO] | 1件目から `time` を消したもの、`enabled` を `"yes"` にしたもの | Export で読む | `schedules[0].time: missing`、`schedules[0].enabled: must be boolean` |
| TC-N122 | [F4-IO] | 3件のうち `schedules` の添字2を数値 `5` に | Export で読む | false、`schedules[2]: not an object` |
| TC-N123 | [F4] [F4-IO] | 1件目の action を `{"power":true,"temp":20}`、`{"power":false,"mode":"cool"}`、`{"power":true,"mode":"cool","temp":<kTempMaxC+1>}` | Export で読む | `schedules[0]: temp requires mode`、`schedules[0]: ac stop takes only power`、`schedules[0]: temp out of range` |
| TC-N124 | [F4-IO] | 1件目の id を `0`、`-1`、`10000`、`70000` | Export で読む | 4つとも false、`schedules[0].id: out of range`（70000 が 4464 として通らない） |
| TC-N125 | [F4-IO] | 1件目の id を `1.5`、`"1"`、`true`、`null` | Export で読む | 4つとも false、`schedules[0].id: must be integer` |
| TC-N126 | [F4-IO] | id を `1`、`9999`、キー無し | Export で読む | true。`items[0].id` が 1、9999、0 |
| TC-N127 | [F4] | `{"max":3,"schedules":[正しい件]}`（version 無し）、`{"version":1,"foo":1,"schedules":[正しい件]}` | 前者を List、後者を Export で読む | どちらも true、`count=1`（List は version 不要・max は無視、一番外側の知らないキーは無視） |
| TC-N128 | [F4-IO] | 1件目は正しく、2件目の time が `"7:00"` | Export で読む | false、`schedules[1].time: must be HH:MM`（添字が2件目を指す） |
| TC-N129 | [F4-IO] | 1件目の id を `10000000000`（`int` に入らない整数。D-03 6.1 の id の手順4、要確認の確認を兼ねる） | Export で読む | false。`r.error` が `schedules[0].id: out of range`（`is<long long>()` が使える場合）または `schedules[0].id: must be integer`（手順4を省いた場合）のどちらか。どちらだったかを `TEST_MESSAGE` に残す |
| TC-N182 | [F4-IO] | 1件目の action を `{"mode":"heat","temp":20}`（`power` 無し） | Export で読む | false、`schedules[0].action.power: missing`（D-03 6.4 の「必須キーが無い」の行） |
| TC-N183 | [F4-IO] | 1件目（`action` の外）に知らないキー `"note":"x"` を足す | Export で読む | false、`schedules[0]: unknown key "note"`（D-03 6.4 の「知らないキー」の行） |
| TC-N197 | [F4-IO] [N-TIME] | `LocalTime` を 2026-01-05 07:03:09、および 2026-09-25 20:00:00 に | `formatJstIso(t)` | `"2026-01-05T07:03:09+09:00"`（1桁の月・日・時・分・秒がゼロ埋め）、`"2026-09-25T20:00:00+09:00"` |
| TC-N198 | [F4-IO] | 同期済み local 2026-01-05 07:03:09 の `ClockReading` を now とし、例の3件の一覧 | `schedulesToJson(一覧, Export, now)` を読み直した `exportedAt` と、`formatJstIso(now.local)` を比べる | 2つが同じ文字列（`"2026-01-05T07:03:09+09:00"`。D-03 6.2） |
| TC-N211 | [F4-IO] | 1件目の action を `{"power":true,"fan":"turbo"}`、`{"power":true,"fan":"quiet"}`、`{"power":true,"mode":"fan"}`、`{"power":true,"mode":"Cool"}` に | Export で読む | 4つとも false。上から `schedules[0].action.fan: unknown fan`×2、`schedules[0].action.mode: unknown mode`×2 |
| TC-N212 | [F4] [F4-IO] | `Mt`（無ければ IGNORE）。1件目の action を `{"power":true,"mode":"<toString(Mt)>","temp":24}`、`{"power":true,"mode":"<toString(Mt)>","temp":<kTempMaxC+1>}` に | Export で読む | どちらも false、`schedules[0]: temp not supported in this mode`（確定値では自動＋温度） |
| TC-N213 | [F4] [F4-IO] | `Fx`（無ければ IGNORE）。1件目の action を `{"power":true,"fan":"<toString(Fx)>"}` に（確定値では `"max"`） | Export で読む | false、`schedules[0]: fan not supported` |
| TC-N214 | [F4-IO] | `cap::kFanChoices` の各値 f について、1件目の action を `{"power":true,"fan":"<toString(f)>"}` にした本文 | Export で読む | すべて true、`items[0].ac.fan==f`（確定値では `"min"` を含む5つ） |
| TC-N215 | [F4-IO] | 1件目に知らないキー `"note":"x"` を足し、同じ件の `time` を `"7:00"` に | Export で読む | false、`schedules[0]: unknown key "note"`（1件の中は知らないキーが先。D-03 6.1） |
| TC-N216 | [F4-IO] | 1件目の action を `{"power":true,"mode":"cool","temp":<kTempMaxC+1>}`（順8 のエラー）、2件目の `time` を `"7:00"`（順7 のエラー） | Export で読む | false、`schedules[0]: temp out of range`（件ごとに 7→8 を行うので件0 のエラーが先） |

（TC-N99・TC-N217 は欠番）

### C. test/test_api（T-04：Hub の基本動作と ApiRouter）

前提（全ケース共通）：`Hub hub(ir, clock, sensor); ApiRouter api(hub);` を新しく作る。FakeClock は未取得、FakeClimateSensor の `next={true, 25.0, 50.0}`。`req(M, path, body)` は `ApiRequest{M, path, body}`。応答本文は ArduinoJson で読み直して比べる。

#### C-1. Hub（N-BOOT・N-STATE・F5・F1）

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N130 | [N-BOOT] | 生成直後、FakeClock を同期済み T0 に | `tick` を 0, 1000, …, 9000 で10回 | `acCount=0`（一覧が空なら送らない） |
| TC-N131 | [N-STATE] [F2] | 生成直後 | `acState()`、`lastClimate()` | `acState()` が TC-N01 と同じ値。`lastClimate().valid=false` |
| TC-N132 | [F5] | 生成直後 | `tick(0)`、`tick(29999)`、`tick(30000)` | `readCount` が 1、1、2（初回は即読む、`kClimateIntervalMs`=30000） |
| TC-N133 | [F5] | 生成直後 | `tick(0xFFFFFFF0)`、`tick(0x0000751F)`、`tick(0x00007520)` | `readCount` が 1、1、2（差 29999 では読まず 30000 で読む） |
| TC-N134 | [F5] | `tick(0)` で `{true, 24.5, 55.0}` を読ませる | `next={false,0,0}` にして `tick(30000)` | `lastClimate().valid=false` |
| TC-N135 | [F1] | 運転中：生成直後に `applyAc({power=true}, &err)`（`acCount=1`） | `applyAc({tempC=27}, &err)` | true、`acCount=2`。`lastAc` の全7項目が `acState()` と一致（`power=true, mode=Cool, hasTemp=true, tempC=27, fan=Auto, swingV=Off, swingH=Off`。変えていない項目も入っている。送った風向は Off） |
| TC-N136 | [F1] | 生成直後 | `applyAc({tempC=cap::kTempMaxC+1}, &err)` | false、`err="temp out of range"`、`acCount=0`、`acState()` は初期値のまま |
| TC-N137 | [F1] | 生成直後に `applyAc({power=true}, &err)`（`acCount=1`）、その後 `ir.result=false` | `applyAc({tempC=27}, &err)` | true、`acCount=2`、`acState().tempC=27`（戻さない） |
| TC-N138 | [F1] | 運転中：生成直後（冷房）に `applyAc({power=true}, &err)`（`acCount=1`） | `applyAc({mode=Cool}, &err)` | true、`acCount=2`（運転中は同じ値のパッチでも送信1回） |
| TC-N139 | [F1] | `Mt`（無ければ IGNORE）。生成直後 | `applyAc({power=true, mode=Mt}, &err)` | true、`acCount=1`、`lastAc.mode=Mt`、`lastAc.hasTemp=false`、`lastAc.power=true` |
| TC-N140 | [F1] | (a) 生成直後（停止中）、(b) `applyAc({power=true})` 済み（運転中、`acCount=1`）。それぞれで | `applyAc({fan=Fx}, &err)`。続けて `Mt` があれば `applyAc({mode=Mt, tempC=24}, &err)` | どれも false、`acCount` は呼ぶ前のまま（(a) 0、(b) 1）、`acState()` 不変。`err` は `fan not supported`、`temp not supported in this mode`（運転中・停止中の両方で送信 0 回。D-02 テスト観点） |
| TC-N185 | [F1] | 生成直後（`power=false`） | `applyAc({mode=Heat})`、`applyAc({tempC=27})`、`applyAc({fan=F1})` を順に | 3回とも true。毎回の後で `acCount=0`（停止中に power を含まないパッチは送らない。D-02 6節）。最後の `acState()` が `power=false, mode=Heat, hasTemp=true, tempC=27, fan=F1, swingV=Off, swingH=Off` |
| TC-N186 | [F1] | TC-N185 の3回の後 | `applyAc({power=true})` | true、`acCount=1`。`lastAc` が `power=true, mode=Heat, hasTemp=true, tempC=27, fan=F1, swingV=Off, swingH=Off`（停止中の変更がまとめて送られる）で、`acState()` と全7項目一致 |
| TC-N187 | [F1] [F2] | 生成直後（`power=false`） | `applyAc({mode=Heat})` → `({tempC=27})` → `({fan=F1})` → `({mode=Cool})` → `({mode=Heat})` | 5回とも true、`acCount=0` のまま。4回目の後 `acState()` が `mode=Cool, tempC=26, fan=Auto`、5回目の後 `mode=Heat, tempC=27, fan=F1`（停止中の変更がモード別の記憶に入っている。`Hub` にはモード別の記憶を読む関数が無いので、モード切替で `acState()` に出る値で確かめる） |
| TC-N188 | [F1] | 生成直後（`power=false`） | `applyAc({power=false})` → `applyAc({power=false, tempC=24})` | 1回目：true、`acCount=1`、`lastAc` が `power=false, mode=Cool, tempC=26, fan=Auto`（今と同じ値でも power を含むので送る）。2回目：true、`acCount=2`、`lastAc.power=false`、`lastAc.tempC=24` |
| TC-N189 | [F1] | 生成直後に `applyAc({power=true})`（`acCount=1`） | `applyAc({power=false})` → `applyAc({tempC=25})` | 1回目：true、`acCount=2`、`lastAc.power=false`。2回目：true、`acCount=2` のまま、`acState().tempC=25`、`lastAc.tempC=26`（停止にした後の変更は送らない） |
| TC-N190 | [F1] | 生成直後に `applyAc({power=true})`（`acCount=1`） | `applyAc({tempC=cap::kTempMaxC+1}, &err)`（31） | false、`err="temp out of range"`、`acCount=1` のまま、`acState()` が `power=true, mode=Cool, tempC=26, fan=Auto` |
| TC-N191 | [F1] [F2] | 生成直後。`fanSupported(High)`・`(Low)` が true | D-02 4節の遷移例の表の14行を上から順に `applyAc`（`{"temp":31}` の行は `kTempMaxC+1`）。各行の前後で `acCount` の差を取る | 差が上から `1,1,1,1,1,1,0,0,1,0,0,1,1,0`（表の「送信」欄どおり）、最後の `acCount=9`。最後の `lastAc` が `power=true, mode=Cool, tempC=27, fan=Low` |
| TC-N193 | [F1] | 生成直後（`power=false`、`acCount=0`） | `applyAc({power=true, tempC=cap::kTempMaxC+1}, &err)` | false、`err="temp out of range"`、`acCount=0` のまま、`acState()` が `power=false, mode=Cool, tempC=26, fan=Auto`（停止中でも power を含むパッチの検証失敗は送信 0 回・状態不変） |
| TC-N194 | [F1] [F2] | 生成直後（`power=false`、冷房 26℃） | D-02 テスト観点の手順どおり、同じ `Hub` で `applyAc({tempC=27})` → `({mode=Heat})` → `({fan=F1})` | 3回とも true、毎回の後で `acCount=0`。3回の後の `acState()` が `power=false, mode=Heat, hasTemp=true, tempC=initialSettings(Heat).tempC`（22。冷房で変えた 27 は入らない）`, fan=F1, swingV=Off, swingH=Off` |
| TC-N195 | [F1] [F2] | TC-N194 の3回の後（同じ `Hub`） | `applyAc({power=true})` | true、`acCount=1`。`lastAc` が `power=true, mode=Heat, hasTemp=true, tempC=initialSettings(Heat).tempC, fan=F1, swingV=Off, swingH=Off` で、`acState()` と全7項目一致 |
| TC-N196 | [F1] [F2] | TC-N195 の後（同じ `Hub`、運転中） | `applyAc({mode=Cool})` | true、`acCount=2`。`lastAc` が `power=true, mode=Cool, hasTemp=true, tempC=27, fan=Auto, swingV=Off, swingH=Off`（停止中に変えた冷房の 27 が残っている） |

#### C-2. ルーティング

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N141 | [API] | 生成直後 | `GET /api/status`、`POST /api/ac {"power":true}`、`GET /api/schedules`、`PUT /api/schedules {"schedules":[]}`、`GET /api/schedules/export`、`POST /api/schedules/import {"version":1,"schedules":[]}` | 6つとも `status=200`、`contentType="application/json"` |
| TC-N142 | [API] | 生成直後 | `GET /api/foo`、`GET /api/status/`、`GET /favicon.ico` | 3つとも 404、本文 `{"error":"not found"}` |
| TC-N143 | [API] | 生成直後 | `POST /api/status`、`GET /api/ac`、`Other /api/schedules`、`GET /api/schedules/import`、`POST /api/schedules/export` | 5つとも 405、`{"error":"method not allowed"}`。`Other /api/foo` は 404 |
| TC-N144 | [API] | 生成直後 | TC-N142・N143 の要求をすべて送る | `acCount=0`、`acState()` 初期値のまま、一覧 0 件 |
| TC-N200 | [API] | 生成直後 | `POST /`、`PUT /`、`Other /`（本文はどれも `{}`） | 3つとも 404（405 ではない）、本文 `{"error":"not found"}`。`acCount=0`（D-04 10.1） |

#### C-3. GET /api/status

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N145 | [API] [F2] [N-TIME] [F5] [N-BOOT] | 生成直後（`tick` を呼ばない） | `GET /api/status` | 200。一番外側のキーが `ac`・`acCapabilities`・`climate`・`clock` の4つだけ。`ac` のキーが `power`・`mode`・`temp`・`fan` の4つだけで、値が `{"power":false,"mode":"cool","temp":26,"fan":"auto"}`。`climate={"valid":false,"temperature":null,"humidity":null}`、`clock={"synced":false,"now":null}`。`acCount=0` |
| TC-N146 | [API] [F1] | 生成直後 | `GET /api/status` の `acCapabilities` | キーが `modes`・`tempMin`・`tempMax`・`tempStep`・`tempModes`・`fan` の6つだけ（`swingV`・`swingH`・`timer`・照明のキーが無い）。`modes=["auto","cool","dry","heat"]`、`tempMin=kTempMinC`（16）、`tempMax=kTempMaxC`（30）、`tempStep=kTempStepC`（1）、`tempModes`＝`AcMode` の順で `cap::tempSupported` が true のモードの文字列（確定値 `["cool","dry","heat"]`）、`fan`＝`kFanChoices` の順の `toString`（確定値 `["auto","min","low","medium","high"]`） |
| TC-N147 | [N-TIME] | FakeClock を同期済み local 2026-01-05 07:03:09 に | `GET /api/status` | `clock.synced=true`、`clock.now="2026-01-05T07:03:09+09:00"` |
| TC-N148 | [F5] | `next={true, 24.46, 55.24}` で `tick(0)` | `GET /api/status` | `climate.valid=true`、`temperature=24.5`、`humidity=55.2` |
| TC-N149 | [F5] | TC-N148 の後、`next={false,0,0}` で `tick(30000)` | `GET /api/status` | `climate.valid=false`、`temperature` と `humidity` が null |
| TC-N150 | [F1] | `Mt`（無ければ IGNORE）。`POST /api/ac {"power":true,"mode":"<toString(Mt)>"}` | その応答と `GET /api/status` | どちらも `ac.mode=toString(Mt)`、`ac.temp` が null（キーはある） |
| TC-N151 | [API] | 生成直後 | `GET /api/status` を3回 | `acCount=0`、`acState()` 初期値のまま |
| TC-N199 | [API] [N-TIME] [F4-IO] | FakeClock を同期済み local 2026-01-05 07:03:09 に固定（`seq` は空） | `GET /api/status` と `GET /api/schedules/export` | `clock.now` と export 本文の `exportedAt` がどちらも `"2026-01-05T07:03:09+09:00"` で同じ文字列（どちらも `formatJstIso`。D-04 11節） |

#### C-4. POST /api/ac

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N152 | [F1] [API] | 生成直後に `POST /api/ac {"power":true}`（`acCount=1`） | `POST /api/ac {"temp":27}` | 200、本文が `{"ac":{…}}` で `ac.power=true`、`ac.temp=27`、`ac.mode="cool"`、`ac.fan="auto"`。`acCount=2`、`lastAc` が `acState()` と一致 |
| TC-N153 | [F1] | 生成直後 | `{"power":true,"mode":"cool","temp":26,"fan":"auto"}` | 200、応答の `ac` が送った4項目と同じ値、キーが4つだけ |
| TC-N154 | [F1] [F2] | `{"temp":27}` を送った後 | `{"mode":"heat"}` → `{"mode":"cool"}` | 1回目 `ac.mode="heat"`、`ac.temp=initialSettings(Heat).tempC`（22）。2回目 `ac.mode="cool"`、`ac.temp=27` |
| TC-N155 | [F1] [API] | 生成直後 | `{"temp":26.5}`、`{"temp":"26"}`、`{"temp":null}`、`{"temp":true}` | 4つとも 400、`{"error":"temp: must be integer"}` |
| TC-N156 | [F1] [API] | 生成直後 | `{"power":1}`、`{"mode":3}`、`{"mode":"Cool"}`、`{"mode":"fan"}` | `power: must be boolean`、`mode: must be string`、`mode: unknown mode`、`mode: unknown mode`（400） |
| TC-N157 | [F1] [API] | 生成直後 | `{"fan":1}`、`{"fan":"turbo"}`、`{"fan":"quiet"}` | `fan: must be string`、`fan: unknown fan`、`fan: unknown fan`（400。静音の文字列は `min`） |
| TC-N158 | [F1] [API] | 生成直後 | `{"timer":60}`、`{"power":true,"tmp":1}` | `unknown key "timer"`、`unknown key "tmp"`（400。本体タイマーを作っていない） |
| TC-N159 | [F1] [API] | 生成直後 | `{}`、`{"temp":<kTempMaxC+1>}`（31）、`{"temp":<kTempMinC-1>}`（15）、`{"fan":"<toString(Fx)>"}`（確定値 `"max"`。Fx が無ければこの1つを省く） | `no ac fields`、`temp out of range`、`temp out of range`、`fan not supported`（400） |
| TC-N160 | [F1] | 生成直後 | TC-N155〜N159 の要求を全部送る | `acCount=0`、`acState()` 初期値のまま |
| TC-N161 | [API] | 生成直後 | `{"x":1,"temp":"a"}`、`{"fan":"turbo","temp":"a"}` | `unknown key "x"`（知らないキーが先）、`temp: must be integer`（power, mode, temp, fan の順） |
| TC-N162 | [API] | 生成直後 | 本文 `""`、`[1]`、`"x"`、`{"temp":` | 4つとも 400、`invalid json` |
| TC-N163 | [API] | 生成直後に `POST /api/ac {"power":true}`（`acCount=1`） | `{"temp":27}` を空白で 512 バイトにしたもの、513 バイトにしたもの | 512：200、`ac.temp=27`、`acCount=2`。513：400、`body too large`、`acCount` は 2 のまま |
| TC-N164 | [F1] | 生成直後に `POST /api/ac {"power":true}`（`acCount=1`）、その後 `ir.result=false` | `POST /api/ac {"temp":27}` | 200、`ac.power=true`、`ac.temp=27`、`acCount=2`（sendAc が false を返しても 200） |
| TC-N192 | [F1] [API] | 生成直後（`power=false`） | `POST /api/ac {"temp":27}` → `POST /api/ac {"power":true}` | 1回目：200、`ac.power=false`、`ac.temp=27`、`acCount=0`（停止中は状態だけ変えて送らない）。2回目：200、`ac.power=true`、`ac.temp=27`、`acCount=1`、`lastAc.power=true`、`lastAc.tempC=27` |
| TC-N218 | [F1] [API] | `Mt`（無ければ IGNORE）。生成直後（冷房） | (1) `{"mode":"<Mt>","temp":24}`。(2) `{"power":true,"mode":"<Mt>"}`。(3) `{"temp":24}`。(4) `{"temp":<kTempMaxC+1>}` | (1) 400 `temp not supported in this mode`、その後の `GET /api/status` の `ac.mode="cool"`（モードも変わらない）、`acCount=0`。(2) 200、`acCount=1`。(3)(4) どちらも 400 `temp not supported in this mode`（範囲外でも `temp out of range` ではない）、`acCount=1` のまま |
| TC-N219 | [F1] [API] | 生成直後に `{"power":true}`（`acCount=1`） | `cap::kFanChoices` の各値 f について順に `{"fan":"<toString(f)>"}` | すべて 200、応答の `ac.fan==toString(f)`、1回ごとに `acCount` が1増える（確定値では `"min"` を含む5回） |

#### C-5. 照明と風向を作っていないこと

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N165 | [F3] [API] | 生成直後 | `POST /api/light {"button":"power"}`、`GET /api/light`、`PUT /api/light {}` | 3つとも 404（405 ではない）、本文 `{"error":"not found"}`。`acCount=0`、`acState()` 初期値のまま（D-04 5節） |
| TC-N166 | [F3] [F1] [API] | 生成直後 | `POST /api/ac {"button":"power"}` | 400、`unknown key "button"`。`acCount=0` |
| TC-N167 | [F1] [API] | 生成直後 | `{"swingV":"auto"}`、`{"swingV":"off"}`、`{"swingH":"off"}`、`{"power":true,"swingV":"off"}` | 順に `unknown key "swingV"`、`unknown key "swingV"`、`unknown key "swingH"`、`unknown key "swingV"`（400。値が `"off"` でも受け付けない）。4回の後 `acCount=0`、`acState().power=false`（4つ目の `power` も効かない） |
| TC-N168 | [F1] [API] | 生成直後 | `POST /api/ac {"power":true}` → `POST /api/ac {"temp":27}` | 2回とも応答の `ac` のキーが `power`・`mode`・`temp`・`fan` の4つだけ（`swingV`・`swingH` が無い）。`lastAc.swingV=Off`、`lastAc.swingH=Off` |

#### C-6. スケジュール API

「有効な件」＝`{"enabled":true,"time":"07:00","days":["mon"],"target":"ac","action":{"power":true}}`。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N169 | [F4] [F4-LIMIT] | 生成直後 | `GET /api/schedules` | 200、`{"max":10,"schedules":[]}`（max は `kScheduleMax`） |
| TC-N170 | [F4] | 生成直後 | `PUT` で id 無しの有効な件2件 → `GET` | PUT：200、`schedules[0].id=1`、`[1].id=2`。GET も同じ内容 |
| TC-N171 | [F4-LIMIT] | TC-N170 の2件 | `PUT` で有効な件を `kScheduleMax+1`（11）件 | 400、`too many schedules (max 10)`。`GET` は元の2件（id 1, 2） |
| TC-N172 | [F4] | TC-N170 の2件 | `PUT` で1件目の time が `"24:00"` | 400、`schedules[0].time: must be HH:MM`。一覧は変わらない |
| TC-N173 | [F4] | TC-N170 の2件 | `PUT` で id 1 の件を2つ | 400、`schedules[1]: duplicate id`。一覧は変わらない |
| TC-N174 | [F4-IO] | 例の3件（B-5）を PUT 済み。PUT の後に FakeClock の `seq` を `[同期済み local 2026-09-25 20:00:59, 同期済み local 2026-09-25 20:01:00]` にする（呼ぶたびに1秒進む時計） | `GET /api/schedules/export` | 200、`contentType="application/json"`、本文 `version=1`、3件。`downloadFilename="irhub-schedules-20260925-2000.json"` かつ `exportedAt="2026-09-25T20:00:59+09:00"`（ファイル名と `exportedAt` が同じ1回の時刻から作られる。D-04 8節） |
| TC-N175 | [F4-IO] [N-TIME] | FakeClock 未取得 | `GET /api/schedules/export` | 200、`downloadFilename="irhub-schedules.json"`、`exportedAt` が null |
| TC-N176 | [F4-IO] | TC-N174 の本文を控え、`PUT {"schedules":[]}` で空にする | 控えた本文をそのまま `POST /api/schedules/import` → `GET` | import：200、`max=10` と3件。GET の3件が id 1, 2, 3 を含め元と一致 |
| TC-N177 | [F4-IO] | 2件入った一覧 | import に `{"schedules":[]}`、`{"version":2,"schedules":[]}` | `version missing`、`unsupported version`（400）。`GET` は元の2件 |
| TC-N178 | [F4-IO] | id 1, 2 の2件 | `{"version":1,"schedules":[有効な件に "id":5]}` を import → `GET` | 200。一覧は id 5 の1件だけ（混ぜずに置き換える） |
| TC-N179 | [N-TIME] [F4] | FakeClock 未取得 | GET schedules、PUT（2件）、GET export、POST import | 4つとも 200 |
| TC-N180 | [F4] [N-BOOT] | FakeClock 同期済み | TC-N169〜N179 の要求をすべて送る | `acCount=0` |
| TC-N184 | [F4] [F4-IO] [API] | TC-N170 の2件（id 1, 2） | `PUT` を3通り：(a) 1件目の id が `10000000000`、(b) 1件目の action が `{"mode":"heat"}`（`power` 無し）、(c) 1件目に知らないキー `"note":"x"`。それぞれの後に `GET` | 3つとも 400。(a) の `error` は `schedules[0].id: out of range` か `schedules[0].id: must be integer` のどちらか、(b) は `schedules[0].action.power: missing`、(c) は `schedules[0]: unknown key "note"`。3回とも `GET` が元の2件（id 1, 2、内容も同じ） |
| TC-N201 | [F4-IO] [API] | TC-N170 の2件（id 1, 2） | `POST /api/schedules/import` に `{"version":1,"schedules":[有効な件に "id":3, 有効な件に "id":3]}` → `GET` | 400、`{"error":"schedules[1]: duplicate id"}`（`badIndex` から ApiRouter が組み立てる。D-03 6.3 順9）。`GET` は元の2件 |
| TC-N220 | [F4] [F3] [API] | TC-N170 の2件（id 1, 2） | `PUT` を2通り：(a) 1件目の `target` が `"light"`、(b) 1件目の action が `{"power":true,"swingV":"off"}`。それぞれの後に `GET` | (a) 400 `schedules[0].target: unknown target`、(b) 400 `schedules[0].action: unknown key "swingV"`。2回とも `GET` が元の2件 |

#### C-7. エラー本文と認証

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-N181 | [API] [N-AUTH] | 生成直後 | `POST /api/ac {"swingV":"auto"}`（400）、`GET /api/foo`（404）、`POST /api/status`（405）の本文を ArduinoJson で読む。あわせて、どの要求も認証の情報なし（`ApiRequest` は method・path・body だけ）で送る | 3つとも JSON として読め、キーは `error` の1つだけ。値は `unknown key "swingV"`（`"` を含む）、`not found`、`method not allowed`。認証の情報なしの `GET /api/status` が 200 |

---

## テストケース（実機）

実機ケースの使い方：各ゲート（H-0〜H-6）で、人がこの表を上から順に行い、各行の「期待結果」を満たせば ✓ を付ける。1つでも満たさなければ、そのゲートは fail とし、行の ID と見えた結果をメモに残す。「記録する」とだけ書いた項目は合否に入れず、値をメモに残す。書き込み（upload）は人が行う。シリアルモニタは `pio device monitor`（115200）。「IP」は ESP32 の IP（シリアルの `[wifi] connected ip=` の値。固定の確認は TC-H45）。

時刻の書き方：「T+2分」「U+4分」などの T・U は、その手順を始めたときの時計の **HH:MM**（秒は切り捨て）とし、その分に2（4）を足した HH:MM を登録する（例：T が 20:58:40 なら T=20:58、T+2分=21:00）。スケジュールは分単位で動く（D-03）ので、「T+2分に動く」は「時計が T+2分の HH:MM になってから、その分の中で動く」の意味。その分の残りが 30 秒未満のときは、次の分まで待ってから T を取り直す。

LED の見方：赤外線 LED はスマホのカメラ越しに見ると光って見える。「LED が光らない」はカメラの画面で一度も光らないこと。

### フェーズ0（H-0）：ESP32 単体の動作確認（H-0 で実施済み）

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H01 | [HW-PINS] | ESP32-DevKitC-VE を USB（A-microB）で PC につなぐ。他の部品はつながない | `pio run -e esp32 -t upload` → `pio device monitor` を開き 10 秒待つ | 1 秒ごとに1行（`irhub phase0 tick`）が出て、10 秒で 9〜11 行。文字化けしない（115200） |

### フェーズ1（H-1）：赤外線の受信と解析（H-1 で実施済み。記録は `docs/hw/phase1-capture.md`）

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H02 | [HW-PINS] | 受信モジュール OSRB38C9AA：OUT→IO14、VCC→3V3、GND→GND（5V につながない） | `pio run -e dump -t upload` → monitor。エアコンのリモコン OAR-N9 を 30cm から受光部へ向けて1回押す | 押すたびに受信の出力が出る。押していないときは何も出ない |
| TC-H03 | [F1] | TC-H02 のまま | OAR-N9 で「運転」を押す | `Protocol  : HITACHI_AC296` と、`Mode`・`Temp` を含む `Mesg Desc.` が出る |
| TC-H04 | [F1] | 同上 | ▼を下限まで、▲を上限まで押す | 下限で `Temp: 16C`。上限はリモコン表示 32℃ で、解析は温度欄が5ビットのため 31℃ までしか表せない（受信記録では最大で `Temp: 0C`）。→ 範囲 16〜30℃ に決定（D1） |
| TC-H05 | [F1] | 同上 | モード（冷房・暖房・除湿・自動）、風量の全段階、風向 上下・左右を順に押す | モードが `3 (Cool)`・`6 (Heat)`・`5 (UNKNOWN)`・`7 (Auto)` と出て（除湿は 5。ライブラリ v2.9.0 の表示は `UNKNOWN`。docs/hw/phase1-capture.md「# 除湿」と同じ）、自動は `Temp: 0C`（温度指定なし）。風量5段階（自動・静音・弱・中・強）が出る。風向は記録だけ（作らない） |
| TC-H06 | [F1] | 同上 | 本体タイマーの入・切を押す | 出力を記録する（本体タイマーは作らない。F1-TIMER 対象外） |
| TC-H08 | [F1] | TC-H03〜H06 の出力 | `docs/hw/phase1-capture.md` に貼り、`/decide D1` を行う | ファイルにプロトコル、温度範囲、モード、風量、風向・タイマーの受信結果がある。照明のリモコンは電波式と分かった旨が記録されている（F3 対象外） |

### フェーズ2（H-2）：赤外線の送信

前提：I-06 の本物の送信層を書き込み済み。赤外線 LED 2個（IO23→1kΩ→2SC1815、5V→100Ω→LED）を配線済み。操作は PC の端末から `curl -X POST -H 'Content-Type: application/json' -d '<本文>' http://IP/api/ac`（「送る：`<本文>`」と書く）。応答はすべて表示して控える。

H-2 で確かめる送信の中身（PROTO）：受信した信号には押したボタンを表すバイト `state[11]` がある（純正リモコンは運転 0x13・モード 0x41・風量 0x42・温度 0x43／0x44）。IRac はこのバイトを押したボタンに合わせて埋めない（D-01 7節・D-02 7節）。送られる値は、`IRHitachiAc296::stateReset` が入れる固定の 0x43 のままになる見込み（D-06 の審査の指摘。設計の不足1）。また自動モードの温度欄は IRac が **1**、純正が **0**。TC-H11〜H15・H77 で、この信号を運転・モード・温度・風量の変更それぞれでエアコンが受け付けるかを見る。受け付けない変更が1つでもあれば H-2 は fail とし、どの行・どの本文で受け付けなかったか（受信音が鳴らない、表示が変わらない、など）を記録する（対処は I-06 で `src/ir_sender_esp32.cpp` の中だけで行う。D-01 7節）。

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H09 | [F1] | — | `pio run -e esp32` | ビルドが成功し、`-Wswitch`（列挙の扱い漏れ）の警告が出ない（`decode_type_t::HITACHI_AC296` と `IRac::sendAc` が使えている） |
| TC-H10 | [N-BOOT] [HW-PINS] | スマホのカメラで LED を映しておく。エアコンの今の状態（運転／停止、表示）を控える | USB を抜き差しする（電源投入）→ 30 秒待つ。続けて EN ボタンでリセット → 30 秒待つ | どちらでも LED が一度も光らない。エアコンの状態が変わらず、受信音も鳴らない。シリアルに `[boot]` の行とリセット理由が出る |
| TC-H11 | [F1] [HW-PINS] | エアコン停止中 | 送る：`{"power":true,"mode":"cool","temp":26}` | 応答 200 `{"ac":{"power":true,"mode":"cool","temp":26,"fan":"auto"}}`。エアコンが受信音を鳴らし、冷房 26℃ で運転を始める（`state[11]` が運転の 0x13 でなくても受け付けられる） |
| TC-H12 | [F1] | TC-H11 の後（運転中） | 送る：`{"temp":27}` → 10 秒後に `{"power":false}` | 1回目でエアコンの設定温度が 27℃ になる。2回目で停止する。どちらも応答 200 |
| TC-H13 | [F1] | エアコンを `{"power":true,"mode":"cool","temp":26}` で運転させておく | 送る：`{"temp":27}`（上げ）→ `{"temp":26}`（下げ）→ `{"temp":16}`（下限）→ `{"temp":30}`（上限）→ `{"temp":26}` を 10 秒おきに | 5回とも応答 200 で、エアコンの設定温度が 27 → 26 → 16 → 30 → 26 と送った値になる（`state[11]` が押したボタンに合わせて変わらなくても、上げ・下げの両方が効く。純正なら上げは 0x44） |
| TC-H14 | [F1] | 運転中（冷房 26℃） | 送る：`{"fan":"min"}` → `{"fan":"low"}` → `{"fan":"medium"}` → `{"fan":"high"}` → `{"fan":"auto"}` を 15 秒おきに | 5回とも応答 200、受信音が鳴る。エアコンの吹き出しの風の強さが 静音 → 弱 → 中 → 強 と段階どおりに強くなり、最後は自動になる（本体に風量の表示があればその表示も控える。風の強さの見分け方は設計に無い：設計の不足3） |
| TC-H77 | [F1] [F2] | 運転中（冷房 26℃） | 送る：`{"mode":"heat"}` → `{"mode":"dry"}` → `{"mode":"auto"}` → `{"mode":"cool"}` を 30 秒おきに | 4回とも応答 200、受信音が鳴る。エアコンの運転モードがそのたびに 暖房 → 除湿 → 自動 → 冷房 に変わる（本体の表示または吹き出しの冷温で確かめる）。自動の応答は `"temp":null`。自動のときエアコンが受け付けること＝温度欄 1（純正は 0）でも受け付けること。最後の冷房の応答は `"temp":26` |
| TC-H78 | [F1] | 受信側を別に用意できる場合だけ行う（例：受信モジュールと `env:dump` を書き込んだ2台目の ESP32。用意できなければ「対象外（受信側なし）」と書いて ✓） | 受信側を ESP32 の LED の前 30cm に置き、TC-H11・H13 の `{"temp":27}`・H14 の `{"fan":"high"}`・H77 の `{"mode":"auto"}` を送り直す | 受信側に `Protocol : HITACHI_AC296` が出る。各送信の `state[11]` の値と、自動のときの温度欄の値（`Temp:` の表示）を記録する（見込み：`state[11]` はすべて 0x43、自動は `Temp: 1C` 相当）。合否に入れない |
| TC-H15 | [F1] | ESP32 を実際の設置場所に置く。エアコンを止めておく | 送る：`{"power":true,"mode":"cool","temp":26}` → 10 秒後に `{"power":false}` を1組として、10 秒おきに3組（送信は計6回。どちらも power を含むので毎回送る） | 6回の送信のうち6回エアコンが反応する（運転開始・停止を3回ずつ繰り返し、最後は停止）。応答はすべて 200（設置場所から届く） |
| TC-H16 | [F2] | リセット直後（初期値の状態）に `{"power":true}` で運転中（冷房 26℃） | 送る：`{"mode":"heat"}` → `{"mode":"dry"}` → `{"mode":"auto"}`（各 1 分ずつ運転） | 合否：応答の `ac` が 暖房 `temp:22, fan:"auto"`、除湿 `temp:26, fan:"auto"`、自動 `temp:null, fan:"auto"`（いずれも D2 確定の初期値）で、エアコンがそれぞれのモードで動く（受信音と運転の様子）。記録のみ（合否に入れない）：除湿で設定温度が効いたか、自動でどう動いたか。除湿の温度が無視されると分かったら人が `kTempSupported[Dry]` を見直す。受信側を別に用意できる場合だけ（TC-H78 と同じ扱い）、送信信号のダンプ（暖房 22℃・風量自動か）を記録する。合否に入れない |
| TC-H17 | [F1] [API] | 運転中。LED をカメラで映す | 送る：`{"temp":99}`、`{"temp":15}`、`{"fan":"max"}`、続けて `{"mode":"auto"}` の後に `{"temp":24}` | 順に 400 `{"error":"temp out of range"}`、400 `temp out of range`、400 `fan not supported`、（`{"mode":"auto"}` は 200）、400 `temp not supported in this mode`。400 の3回と最後の1回では LED が光らず、エアコンは変わらない |
| TC-H68 | [F1] | エアコンと ESP32 がどちらも停止（例：TC-H12 の後）。スマホのカメラで LED を映す | 送る：`{"mode":"heat"}` → `{"temp":24}` → `{"fan":"high"}` を 5 秒おきに → 5 秒後に `{"power":true}` → 確かめたら `{"power":false}` | 最初の3回：応答 200、応答の `ac.power` が false、`ac` がそれぞれ `mode="heat"`、`temp=24`、`fan="high"` になる。LED が光らず、エアコンの受信音が鳴らず、エアコンは停止したまま（停止中に power を含まない変更は送らない。D-02 6節）。`{"power":true}`：LED が1回光り、エアコンが暖房・24℃・風量 強で運転を始める（停止中に変えた設定がまとめて届く）。最後の `{"power":false}` で停止する |
| TC-H69 | [F1] [N-BOOT] | エアコンを `{"power":true,"mode":"cool","temp":26}` で運転させておく。LED をカメラで映す | ESP32 の EN ボタンでリセット → 30 秒待つ → `curl http://IP/api/status` → 送る：`{"temp":25}` → 10 秒待つ | `/api/status` の `ac.power` が false（N-STATE）。`{"temp":25}` の応答は 200・`ac.power=false`・`ac.temp=25`。LED が光らず、エアコンは冷房 26℃ で運転を続ける（再起動後に温度だけ変えても停止信号が出ない） |

### フェーズ3（H-3）：温湿度

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H18 | [F5] [HW-PINS] | DHT20：SDA→IO21、SCL→IO22（各 10kΩ で 3.3V へプルアップ）、VDD→3V3、0.1µF を VDD–GND | リセットしてシリアルを 70 秒見る | 起動から 1 秒以内に `[dht20] xx.xC xx.x% (NNms)` の1回目、その後 30 秒ごと（±1 秒）に1行。`NNms` を記録する（見込み 100 ms 前後。D-06 3.2） |
| TC-H19 | [F5] | 室温計を DHT20 の横に 10 分置く | シリアルの値と室温計を3回（10 分おき）比べる | 室温と湿度がシリアルに出る。3回の差を数値で記録する（合否の閾値は設計に無い：設計の不足2） |
| TC-H20 | [F5] | 動いている状態 | SDA の線を抜いて 40 秒待ち、`curl http://IP/api/status` → 線を戻して 40 秒待ち、もう一度 | 抜いた後：シリアルに `[dht20] read error`、`climate.valid=false`、`temperature` と `humidity` が null。戻した後：`valid=true` で値が出る |

### フェーズ4（H-4）：画面と API（F1・F5）＋起動・ネットワーク・時刻

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H21 | [UI] [F3] | スマホ（iOS Safari と Android Chrome）を家の Wi-Fi に | `http://IP/` を開く | 1ページに上から 上部バー（室温・湿度、時刻、NTP の状態）、エアコン、スケジュールのカードが出る。照明のカード、風向の選択、本体タイマーのボタンが無い。日本語が化けない。題名が「IRハブ」。SwitchBot のロゴ・名称が無い |
| TC-H22 | [F1] [UI] | TC-H21 の画面、エアコン停止 | 運転ボタン → モードの「冷房」→ ＋を上限まで → −を下限まで → 風量の5つのボタン（自動・静音・弱・中・強）を順に → 運転ボタン | 運転中の操作は毎回エアコンが画面どおりに動き、画面の表示も変わる。温度が 30℃ で＋、16℃ で−が押せなくなる（`disabled`）。風量のボタンが 自動・静音・弱・中・強 の5つ並ぶ。最後に停止する |
| TC-H23 | [F1] [F2] [UI] | TC-H21 の画面、エアコン運転中（冷房 26℃） | モードの「自動」を押す → 30 秒後に「冷房」を押す | 自動：温度の −／＋ と数値が消え「自動では温度を指定できません」が出る。エアコンが自動運転になる。冷房：押した直後は温度が `--℃` で −／＋ が押せず、応答の後 `26℃` が出て −／＋ が押せる。エアコンが冷房 26℃ になる |
| TC-H70 | [F1] [UI] | TC-H21 の画面。エアコン停止（運転ボタンが「停止中」）。LED をカメラで映す | ＋を1回 → 5 秒後にモードの「暖房」→ 5 秒後に風量のボタンを今と違うもの1つ → 5 秒後に運転ボタン → 確かめたら運転ボタンでもう一度停止 | 最初の3回：押すたびに画面の表示が変えた値になり、`#ac-msg` に「停止中のため設定だけ変えました（運転を押すとまとめて送ります）」が出て約 3 秒で消える。LED が光らず、受信音が鳴らず、エアコンは停止したまま。運転ボタン：LED が1回光り、エアコンが画面の設定（暖房・画面の温度・選んだ風量）で運転を始める。このとき「停止中のため…」は出ない。最後の停止で止まる |
| TC-H24 | [F5] [UI] | 同上 | 室温・湿度を見る → DHT20 に息を 5 秒かける → 60 秒待つ | 小数1桁で表示される（例 `室温 24.5℃  湿度 55.2%`）。息をかけてから 60 秒以内に湿度の表示が上がる（センサー周期 30 秒＋画面の読み直し 30 秒の最悪値。D-05 の「30 秒以内」とは違う：設計の不足5） |
| TC-H25 | [N-RESP] | エアコンを運転中にしておく（停止中は＋で赤外線を送らない）。スマホのスロー動画（240fps など）で、画面のボタンと LED を同時に映す | (a) ＋と−を交互に 10 回、1回ごとに LED が消えてから 2 秒以上空けて押す。(b) ＋を素早く3回、1回目の LED が光り終わる前に（1回目から 0.3 秒以内に）3回押し終える。押す間隔を動画で記録する | (a) 10回とも、指がボタンに触れたコマから LED が光り始めるコマまで 1.0 秒以内。(b) 1回目の押下から LED が光り始めるまで 1.0 秒以内。2回目・3回目を1本目の応答前（1回目の LED が消える前）に押せていれば、LED が光るのは2回（1回目と、まとめられた2・3回目。D-05 4.4）。動画で3回目が1回目の LED の消えた後だったときは判定せず押し直す。エアコンの最後の設定温度が押す前＋3℃ |
| TC-H79 | [N-RESP] [UI] [F1] | エアコンを運転中（冷房、風量 自動）にしておく。TC-H25 と同じくスロー動画で画面と LED を映す | ＋ → 風量「強」→ モード「暖房」を、それぞれの間を 0.3 秒以内にして素早く押す。これを3回くり返す（間は 30 秒空ける。2回目・3回目は −／風量「自動」／モード「冷房」などに変えてよい） | 3回とも LED が3回光り（送信が捨てられない）、エアコンと画面が最後に押したモードの状態になる（暖房なら暖房の記憶の温度・風量。D-05 4.4）。各回の「最後のボタン（モード）に触れたコマ」から「3回目の LED が光り始めるコマ」までの時間を記録する。1.0 秒を超えても fail にしない（押した順の送信キューの引き換え。D-05 要件への疑問5 の人の判断の材料） |
| TC-H26 | [N-TIME] [UI] | ルーターがインターネットにつながっている | リセットして 60 秒後に画面と `curl http://IP/api/status` を見る | `clock.synced=true`、`now` がスマホの時計と ±5 秒以内で `+09:00`。画面の時刻と曜日が合い、「時刻同期済み」。警告の帯が無い |
| TC-H27 | [N-TIME] | ルーターの WAN 側を外す。エアコンを止めておき、LED をカメラで映す | リセットして画面を開く → スケジュールに 2 分後・毎日の「エアコン運転 冷房 26℃」を登録 → 予定の時刻を過ぎるまで待つ → WAN を戻し、シリアルに `[ntp] synced` が出るのを待つ | 未取得の間：上部に「時刻未取得のためスケジュールは実行されません」、`#clock` が `--:--`、予定の時刻に LED が光らず、エアコンは止まったまま。`[ntp] synced` の後 30 秒以内に帯が消える |
| TC-H28 | [N-AUTH] | 家の LAN の PC のブラウザ。エアコン運転中 | `http://IP/` を開き、風量のボタンを1つ押す | ログインやパスワードの入力なしで画面が開き、エアコンの風量が変わる |
| TC-H29 | [API] | PC の端末 | `curl 'http://IP/api/status?x=1'`、`curl -X PUT -H 'Content-Type: application/json' -d '{"schedules":[]}' http://IP/api/schedules`、`-H` を付けず `-d` だけで `curl -X POST -d '{"temp":27}' http://IP/api/ac`（curl が Content-Type を `application/x-www-form-urlencoded` にする） | 1つ目 200（クエリは無視）、2つ目 200 `{"max":10,"schedules":[]}`、3つ目 400 `{"error":"invalid json"}` |
| TC-H80 | [API] [F3] [F1] | PC の端末。LED をカメラで映す | `curl -X POST -H 'Content-Type: application/json' -d '{"button":"power"}' http://IP/api/light`、`curl -X POST -H 'Content-Type: application/json' -d '{"swingV":"auto"}' http://IP/api/ac`、`curl http://IP/api/status` | 1つ目 404 `{"error":"not found"}`、2つ目 400 `{"error":"unknown key \"swingV\""}`。LED が光らない。3つ目の `ac`・`acCapabilities` に `swingV`・`swingH` のキーが無く、`acCapabilities.fan` が `["auto","min","low","medium","high"]`、`tempModes` が `["cool","dry","heat"]` |
| TC-H30 | [N-WIFI] [N-BOOT] | 接続中。シリアルを開き、LED をカメラで映す | ルーターの電源を切り、2 分待つ | シリアルの行の時刻（`at NNNms`）で判定する：`[wifi] lost at X ms` の直後に `attempt 1/3`、以後 `attempt 2/3`・`3/3` がそれぞれ前の `attempt` から約 10000ms 後、`giving up, restart at Y ms` の Y−X がおよそ 30000ms（D-01 6a の例：1000→31000）。続けて再起動し `[boot] irhub <版> reset=3` の行が出る（3＝`ESP_RST_SW`、ソフトウェア再起動。電源投入は `reset=1`。D-06 2.1 の起動ログは数値で出す）。LED は光らない |
| TC-H31 | [N-WIFI] | 接続中 | ルーターを切り、`attempt 2/3` が出たら電源を戻す → つながったらもう一度切る | 再起動せずに `[wifi] connected ip=…`、画面が再び開ける。2回目の切断で `attempt 1/3` から数え直す |
| TC-H32 | [N-WIFI] | ルーターの電源を切っておく | ESP32 をリセットして 100 秒待つ → ルーターを戻す | 約 30 秒ごとに再起動を繰り返す（100 秒で3回前後）。ルーターを戻すと `connected` になり画面が開ける |
| TC-H33 | [N-WIFI] | TC-H30〜H32 のシリアルの記録 | 各 `[wifi] connected` について、その前にある直近の `[wifi]` の行を見る（間の `[dht20]`・`[ntp]` などの行は飛ばす） | すべての `connected` で、直前の `[wifi]` の行が `attempt n/3` である（自動再接続が働いていない。D-06 4.2） |
| TC-H34 | [N-STATE] | 画面でエアコンを暖房 22℃ にし、スケジュールを1件登録 | EN ボタンでリセットし、画面を開き直す | エアコンの表示が停止・冷房・26℃（初期値）、スケジュールが「スケジュールはありません」 |
| TC-H35 | [N-BOOT] | TC-H34 と同時に、LED をカメラで映す | リセット後 60 秒見る | LED が光らない。エアコンが動かない |
| TC-H36 | [UI] | (1) で題名を変える前に `web/index.html` のコピーを取っておく（(3) の最後にこのコピーで戻すので、題名も元に戻る） | (1) `web/index.html` の題名を一時的に1文字変えて `pio run -e esp32 -t upload` → 画面を再読み込み。(2) `include/secrets.h` を一時的に別名にして `pio run -e esp32`（終わったら戻す）。(3) `web/index.html` の末尾に 0x00 のバイトを1つ足して（`printf '\x00' >> web/index.html`）`pio run -e esp32` → `echo $?` → 取っておいたコピーで元に戻して `pio run -e esp32` | (1) 変えた題名が出る（古い画面が残らない）。(2) ビルドが `#error` の「include/secrets.h がありません」で止まる。`git status` に `include/secrets.h` が出ない。(3) `embed_html.py` の理由の1行が出て、コンパイルに進まずにビルドが失敗で止まる（`FAILED`、終了コードが 0 以外）。戻した後のビルドは成功する |
| TC-H60 | [N-TIME] [F4] | TC-H26 のとおり同期済み。エアコンを止めておく。今の時刻を T とし、T+3分・毎日の「エアコン運転 冷房 26℃」を1件登録。LED をカメラで映す | ルーターの WAN 側のケーブルを抜く（家の Wi-Fi は生かす）→ 1 分待って `curl http://IP/api/status` と画面を見る → T+4分まで待つ | WAN を抜いた後も `clock.synced=true`、`now` が進んでいる（スマホの時計と ±5 秒以内）。画面に警告の帯が出ない。T+3分に LED が1回光り、エアコンが冷房 26℃ で運転を始める（同期後に NTP が途切れてもスケジュールが動く。D-06 5.2） |
| TC-H61 | [N-TIME] [N-BOOT] [N-WIFI] | 同期済み。シリアルを開く | ルーターの WAN 側のケーブルを抜く → ルーターの電源を切り、`[wifi] giving up, restart` と `[boot]` の行が出るのを待つ → WAN を抜いたままルーターの電源を戻す → `[wifi] connected` の後に `curl http://IP/api/status` と画面を見る → WAN を戻す | 再起動後（シリアルに `[boot] irhub <版> reset=3`。3＝`ESP_RST_SW`）の `/api/status` が `clock.synced=false`、`clock.now=null`。画面に「時刻未取得のためスケジュールは実行されません」、`#clock` が `--:--`（前の起動の時刻を同期扱いにしない）。WAN を戻すと `[ntp] synced` が出て、その後 30 秒以内に帯が消える |
| TC-H65 | [N-RESP] | エアコンを運転中にしておく。スロー動画で画面のボタン・LED・シリアルモニタ（PC の画面）を同時に映す | (a) 今の時刻を T とし、T+2分・毎日の「エアコン運転 冷房 26℃」を登録。T+2分に LED が光り始めたら、すぐ（0.3 秒以内に）画面の＋を押す。これを別の分で3回。(b) ＋と−を 2 秒おきに交互に 70 秒押し続ける（その間に `[dht20]` の行が2回以上出る） | (a) 3回とも＋の送信が捨てられず（LED がもう一度光り、設定温度が1上がる）、＋に触れてから2回目の LED が光り始めるまで 1.0 秒以内（D-06 3.3 の見込み 約 0.5〜0.9 秒）。時間を記録する。(b) すべての押下で、触れてから LED が光り始めるまで 1.0 秒以内（`[dht20]` の行が出た前後の押下も含む） |

### フェーズ5（H-5）：スケジュール

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H37 | [F4] [F1] | NTP 取得済み（帯が無い）。エアコン停止。LED をカメラで映す。今の時刻を T とする | 画面で3件登録：T+2分 エアコン運転（暖房 20℃）、T+3分 エアコン停止、T+4分 エアコン運転（自動・風量 静音）。曜日は今日を含む | 一覧に3件が登録順で出る（3件目は「エアコン 運転 自動 風量静音」）。T+2分の 0〜約1秒で LED が1回光り、エアコンが暖房 20℃ で動く。T+3分でエアコンが止まる。T+4分でエアコンが自動・静音で動く。LED はそれぞれの分で1回ずつ光り、同じ分に2回光らない。画面のエアコン表示も次の読み直し（30 秒以内）で同じ状態になる |
| TC-H38 | [F4] | TC-H37 の3件。エアコンを止めておく。LED をカメラで映す。操作を始める時刻を U とし、以下の操作は1分以内に終える | 1件目（暖房）を「編集」で U+2分・冷房 26℃ に変える。3件目（自動）を「削除」。2件目（停止）を「編集」で U+4分 に変えてから、有効の切替で無効にする。U+5分まで LED とエアコンを見る | 各操作の後に一覧が描き直る。削除した件は一覧から消え、2件目は無効の表示で U+4分 になっている。U+2分に LED が1回光り、エアコンが冷房 26℃ で運転を始める。U+4分〜U+5分に LED が光らず、エアコンは運転したまま（無効にした停止の件が送られない） |
| TC-H81 | [F4] [F1] | エアコン停止。LED をカメラで映す。今の時刻を T とする | T+2分・今日の曜日で2件を登録：1件目 エアコン運転（冷房 26℃）、2件目 エアコン運転（冷房・風量 強）。T+3分まで見る | T+2分に LED が続けて2回光る。エアコンが冷房 26℃・風量 強で運転している（同じ分の2件を間隔なしで送っても両方受け付ける）。2件目が効かなかった（風量が強にならない）場合は fail とし、記録する（送信の間隔が要る。D-03 要件への疑問7） |
| TC-H39 | [F4-LIMIT] | 一覧を10件にする | 画面の追加ボタンを見る。続けて11件の本文で `curl -X PUT -H 'Content-Type: application/json' -d '<11件>' http://IP/api/schedules` | 見出しが `10/10件`、追加ボタンが押せず「上限 10 件」。curl は 400 `{"error":"too many schedules (max 10)"}`、一覧は10件のまま |
| TC-H40 | [F4-IO] | 3件以上登録済み、NTP 取得済み | iOS Safari と Android Chrome のそれぞれで画面を開き「エクスポート」を押す（押した後はほかの操作をしない）→ 端末のファイル（iOS は「ファイル」アプリのダウンロード、Android は「ダウンロード」）を開く | 両方の端末で：「エクスポートしました：irhub-schedules-YYYYMMDD-HHMM.json」が約 3 秒出る。ページが移らず、画面に JSON の文字が表示されるだけにならない。ダウンロードに `irhub-schedules-YYYYMMDD-HHMM.json`（押した時刻の JST。画面の知らせと同じ名前）が1つ増える（保存されなければ fail とし、止められたか・別名になったかを記録：設計の不足6）。中身に `"version": 1`、`exportedAt`（`+09:00`）、登録した件が id 付きであり、すべて `"target": "ac"` |
| TC-H41 | [F4-IO] [N-STATE] | TC-H40 のファイル | EN ボタンでリセット → 画面を開く → 「インポート」で TC-H40 のファイルを選ぶ | リセット後の一覧は空。インポート後「<n>件を読み込みました」と出て、一覧が id を含めエクスポート前と同じになる |
| TC-H42 | [F4-IO] | TC-H40 のファイルの `"version": 1` を `2` に書き換えたもの | インポートする | 「読み込めませんでした：unsupported version」。一覧は変わらない。同じファイルを続けてもう一度選べる |
| TC-H82 | [F4-IO] [F3] | TC-H40 のファイルの1件目の `"target": "ac"` を `"target": "light"` に書き換えたもの | インポートする | 「読み込めませんでした：schedules[0].target: unknown target」。一覧は変わらない |
| TC-H43 | [N-BOOT] [F4] | エアコン停止。今の時刻を T とし、T+2分・毎日の「エアコン運転 冷房 26℃」の1件だけを登録してエクスポートしておく。LED をカメラで映す | T+2分の 20 秒前に EN ボタンでリセット → T+2分になってから 10 秒以上たった後（同じ分の中）に、エクスポートしたファイルをインポート → T+3分まで待つ | リセットから T+3分まで LED が一度も光らず、エアコンは止まったまま（リセット直後は一覧が空。判定済みの分にインポートした予定はその分には送られない） |
| TC-H76 | [F4-IO] [API] | NTP 取得済み。スケジュールを1件以上登録。PC の端末 | `curl -D - http://IP/api/schedules/export` を実行し、そのときの時計（JST）を控える | 応答 200。応答ヘッダに `Content-Disposition` があり、その `filename` が `irhub-schedules-YYYYMMDD-HHMM.json`（実行した時刻の JST）。本文の `exportedAt` が同じ日付・時分の `+09:00` の文字列 |

### フェーズ6（H-6）：外出先から（`docs/ops/remote-access.md` の手順0〜7を済ませてから）

期待結果を満たさない行があったら、`docs/ops/remote-access.md` の「うまくいかないとき」の表で当てはまる症状を探し、「見るところ」で見えたことと行った「対応」を、その行の ID と一緒にメモに残す。対応の後にその行をやり直して満たせば ✓ にしてよい（満たさなければ fail のまま）。

| ID | 要件 | 前提 | 操作 | 期待結果（完成判定） |
|---|---|---|---|---|
| TC-H44 | [F6] | 手順0〜5 済み | Tailscale の管理画面で PC の Subnets を見る | 承認済みのルートが `<IP>/32` の1つだけ。`/24` などが無い |
| TC-H75 | [F6] | 手順1a（Disable key expiry）済み | 管理画面の Machines を開き、PC の行を見る。続けて PC の行のメニュー（…）を開いて項目を見て、何も選ばずに閉じる | PC の行に Expiry disabled と出る（表示の文言・位置は D-07 の要確認8。行に無ければ PC の詳細画面で鍵の期限の欄が無効になっていればよい）。メニューの項目が Enable key expiry（期限を戻す側）になっている。スマホの行は変えていない |
| TC-H45 | [F6] [N-IP] | ESP32 の IP 固定（D-06 6節の方式A または B） | 方式A：ルーターの DHCP 予約一覧を見る。方式B：シリアルの `[wifi] static ip set` と `connected ip=` を見る | 方式A：`[wifi] mac` の MAC に IP が予約され、`connected ip=` と一致。方式B：`connected ip=` が `kStaticIp` と一致し、ルーターの自動割り当て範囲の外 |
| TC-H64 | [F6] [N-IP] [N-WIFI] [N-TIME] | 方式B（`kUseStaticIp=true`）の場合だけ行う（方式A なら「対象外」と書いて ✓）。シリアルを開く | 起動後の `[wifi] connected ip=` を控える → ルーターの電源を切り、`attempt 2/3` が出たら戻す → 再起動せずに `[wifi] connected` が出たら `curl http://IP/api/status` | 再試行後の `connected ip=` が起動時と同じ `kStaticIp` の値。`clock.synced=true`（DNS が効いている）。`http://IP/` が開ける（D-06 4.3 の要確認） |
| TC-H66 | [F6] | 手順3（スリープしない設定）済み。変える前のスリープ時間を控えておく（例 30 分） | PC に触らずに、控えた時間より長く（例 1 時間）置く → スマホ（モバイル回線）で Tailscale の管理画面の Machines を開く → 続けて TC-H46 を行う | 管理画面の PC が Connected のまま。TC-H46 の期待結果を満たす |
| TC-H46 | [F6] | エアコンを運転中にしておく（停止中は温度だけ変えても赤外線を送らない）。スマホの Wi-Fi を切り（モバイル回線）、Tailscale アプリをオン | `http://IP/` を開き、エアコンの温度を 1℃ 上げる | 家の中と同じ画面が出て、室温・湿度・時刻が表示される。エアコンの設定温度が実際に 1℃ 上がる（家にいる人、または家の中でモバイル回線で確かめる） |
| TC-H47 | [F6] | TC-H46 と同じ状態 | ルーターの管理画面（例 `http://192.168.1.1/`）を開く。次に Tailscale アプリをオフにして `http://IP/` を開く | どちらも開けない（タイムアウト） |
| TC-H48 | [F6] | PC を Windows からサインアウト（シャットダウンしない） | TC-H46 をもう一度行う（温度は 1℃ 下げる） | 画面が開き、エアコンの設定温度が 1℃ 下がる |
| TC-H49 | [F6] [N-RESP] | TC-H46 の状態 | エアコンの＋を押し、押してから画面の温度表示が応答で確定する（応答が返る）までの時間を計る | 操作が効く（外出先の遅れは N-RESP の1秒の対象外。D-06 疑問4。かかった時間を記録） |

### PC とモックで人が確かめること（I-04 の後、H-4 の前）

前提：`python tools/mock_server.py`（オプションは各行）を起動し、PC のブラウザをスマホ幅（幅 390px 程度）にして `http://127.0.0.1:8000/` を開く。送った本文は開発ツールの Network で見る。

| ID | 要件 | 前提 | 操作 | 期待結果 |
|---|---|---|---|---|
| TC-H50 | [UI] [F1] | オプションなし | 開く。モックの風量の選択肢を1つ減らして再起動し、開き直す | 上部バー・エアコン・スケジュールの3つが描かれ、照明のカードと風向の選択が無い。モード・風量のボタンが `acCapabilities` の順に並び、風量に「静音」（`min`）が出る。選択肢を減らすとボタンも減る |
| TC-H51 | [UI] [F1] | オプションなし（運転中・冷房 26℃） | ＋を1回押す。上限まで＋、下限まで− | 送った本文が `{"temp":27}` だけ。上限（`tempMax`）で＋、下限（`tempMin`）で−が押せない |
| TC-H52 | [UI] [N-RESP] | `--delay-ms 800` | ＋を素早く3回、1回目から 0.5 秒以内に3回押し終える（800ms の遅延より十分短く） | `POST /api/ac` が重ならず1本ずつ出る（2本）。2本目の本文が `{"temp":29}`。最後の表示が 29℃（26＋3） |
| TC-H53 | [UI] | `--delay-ms 6000` | ＋を2回押す。モックを `--delay-ms 0` で起動し直して帯を押す | 約 5 秒で「ESP32 に接続できません」の帯。2回目の＋は捨てられ、読み直した温度が描かれる。次の成功で帯が消える |
| TC-H54 | [UI] [N-TIME] | `--unsynced` | 開く。スケジュールの追加・エクスポート・インポートを押す | 上部の帯「時刻未取得のためスケジュールは実行されません」、カードの注記「時刻未取得の間は実行されません」、`#clock` が `--:--`。スケジュールの操作は押せる |
| TC-H55 | [UI] [F5] | `--no-sensor`、次にモックの室温を整数 24 に | 開く | `室温 --.-℃  湿度 --.-%`。整数のときは `室温 24.0℃` |
| TC-H56 | [UI] [F5] | オプションなし | 2 分開いたままにする → タブを隠して 1 分 → 戻す | `GET /api/status` が 30 秒ごとに1本ずつ（2 分で4本）。隠している間は出ない。戻したらすぐ1本と `GET /api/schedules` |
| TC-H57 | [UI] [F4-IO] | エクスポートしたファイル | インポート。`version` を 2 にしたファイルでもう一度 | 送った本文がファイルの中身そのもの（文字列化の二重エスケープが無い）。2回目は「読み込めませんでした：unsupported version」で一覧が変わらない |
| TC-H58 | [UI] [F4] [F4-LIMIT] | オプションなし | 追加（エアコン運転＋暖房 20℃、エアコン停止、エアコン運転＋自動＋風量静音）、編集、削除、有効切替。フォームでモードを「指定しない」にする。件数を max まで増やす | 各操作で `PUT /api/schedules` が1回、既存の件に id が付いていて、すべて `"target":"ac"`。対象のラジオが「エアコン運転」「エアコン停止」の2つだけ。モード未指定のとき、およびモードが自動のとき温度が選べず「指定しない」に戻る。一覧に「エアコン 運転 自動 風量静音」と出る。max 件で追加ボタンが押せない |
| TC-H59 | [UI] | — | `python tools/embed_html.py` を2回。`web/index.html` を消す／0x00 を入れる／80,000 バイト超にして実行（毎回元に戻す） | 生成された配列（末尾の 0 を除く）が `web/index.html` のバイト列と一致し、`kIndexHtmlLen` がそのバイト数。2回目はファイルの更新時刻が変わらない。3つの異常はどれも終了コードが 0 以外 |
| TC-H62 | [UI] [F1] [F2] | `tools/mock_server.py` の `acCapabilities.tempModes` から `dry` を外して起動 | エアコンのモードで「除湿」を押す → 「冷房」に戻す。次にスケジュールの「追加」でエアコン運転を選び、モードを除湿にする | 除湿のとき：`POST /api/ac` の応答の `ac.temp` が null、温度の欄（＋・−と数値）が隠れ、「除湿では温度を指定できません」が出る。冷房に戻すと温度の欄が戻り、その文言が消える。スケジュールのフォームで除湿を選ぶと温度の選択が押せない（`disabled`、値は「指定しない」）（`tempModes` で判断し「自動」を直書きしていないことの確認） |
| TC-H63 | [UI] [N-TIME] [F6] | オプションなし。モックの `/api/status` の `clock.now` を Network で見て控える | PC（OS）のタイムゾーンを UTC に変えてからブラウザを開き直して画面を開く。終わったら元に戻す | 上部バーの時刻が `clock.now` の JST の時刻と日付・曜日（例 `9/25(金) 20:00`。1 分以内の進みは可）で、UTC の `11:00` にならない |
| TC-H67 | [UI] [F5] | `--delay-ms 3000`。開発ツールの Network を開き、Waterfall を出しておく | 画面を開いて最初の読み込みが終わるのを待つ → 次の `GET /api/status` が出て応答待ちの間、出てから 1 秒以内にタブを隠し、すぐ戻す。これを3回くり返す | 3回とも、`/api/status` の保留中の要求は同時に多くて1本（Waterfall で帯が重ならない）。タブを戻した時点で前の要求が応答待ちなら、2本目の `/api/status` は出ない（D-05 7節 `statusInFlight`） |
| TC-H71 | [UI] [F1] | オプションなし。画面の運転ボタンが「停止中」 | (1) ＋を1回、5 秒後にモードのボタン（今と違うもの。自動以外）、5 秒後に風量のボタン（今と違うもの）。(2) 5 秒後に運転ボタン、続けて 5 秒後に＋。(3) モックを `--delay-ms 4000` で起動し直し、画面を開き直して運転ボタンが「停止中」であることを確かめる。＋を押し、その応答を待たずに（1 秒以内に）運転ボタンと＋を続けて押す | (1) 3回とも `POST /api/ac` の本文に `power` が無く（`{"temp":27}` など1項目）、200 の後に `#ac-msg` に「停止中のため設定だけ変えました（運転を押すとまとめて送ります）」が出て約 3 秒で消える。(2) 運転ボタン（本文 `{"power":true}`）と運転中の＋では出ない。(3) `POST /api/ac` が3本、押した順（`{"temp":…}`、`{"power":true}`、`{"temp":…}`）に1本ずつ出る。1本目の応答で知らせが出て、2本目・3本目の応答の後には出ない。最後の表示が「運転中」 |
| TC-H72 | [UI] [F4-IO] | オプションなし。スケジュールを2件登録。開発ツールの Network を開く。ブラウザのダウンロード先を控える | (1)「エクスポート」を押す。(2) モックを `--delay-ms 3000` で起動し直し、押してから応答までの間に「エクスポート」をもう一度押そうとする。(3) 一覧を0件にして押す。(4) モックを `--unsynced` で起動し直して押す | (1) `GET /api/schedules/export` が `fetch` で1本出る。ページが移らず、ダウンロードに1つファイルが増え、その名前がモックの応答の `Content-Disposition` の `filename` と同じ。中身が応答の本文と同じ。`#sched-msg` に「エクスポートしました：<その名前>」が出て約 3 秒で消える。(2) 応答までの間「エクスポート」が押せず（`disabled`）、要求は1本だけ。(3) 0件でも保存され、中身の `schedules` が `[]`。(4) 保存名が `Content-Disposition` のとおり |
| TC-H73 | [UI] [F4-IO] | スケジュールを2件登録した画面 | (1) モックを止めて（Ctrl+C）「エクスポート」を押す。(2) モックを `--delay-ms 0` で起動し直して帯を押し、帯が消えたら（画面は開き直さない）モックを `--delay-ms 6000` で起動し直し、すぐに「エクスポート」を押す | (1) すぐに上部に「ESP32 に接続できません」の帯、`#sched-msg` に「送れませんでした（通信エラー）」。ファイルは保存されない。(2) 押してから約 5 秒で同じ帯と文言が出て、ファイルは保存されない。どちらも一覧の表示は2件のまま |
| TC-H74 | [UI] [F4-IO] | オプションなしで画面を開き、開発ツールのコンソールを開く | `filenameFromDisposition('attachment; filename="irhub-schedules-20260925-2000.json"')`、`filenameFromDisposition(null)`、`filenameFromDisposition('attachment')` を入力する | 順に `"irhub-schedules-20260925-2000.json"`、`"irhub-schedules.json"`、`"irhub-schedules.json"` |
| TC-H83 | [UI] [F1] | オプションなし（モックの `tempModes` に `auto` が無い。運転中・冷房） | モードの「自動」を押す → 5 秒後に「冷房」を押す | 自動：`POST /api/ac` の本文が `{"mode":"auto"}` だけ、応答の `ac.temp` が null、温度の欄が隠れ「自動では温度を指定できません」。冷房：押した直後から応答までは `--℃` で −／＋ が `disabled`、応答で温度が出て押せる |
| TC-H84 | [UI] [F1] [N-RESP] | `--delay-ms 800`、運転中・冷房 | (1) ＋を押した直後（応答前）に「自動」を押す。(2) 5 秒後、風量「強」を押した直後に「暖房」を押す | (1) `{"temp":…}` と `{"mode":"auto"}` が別々の要求で押した順に出て、どちらも 200（1本にまとめて 400 にならない）。(2) `{"fan":"high"}` → `{"mode":"heat"}` の順に2本出る |
| TC-H85 | [UI] [F3] [F1] | — | PC の端末で `grep -n -i -E '/api/light|swingv|swingh|light-card|light-buttons|light-msg|sf-target-light|sf-button|LIGHT_BUTTONS|ac-timer|sf-timer|タイマー|switchbot|quiet' web/index.html` | 何も出ない（照明・風向・本体タイマーの id と文字列は D-05 テスト観点の文字列検査の一覧、`switchbot` は D-05 3節「`SwitchBot` の文字列を一切書かない」（verify の ui 検査と同じ内容を手でも見る）、`quiet` は D-05 4.1「画面に `quiet` の文字列は書かない」が根拠） |

---

## 要件カバレッジ

| 要件 | native（TC-N） | 実機・PC（TC-H） |
|---|---|---|
| [F1] | N03〜N34、N91〜N93、N135〜N140、N146、N150、N152〜N161、N164、N166〜N168、N185〜N196、N202〜N204、N208〜N210、N218、N219 | H03〜H06、H08、H09、H11〜H15、H17、H22、H23、H37、H50、H51、H62、H68〜H71、H77〜H81、H83〜H85 |
| [F2] | N01、N02、N08〜N11、N23、N24、N131、N145、N154、N187、N191、N194〜N196、N202、N210 | H16、H23、H62、H77 |
| [F3] | 対象外（要件 v0.4 で照明は作らない。code=false）。作っていないことの確認として N117、N120、N165、N166、N220 | 対象外（同上）。作っていないことの確認として H21、H80、H82、H85 |
| [F4] | N35〜N98、N102〜N104、N123、N127、N169〜N173、N179、N180、N184、N205〜N210、N212、N213、N220 | H37、H38、H43、H58、H60、H81 |
| [F4-LIMIT] | N69、N70、N105、N114、N169、N171 | H39、H58 |
| [F4-IO] | N76、N77、N100、N101、N106〜N129、N174〜N178、N182〜N184、N197〜N199、N201、N211〜N216 | H40〜H42、H57、H72〜H74、H76、H82 |
| [F5] | N132〜N134、N145、N148、N149 | H18〜H20、H24、H55、H56、H67 |
| [F6] | 対象外（code=false。ESP32 側の対応なし） | H44〜H49、H63、H64、H66、H75 |
| [N-AUTH] | N181 | H28 |
| [N-WIFI] | 対象外（判断が `src/wifi_manager` にあり native テストを作らない。D-01 1節の人の判断） | H30〜H33、H61、H64 |
| [N-STATE] | N01、N82、N131 | H34、H41、H69 |
| [N-BOOT] | N43、N82、N84、N130、N145、N180 | H10、H30、H35、H43、H61、H69 |
| [N-TIME] | N35〜N37、N43、N83、N84、N86、N87、N107、N145、N147、N175、N179、N197、N199 | H26、H27、H54、H60、H61、H63、H64 |
| [N-RESP] | 対象外（時間の計測は実機だけ。D-05 9節・D-06 3節） | H25、H49、H52、H65、H79、H84 |
| [UI] | 対象外（画面は C++ ではない。画面が頼る API の形は N145〜N181 で確かめる） | H21〜H24、H36、H50〜H59、H62、H63、H67、H70〜H74、H79、H83〜H85 |
| [API] | N141〜N163、N165〜N168、N181、N184、N192、N199〜N201、N218〜N220 | H17、H29、H76、H80 |
| [HW-PINS] | 対象外（`src/pins.h` は native でビルドしない） | H01、H02、H10、H11、H18 |
| （参考）[N-IP] | 対象外（この作業項目の reqs の外） | H45、H64（F6 の前提として付けた。カバレッジの判定には数えない） |

要件8章の完成判定との対応：フェーズ0＝TC-H01（実施済み）、フェーズ1＝TC-H02〜H06・H08（実施済み）、フェーズ2＝TC-H11〜H15・H77、フェーズ3＝TC-H18・H19、フェーズ4＝TC-H21〜H24、フェーズ5＝TC-H37・H40・H41、フェーズ6＝TC-H46。

### 設計の不足（テストを書くうえで設計書に無かった・食い違っていた点）

1. **`state[11]` に実際に入る値が設計書に無い。** D-01 7節・D-02 7節・D-06 2.4 は「IRac は `state[11]` を埋めない」とだけ書く。D-06 の審査（`loop/reviews/D-06-a1.md`）では `IRHitachiAc296::stateReset` が固定の 0x43 を入れると指摘されている。この計画の合否（TC-H11〜H15・H77）は「押したボタンに合わせて埋めない信号をエアコンが受け付けるか」で決め、0x43 は TC-H78 の記録の見込みにだけ使う。設計書の文言の直しは設計の担当に任せる。
2. **フェーズ3「室温計と大きくずれない」の許容差がどの設計書にも無い。** TC-H19 は差を記録するだけにした（人が閾値を決める必要がある）。
3. **H-2 で「エアコンが受け付けた」を何で判定するかが設計に無い**（D-02「実機でしか確かめられないこと」は「効く」とだけ書く）。特に風量5段階（TC-H14）と自動モード（TC-H77）は本体の表示で見分けられるか分からない。この計画は受信音と吹き出しの風の強さ・冷温、本体の表示で判定するとした。見分けられない場合の判定方法は人が決める。
4. **送信信号の記録（`state[11]`・自動の温度欄）の手段が設計に無い。** D-02・D-06 は「受信機で送信信号をダンプし記録する」とするが、部品表の ESP32 は1台で、本体ファームは IO14 を初期化しない（D-01 7節）。TC-H78 は受信側を別に用意できる場合だけ行い、できなければ対象外として、受け付けるかどうか（TC-H11〜H15・H77）だけで判定する。
5. **F5 の画面への反映の時間**：D-05 のテスト観点は「30 秒以内に変化が反映される」だが、センサーの周期（30 秒、D-06）と画面の読み直し（30 秒、D-05）が重なると最悪 60 秒になる。TC-H24 は 60 秒で判定した。どちらを正とするかは人が決める。
6. **エクスポートで、応答を待った後の `a.click()` を iOS Safari・Android Chrome が止めた場合の代わりの手段が決まっていない**（D-05 6.5 の要確認・疑問7）。TC-H40 は保存されなければ fail とし、どうなったかを記録するだけにした。
7. **`Hub::clockNow()` が `IClock::now()` を何回呼ぶかは明記されていない。** TC-N174 はそれに頼らず、呼ぶたびに進む FakeClock（`seq`）で「ファイル名と `exportedAt` が同じ時刻」だけを確かめる（D-04 8節）。
8. **D-05 8.2 の「200 の後の本文の読み取りが中止・失敗したら帯を出し直し status 0 として扱う」を PC で起こす手段が無い**（D-05 12節のモックのオプションは応答全体を遅らせる `--delay-ms` だけ）。このケースは作らず、応答そのものの失敗・タイムアウト（TC-H53、TC-H73）だけを確かめる。
