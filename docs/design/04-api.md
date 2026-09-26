# 詳細設計：HTTP API

作業項目：D-04／要件の原本：`docs/requirements.md` v0.3／要件ID：`docs/req-index.json`／前提：`docs/design/01-architecture.md`（D-01）、`docs/design/02-ac-state.md`（D-02）、`docs/design/03-schedule.md`（D-03）

この文書では、`lib/core/src/api.h/.cpp`（`ApiRouter`）の中身と、`src/web_bridge.*` が WebServer との間でどう橋渡しするかを決める。全エンドポイントのリクエスト／レスポンスの JSON、各フィールドの型と検証、エラーの一覧をここで確定する。

D-01 の名前（`HttpMethod`・`ApiRequest`・`ApiResponse`・`ApiRouter::handle`）とルーティング表、D-02 の `AcPatch`・`AcError`・`errorMessage`・文字列表、D-03 の `schedulesToJson`・`schedulesFromJson`・`exportFilename`・`Hub::replaceSchedules`・エラー文言に従う。それらは変えない。

---

## 対象要件

- [API] HTTP API（仮）：「2. エンドポイント一覧」「3〜9. 各エンドポイント」「10. エラーの一覧」「11. ApiRouter の実装」「12. web_bridge」
- [F1] エアコン操作：「4. POST /api/ac」（送った項目だけ変えて状態一式を送る）、「3. GET /api/status」の `ac` と `acCapabilities`（選択肢を画面に渡す）
- [F3] 照明操作：「5. POST /api/light」（6ボタンのうち1つを送る。状態を返さない）
- [F4] スケジュール：「6. GET /api/schedules」「7. PUT /api/schedules」（登録・編集・削除はすべて一覧の置き換え）
- [F4-IO] エクスポート／インポート：「8. GET /api/schedules/export」（ダウンロード用ヘッダ）、「9. POST /api/schedules/import」（ファイルの中身を JSON 本文で送る）
- [F5] 室温・湿度の表示：「3. GET /api/status」の `climate`（最後に読んだ値、読めていなければ null）
- [N-AUTH] LAN 内は認証なし：「1. 共通の決まり」の認証なし（トークン・ヘッダの確認をしない）、「13. 作らないもの」
- [N-TIME] NTP・JST、取得前は警告：「3. GET /api/status」の `clock.synced` と `clock.now`（画面が警告を出す材料）。取得前もスケジュール API は受け付ける（6〜9）

---

## 設計

### 1. 共通の決まり

| 項目 | 決まり |
|---|---|
| 認証 | なし（N-AUTH）。`Authorization` などのヘッダを見ない。どの端末からでも同じに動く |
| 文字コード | UTF-8。応答の文言（`error`）は英語の ASCII（D-02・D-03 の流儀） |
| 応答の Content-Type | すべて `application/json`（エクスポートも。`downloadFilename` でダウンロードにする） |
| リクエストの Content-Type | 画面は本文のあるリクエストに `Content-Type: application/json` を付ける（12節の理由）。`ApiRouter` は Content-Type を見ない |
| パスの一致 | 完全一致。クエリ文字列は `web_bridge` が落としてから渡す（`/api/status?x=1` は `/api/status`）。末尾スラッシュ付き（`/api/status/`）は別のパス＝404 |
| 本文の大きさ | `/api/ac`・`/api/light` は `kApiSmallBodyMaxBytes = 512` バイトまで。スケジュール系は D-03 の `kScheduleJsonMaxBytes = 8192` バイトまで（`schedulesFromJson` が確かめる） |
| JSON の形 | 本文は一番外側がオブジェクトであること。知らないキーの扱いはエンドポイントごと（各節） |
| 成功 | 200 と本文（各節） |
| エラー | 検証エラーは **400 と `{"error":"理由"}`**（要件5章）。ルーティングのエラーだけ 404／405（10節） |
| CORS | 付けない。画面は同じ ESP32 から配られる（同一オリジン）。F6 も同じ URL を開くだけ |

### 2. エンドポイント一覧

| メソッド | パス | 本文 | 成功 | 主なエラー | 節 |
|---|---|---|---|---|---|
| GET | `/` | — | 200 HTML | — | 12（web_bridge が返す。ApiRouter は扱わない） |
| GET | `/api/status` | — | 200 状態 | — | 3 |
| POST | `/api/ac` | AcPatch の JSON | 200 `{"ac":{...}}` | 400 | 4 |
| POST | `/api/light` | `{"button":...}` | 200 `{"ok":true}` | 400 | 5 |
| GET | `/api/schedules` | — | 200 一覧 | — | 6 |
| PUT | `/api/schedules` | 一覧 | 200 一覧（採番済み） | 400 | 7 |
| GET | `/api/schedules/export` | — | 200 エクスポートファイル＋ダウンロード | — | 8 |
| POST | `/api/schedules/import` | エクスポートファイルの中身 | 200 一覧（採番済み） | 400 | 9 |

要件5章の表にあるものだけ。これ以外のエンドポイントは作らない（13節）。

### 3. GET /api/status

**選択肢（capabilities）を含める**。決めた理由：
- 画面は起動時と定期的にこれ1本を読めば描ける（要件5章の API 表にない選択肢専用のエンドポイントを足さずに済む）。
- F1-VALUES の rule「画面は /api/status などから選択肢を受け取って描き、値を直書きしない」に合う。
- 大きさは 200 バイト程度で、定期的に読んでも負担にならない。

応答（200、値は今の仮値。NTP 取得済み・センサー読み取り済み）：

```json
{
  "ac": { "power": false, "mode": "cool", "temp": 26, "fan": "auto", "swingV": "off", "swingH": "off" },
  "acCapabilities": {
    "modes": ["auto", "cool", "dry", "heat"],
    "tempMin": 16, "tempMax": 30, "tempStep": 1,
    "tempModes": ["auto", "cool", "dry", "heat"],
    "fan": ["auto", "low", "medium", "high"],
    "swingV": ["off", "auto"],
    "swingH": ["off"]
  },
  "climate": { "valid": true, "temperature": 24.5, "humidity": 55.2 },
  "clock": { "synced": true, "now": "2026-09-25T20:00:00+09:00" }
}
```

NTP 未取得・センサー未読み取り（起動直後）：

```json
{
  "ac": { "power": false, "mode": "cool", "temp": 26, "fan": "auto", "swingV": "off", "swingH": "off" },
  "acCapabilities": { "...": "上と同じ" },
  "climate": { "valid": false, "temperature": null, "humidity": null },
  "clock": { "synced": false, "now": null }
}
```

フィールド：

| キー | 型 | 出どころ | 規則 |
|---|---|---|---|
| `ac.power` | bool | `hub.acState().power` | |
| `ac.mode` | 文字列 | `toString(state.mode)` | D-02 2節の表 |
| `ac.temp` | 整数 または null | `state.tempC` | `state.hasTemp == false` なら `null`（D-02 5節） |
| `ac.fan` / `ac.swingV` / `ac.swingH` | 文字列 | `toString(...)` | D-02 2節の表 |
| `acCapabilities.modes` | 文字列の配列 | `AcMode` の値の順（auto, cool, dry, heat） | 確定の4つ |
| `acCapabilities.tempMin` / `tempMax` / `tempStep` | 整数 | `cap::kTempMinC` / `kTempMaxC` / `kTempStepC` | 数値を api.cpp に直書きしない |
| `acCapabilities.tempModes` | 文字列の配列 | `cap::tempSupported(m)` が true のモード | `AcMode` の順 |
| `acCapabilities.fan` / `swingV` / `swingH` | 文字列の配列 | `cap::kFanChoices` / `kSwingVChoices` / `kSwingHChoices` の順 | 画面はこの順に並べる |
| `climate.valid` | bool | `hub.lastClimate().valid` | 一度も読めていない、または最後の読み取りが失敗なら false |
| `climate.temperature` | 数値 または null | `temperatureC` | 小数1桁に丸める（`std::round(x * 10.0) / 10.0`）。`valid == false` なら null |
| `climate.humidity` | 数値 または null | `humidityPct` | 同上 |
| `clock.synced` | bool | `hub.clockNow().synced` | false の間、画面は「時刻未取得のためスケジュールは実行されません」を出す（N-TIME、文言は D-05） |
| `clock.now` | 文字列 または null | `ClockReading::local` | `"YYYY-MM-DDTHH:MM:SS+09:00"`（ゼロ埋め、D-03 6.2 の `exportedAt` と同じ書式。schedule_json の `formatJstIso` で作る、11節）。`synced == false` なら null |

- 照明のボタンの一覧は載せない。6つは要件 F3 で決定しており（受信結果で変わらない）、画面の日本語の表示名と一緒に画面側に持つ（D-05）。API の文字列は5節の6つ。
- スケジュールの上限 `max` は `/api/status` ではなく `GET /api/schedules` に載せる（D-03 6.1 のとおり）。
- 本体タイマーのキーは載せない（F1-TIMER、13節）。

### 4. POST /api/ac

要件5章「エアコンの設定を変更して送信。送った項目だけ変更し、状態一式を送る」。

リクエスト（どれも任意。1つ以上必要）：

```json
{ "temp": 27 }
{ "power": true, "mode": "heat" }
{ "power": true, "mode": "cool", "temp": 26, "fan": "auto", "swingV": "auto", "swingH": "off" }
```

| キー | JSON の型 | `AcPatch` のフィールド | 形の検査（api.cpp） | 値の検査（D-02 `AcModel::validate`） |
|---|---|---|---|---|
| `power` | bool | `power` | `is<bool>()` でなければ `power: must be boolean` | — |
| `mode` | 文字列 | `mode` | 文字列でなければ `mode: must be string`、`parseAcMode` が nullopt なら `mode: unknown mode` | — |
| `temp` | 整数 | `tempC`（int） | `is<int>()` でなければ `temp: must be integer`（`26.5`・`"26"`・`null`・`true` も） | 範囲 `temp out of range`、そのモードで不可 `temp not supported in this mode` |
| `fan` | 文字列 | `fan` | 文字列でなければ `fan: must be string`、不明は `fan: unknown fan` | `fan not supported` |
| `swingV` | 文字列 | `swingV` | 同様に `swingV: must be string` / `swingV: unknown swingV` | `swingV not supported` |
| `swingH` | 文字列 | `swingH` | 同様に `swingH: must be string` / `swingH: unknown swingH` | `swingH not supported` |

- 上の6つ以外のキーがあれば 400 `unknown key "<キー>"`（打ち間違い・本体タイマーなど作っていない項目を黙って捨てないため）。
- 値に `null` を置くのは不可（型の違いとして上の `must be ...`）。`/api/status` の `ac` をそのまま送り返すと、除湿で温度指定できない機種では `"temp": null` で 400 になる。画面は変えた項目だけを送る（D-05 への申し送り）。
- `{}` は形の検査を通り、`AcModel::validate` の `EmptyPatch` で 400 `no ac fields`。

検査の順（最初に当たったもので 400。どこで止まっても状態は変わらず、送信もしない）：

1. `body.size() > kApiSmallBodyMaxBytes` → `body too large`
2. JSON として読めない、または一番外側がオブジェクトでない → `invalid json`
3. 知らないキー（本文の中の出てきた順で最初のもの） → `unknown key "<キー>"`
4. キーごとの形の検査。見る順は `power, mode, temp, fan, swingV, swingH`（本文の中の順ではない。結果を決まった形にするため）
5. `hub.applyAc(patch, &err)` が false → `err`（D-02 2節の `errorMessage`）

成功（200）：更新後の状態を3節の `ac` と同じ形で返す。

```json
{ "ac": { "power": true, "mode": "cool", "temp": 27, "fan": "auto", "swingV": "off", "swingH": "off" } }
```

- `Hub::applyAc` が true なら、`sendAc` の戻り値にかかわらず 200（D-01 5節：送信の失敗は状態を戻さず、応答にも出さない）。
- 同じ値だけのパッチも 200（D-02 4節）。送るかどうかは D-02 6節の規則どおり（運転中なら状態一式を送る。停止中に power を含まないパッチは状態を更新するだけで送らない）。API はこの規則を知らず、`hub.applyAc` の戻り値だけを見る。

### 5. POST /api/light

リクエスト：

```json
{ "button": "night" }
```

| キー | 型 | 必須 | 規則 |
|---|---|---|---|
| `button` | 文字列 | 必須 | `power` `night` `brighter` `dimmer` `full` `half` のどれか（`parseLightButton`、大文字小文字を区別） |

検査の順：
1. `body too large`（512 バイト超）
2. `invalid json`
3. `button` 以外のキー → `unknown key "<キー>"`
4. `button` が無い → `button: missing`
5. 文字列でない → `button: must be string`
6. `parseLightButton` が nullopt → `button: unknown button`

成功（200）：`hub.pressLight(button)` を1回呼び、`{"ok":true}` を返す。

- 照明は状態を持たない（F3）ので、状態は返さない。
- `pressLight` の戻り値（`sendLight` の戻り値）は応答に出さない。エアコンと同じく、赤外線が届いたかはもともと分からないため（要件への疑問 2）。

`ApiRequest` / `ApiResponse` の実例：

```json
{ "method": "POST", "path": "/api/light", "body": "{\"button\":\"night\"}" }
{ "status": 200, "contentType": "application/json", "body": "{\"ok\":true}", "downloadFilename": "" }

{ "method": "POST", "path": "/api/light", "body": "{\"button\":\"on\"}" }
{ "status": 400, "contentType": "application/json", "body": "{\"error\":\"button: unknown button\"}", "downloadFilename": "" }
```

### 6. GET /api/schedules

応答（200）：`schedulesToJson(hub.schedules(), ScheduleJsonKind::List, hub.clockNow())` の結果をそのまま本文にする。形は D-03 6.1（ここで確定する）。

```json
{ "max": 10,
  "schedules": [
    { "id": 1, "enabled": true, "time": "07:00", "days": ["mon","tue","wed","thu","fri"],
      "target": "ac", "action": { "power": true, "mode": "heat", "temp": 20 } },
    { "id": 3, "enabled": false, "time": "06:45", "days": ["sun","sat"],
      "target": "light", "action": { "button": "full" } }
  ] }
```

- `max` は `kScheduleMax`（仮(F4-LIMIT)）。画面は上限を直書きせず、これを使う。
- NTP 未取得でも 200（N-TIME：実行しないだけで、一覧の閲覧・編集は受け付ける）。

### 7. PUT /api/schedules

本文（D-03 6.1。`id` の無い件は新規で採番される。`max` や `version` は付けても無視）：

```json
{ "schedules": [
    { "id": 1, "enabled": true, "time": "07:00", "days": ["mon","tue","wed","thu","fri"],
      "target": "ac", "action": { "power": true, "mode": "heat", "temp": 20 } },
    { "enabled": true, "time": "23:30", "days": ["sun","mon","tue","wed","thu","fri","sat"],
      "target": "ac", "action": { "power": false } }
] }
```

処理：

```
ScheduleParseResult r;
if (!schedulesFromJson(body, ScheduleJsonKind::List, &r)) → 400 {"error": r.error}
int bad = -1;
ScheduleError e = hub.replaceSchedules(r.items, r.count, &bad);
if (e != None) → 400 {"error": bad >= 0 ? "schedules[<bad>]: " + errorMessage(e) : errorMessage(e)}
→ 200 schedulesToJson(hub.schedules(), List, hub.clockNow())
```

- 成功の応答は6節と同じ形（採番済みの id 付き）。画面はこれで描き直す。
- 検査の順と文言は D-03 6.3（`version` の2行を除く）・6.4 のとおり。失敗時は一覧を変えない。
- `ScheduleParseResult` は `Schedule` を `kScheduleMax` 件持つ（約 300 バイト）。`handle` の中のローカル変数にしてよい（ESP32 の loop タスクのスタック 8KB に対して小さい）。

### 8. GET /api/schedules/export

応答：

| ApiResponse のフィールド | 値 |
|---|---|
| `status` | 200 |
| `contentType` | `application/json` |
| `body` | `schedulesToJson(hub.schedules(), ScheduleJsonKind::Export, now)`（D-03 6.2 の形） |
| `downloadFilename` | `exportFilename(now)`（例 `irhub-schedules-20260925-2000.json`、未取得なら `irhub-schedules.json`） |

`now = hub.clockNow()` を1回だけ取り、本文とファイル名の両方に使う（`exportedAt` とファイル名の時刻がずれないように）。

本文の例（D-03 6.2）：

```json
{ "version": 1, "exportedAt": "2026-09-25T20:00:00+09:00",
  "schedules": [ { "id": 1, "enabled": true, "time": "07:00", "days": ["mon","tue","wed","thu","fri"],
                   "target": "ac", "action": { "power": true, "mode": "heat", "temp": 20 } } ] }
```

`web_bridge` は `downloadFilename` が空でなければ `Content-Disposition: attachment; filename="<名前>"` を付ける（12節）。画面は `fetch` でこの応答を受け取り、Blob にして保存する（ファイル名は `Content-Disposition` から読む。D-05 6.5）。API の形はこのまま変えない。

### 9. POST /api/schedules/import

**本文はエクスポートファイルの中身そのもの（JSON 本文）**。multipart（`<form>` のファイル送信）は使わない。画面の JS が `FileReader`／`File.text()` でファイルを読み、`fetch` で `Content-Type: application/json` の本文として送る（D-01 疑問4・D-03 6.3 の推測をここで確定）。理由：`ApiRequest::body` の形がほかと同じになり、`ApiRouter` を HTTP 非依存のまま保てる。

処理は7節と同じで、`ScheduleJsonKind::Export` を使う（`version` の検査が入る）。

- 成功（200）：6節の一覧の形（`max` 付き）。ファイルの内容で**置き換わる**（混ぜない）。
- 失敗（400）：D-03 6.3 の順と文言（`version missing`・`unsupported version`・`too many schedules (max 10)`・`schedules[0].time: must be HH:MM` など）。一覧は変わらない。
- NTP 未取得でも受け付ける（再起動直後に戻せるように。D-03 4.3）。

### 10. エラーの一覧

本文はすべて `{"error":"<文言>"}`。文言に `"` が入るもの（`unknown key "tmp"`）があるので、本文は ArduinoJson で組み立てる（文字列連結で作らない）。

#### 10.1 ルーティング（ApiRouter）

| 状況 | ステータス | 文言 |
|---|---|---|
| パスが2節の表に無い（`/api/foo`、`/api/status/`、`/favicon.ico` など `/` 以外すべて） | 404 | `not found` |
| パスはあるがメソッドが違う（`POST /api/status`、`GET /api/ac`、`DELETE /api/schedules` など。`HttpMethod::Other` を含む） | 405 | `method not allowed` |
| `/` に `GET` 以外（`POST /`、`PUT /`、`DELETE /` など） | 404 | `not found` |

- `/` の `GET` だけは web_bridge の `on("/", HTTP_GET, ...)` が受ける。`GET` 以外の `/` はこの登録に合わず `onNotFound` → `ApiRouter` に来る。`ApiRouter` の振り分け表（11節）に `/` は無いので「それ以外」＝ **404**（405 ではない）。

#### 10.2 共通（本文のあるエンドポイント）

| 文言 | 条件 | 対象 |
|---|---|---|
| `body too large` | 本文が上限を超える（512 または 8192 バイト） | 全部 |
| `invalid json` | JSON として読めない、空、一番外側がオブジェクトでない | 全部 |

#### 10.3 POST /api/ac（400）

| 文言 | 条件 | 出すところ |
|---|---|---|
| `unknown key "<キー>"` | 6つ以外のキー | api.cpp |
| `power: must be boolean` | `power` が bool でない | api.cpp |
| `mode: must be string` / `fan: must be string` / `swingV: must be string` / `swingH: must be string` | 文字列でない | api.cpp |
| `mode: unknown mode` / `fan: unknown fan` / `swingV: unknown swingV` / `swingH: unknown swingH` | D-02 2節の表に無い文字列 | api.cpp |
| `temp: must be integer` | 整数でない | api.cpp |
| `no ac fields` | `{}` | `AcError::EmptyPatch` |
| `temp not supported in this mode` | 更新後のモードで温度を指定できない | `AcError::TempNotSupported` |
| `temp out of range` | `cap::kTempMinC..kTempMaxC` の外 | `AcError::TempOutOfRange` |
| `fan not supported` / `swingV not supported` / `swingH not supported` | 選択肢の外 | `AcError::*NotSupported` |

#### 10.4 POST /api/light（400）

| 文言 | 条件 |
|---|---|
| `unknown key "<キー>"` | `button` 以外のキー |
| `button: missing` | `button` が無い |
| `button: must be string` | 文字列でない |
| `button: unknown button` | 6つ以外の文字列 |

#### 10.5 PUT /api/schedules・POST /api/schedules/import（400）

D-03 6.3・6.4 の文言をそのまま使う（ここには再掲しない。変えるときは D-03 と一緒に直す）。`replaceSchedules` が返す `DuplicateId` は `schedules[<i>]: duplicate id`（`<i>` は `badIndex`）。

### 11. ApiRouter の実装（lib/core/src/api.h/.cpp）

D-01 の `api.h` に定数と、テストしやすいように本文の解析関数を1つ足す。D-01 で決めた名前・フィールドは変えない。

```cpp
// lib/core/src/api.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include "hub.h"
#include "schedule_json.h"   // ScheduleJsonKind・schedulesToJson・schedulesFromJson・exportFilename・formatJstIso（D-01 の依存 api → schedule_json の範囲内）

namespace irhub {

enum class HttpMethod : uint8_t { Get, Post, Put, Other };

struct ApiRequest {
  HttpMethod  method;
  std::string path;   // クエリ文字列を除いたパス。例 "/api/ac"
  std::string body;   // 本文（JSON 文字列）。GET は空
};

struct ApiResponse {
  int         status = 200;
  std::string contentType = "application/json";
  std::string body;
  std::string downloadFilename;
};

constexpr size_t kApiSmallBodyMaxBytes = 512;   // /api/ac と /api/light の本文の上限（推測。最大の正当な本文は 110 バイト程度）

// POST /api/ac の本文 → AcPatch。形・型・文字列まで確かめる（値の範囲・選択肢は AcModel::validate）。
// 失敗なら false と *error（10.3 の api.cpp の文言、または body too large / invalid json）。
bool parseAcPatchJson(std::string_view body, AcPatch* out, std::string* error);

class ApiRouter {
 public:
  explicit ApiRouter(Hub& hub);
  ApiResponse handle(const ApiRequest& req);   // onNotFound から来るすべて（GET 以外の "/" も含む。無いパスは 404）
 private:
  ApiResponse getStatus();
  ApiResponse postAc(const std::string& body);
  ApiResponse postLight(const std::string& body);
  ApiResponse getSchedules();
  ApiResponse putSchedules(const std::string& body);
  ApiResponse getExport();
  ApiResponse postImport(const std::string& body);
  ApiResponse replaceFrom(const std::string& body, ScheduleJsonKind kind);   // put と import の共通部分（7節）
  Hub& hub_;
};

}  // namespace irhub
```

`api.cpp` の中（無名名前空間）の道具：

```cpp
ApiResponse jsonOk(std::string body);                        // 200
ApiResponse jsonError(int status, std::string_view message); // {"error": message} を ArduinoJson で作る
void writeAcState(JsonObject o, const AcState& s);           // 3節の "ac" の形（hasTemp=false なら temp は null）
void writeCapabilities(JsonObject o);                        // 3節の "acCapabilities"（cap:: だけから作る）
```

`clock.now` の文字列は api.cpp で作らない。**schedule_json 側の公開関数を使う**（`exportedAt` と同じ書式を1か所で作り、ずれないようにする）：

```cpp
// lib/core/src/schedule_json.h に公開される関数（D-03 に足す必要がある。要件への疑問 11）
// LocalTime → "YYYY-MM-DDTHH:MM:SS+09:00"（ゼロ埋め）。exportedAt と /api/status の clock.now の両方で使う
std::string formatJstIso(const LocalTime& t);
```

- `getStatus` は `now.synced` なら `o["now"] = formatJstIso(now.local)`、そうでなければ `o["now"] = nullptr`。
- D-03 の今の `schedule_json.h`（6節）にはこの関数が無い（`exportedAt` の書式は schedule_json.cpp の中で作る形）。D-03 に公開関数として足すまで、api.cpp に同じ書式のフォーマッタを別に作らない（実装項目では D-03 の修正を先に行う）。

`handle` の振り分け（パスを先に見て、合わなければ 404、パスが合ってメソッドが違えば 405）：

| path | Get | Post | Put |
|---|---|---|---|
| `/api/status` | `getStatus()` | 405 | 405 |
| `/api/ac` | 405 | `postAc()` | 405 |
| `/api/light` | 405 | `postLight()` | 405 |
| `/api/schedules` | `getSchedules()` | 405 | `putSchedules()` |
| `/api/schedules/export` | `getExport()` | 405 | 405 |
| `/api/schedules/import` | 405 | `postImport()` | 405 |
| それ以外 | 404 | 404 | 404 |

`HttpMethod::Other` は表のどのパスでも 405、表に無いパスでは 404。

`postAc`：

```cpp
ApiResponse ApiRouter::postAc(const std::string& body) {
  AcPatch patch;
  std::string err;
  if (!parseAcPatchJson(body, &patch, &err)) return jsonError(400, err);
  if (!hub_.applyAc(patch, &err)) return jsonError(400, err);   // D-02 の errorMessage
  JsonDocument doc;
  writeAcState(doc["ac"].to<JsonObject>(), hub_.acState());
  std::string out;
  serializeJson(doc, out);
  return jsonOk(std::move(out));
}
```

`parseAcPatchJson` の手順（4節の検査の順）：

```
if body.size() > kApiSmallBodyMaxBytes → "body too large"
JsonDocument doc; if (deserializeJson(doc, body.data(), body.size())) → "invalid json"
if (!doc.is<JsonObject>()) → "invalid json"
obj = doc.as<JsonObject>()
for (JsonPair kv : obj) if kv.key() が6つのどれでもない → "unknown key \"<key>\""
power：あれば is<bool>() でなければ "power: must be boolean"、out->power = as<bool>()
mode ：あれば is<const char*>() でなければ "mode: must be string"、parseAcMode が nullopt なら "mode: unknown mode"
temp ：あれば is<int>() でなければ "temp: must be integer"、out->tempC = as<int>()
fan / swingV / swingH：mode と同じ形
```

- キーの有無は **`for (JsonPair kv : obj)` の走査で集める方法だけ**で判定する。上の「知らないキー」の走査と同じループで、6つのキーそれぞれについて「出てきたか」の bool（`bool seen[6]`）と値（`JsonVariantConst`）を記録し、以降の形の検査は `seen` が true のキーだけを見る。`obj["temp"].isNull()` などでキーの有無を判定しない（ArduinoJson v7 では「キーが無い」と「値が null」の両方で true になり、`null` の値を「キーはあるが型が違う」＝ `must be ...` にできないため）。`/api/light` の `button` も同じ走査で `button: missing` を判定する。
- `is<int>()` が `26.5`・`1e10`・`true`・`"26"`・`null` に false を返すことは D-03 と同じく**要確認**（test_api で確かめる。false にならなければ `is<long long>`／`is<double>` と整数判定を自前で行う）。

使う ArduinoJson v7 の API は D-03 6 節の一覧と同じ（`JsonDocument`、`deserializeJson(doc, const char*, size_t)`、`is<T>()`、`as<T>()`、`for (JsonPair kv : obj)`、`kv.key().c_str()`、`to<JsonObject>()`、`to<JsonArray>()`、`JsonArray::add(value)`、`serializeJson(doc, std::string&)`）。null の書き込みは `o["temp"] = nullptr`（v7 で使える。**要確認**：ESP32 ビルドでも同じ）。

`getStatus` は `hub_.acState()`・`hub_.lastClimate()`・`hub_.clockNow()` を1回ずつ読むだけで、状態を変えず送信もしない。

### 12. src/web_bridge（WebServer との橋渡し）

判断を持たない。`ApiRequest` を詰めて `ApiRouter::handle` を呼び、`ApiResponse` をそのまま返すだけ。

```cpp
// src/web_bridge.h
#pragma once
#include <WebServer.h>
#include "api.h"

namespace irhub {

class WebBridge {
 public:
  WebBridge(WebServer& server, ApiRouter& router);
  void begin();          // on("/", HTTP_GET, ...) と onNotFound(...) を登録して server.begin()
 private:
  void handleRoot();     // send_P(200, "text/html", kIndexHtml)（細部は D-05）
  void handleApi();      // onNotFound から呼ぶ。下の手順
  WebServer& server_;
  ApiRouter& router_;
};

}  // namespace irhub
```

`handleApi()` の手順：

```cpp
ApiRequest req;
switch (server_.method()) {
  case HTTP_GET:  req.method = HttpMethod::Get;  break;
  case HTTP_POST: req.method = HttpMethod::Post; break;
  case HTTP_PUT:  req.method = HttpMethod::Put;  break;
  default:        req.method = HttpMethod::Other; break;
}
req.path = server_.uri().c_str();                                    // クエリは含まれない
if (server_.hasArg("plain")) req.body = server_.arg("plain").c_str();
const ApiResponse res = router_.handle(req);
if (!res.downloadFilename.empty()) {
  String cd = "attachment; filename=\"";
  cd += res.downloadFilename.c_str();
  cd += "\"";
  server_.sendHeader("Content-Disposition", cd);
}
server_.send(res.status, res.contentType.c_str(), res.body.c_str());
```

使う WebServer の API（Arduino core 2.x、D-01 7節の一覧の範囲）：`on(uri, HTTP_GET, handler)`、`onNotFound(handler)`、`method()`（`HTTPMethod` の `HTTP_GET`/`HTTP_POST`/`HTTP_PUT`）、`uri()`、`hasArg(name)`、`arg("plain")`、`sendHeader(name, value)`、`send(code, content_type, content)`、`begin()`、`handleClient()`。

要確認（I-実装時に core 2.x のソース `libraries/WebServer/src/Parsing.cpp` で確かめる）：
- 本文が `arg("plain")` に入るのは、Content-Type が `application/x-www-form-urlencoded` と `multipart/form-data` のどちらでもないとき。よって画面は `Content-Type: application/json` を付ける（1節）。
- `PUT` でも本文が読まれて `arg("plain")` に入ること（POST と同じ扱いか）。入らなければ、実装者が勝手に `POST` へ変えず（要件5章の `PUT` を守る）、実装時に報告して人が判断する（要件への疑問 8）。
- `uri()` がクエリ文字列を含まないこと。

メモリの扱い：WebServer（core 2.x）は、`ApiRouter` が本文の上限（`kApiSmallBodyMaxBytes`・`kScheduleJsonMaxBytes`）を確かめるより前に、Content-Length 分の本文をまるごと読んで `arg("plain")`（`String`）に入れ、さらに `req.body`（`std::string`）へコピーする。よってこの上限は RAM の守りにはならない（大きすぎる本文は `ApiRouter` に届く前にヒープを使う）。上限の役目は「おかしな本文を 400 `body too large` で断ること」だけとする。要件の範囲（LAN 内・自分だけが使う、N-AUTH）ではこれで足りるとし、Content-Length を先に見て断るなどの対策は作らない。

C++17：core は `std::optional`・`std::string_view` を使うので C++17 が要る。ESP32 のビルドで `-std=gnu++17` にするのは I-04（platformio.ini）で入れる。

### 13. 作らないもの

| 作らないもの | 理由 |
|---|---|
| 認証（パスワード、トークン、Basic 認証） | N-AUTH（LAN 内は認証なし）。外からは Tailscale の /32 公開だけ（F6、ESP32 側の対応なし） |
| 本体タイマーのキー（`timer`、`sleep`、`offTimer` など）とエンドポイント | F1-TIMER は未決。来たら `unknown key` で 400 |
| 選択肢専用のエンドポイント（`/api/capabilities` など） | `/api/status` に含めた（3節）。要件5章の表に無い |
| スケジュールの1件ずつの API（`POST /api/schedules/<id>`、`DELETE`） | 要件5章に無い（D-03 3.3） |
| 照明の状態（点灯中かどうか）を返すキー | F3・やらないこと（照明の点灯状態の検出） |
| 室温・湿度の履歴 | F5「履歴は持たない」 |
| 送信結果・実行履歴を返すキー、通知 | やらないこと（通知）。送信の成否は分からない |
| 設定の保存・読み込み API、OTA のエンドポイント | やらないこと（永続化・OTA） |
| CORS ヘッダ | 同一オリジンで足りる |
| multipart のファイル受信 | 9節（JSON 本文で送る） |

---

## 仮・未決の扱い

| 要件・値 | 状態 | この文書での扱い | 変わったときに直す場所 |
|---|---|---|---|
| API | 仮 | パス・JSON・エラーを本書で確定した。`web_bridge` は中身を知らない | パスや JSON が変われば `lib/core/src/api.cpp`（スケジュールの形は `schedule_json.cpp`）、`test/test_api/`、`web/index.html`、`tools/mock_server.py`。`src/web_bridge.cpp` は変えずに済む |
| F1-VALUES | 未決 | `acCapabilities` と `/api/ac` の値の検査は `cap::` の定数・関数だけから作る。`api.*` に数値・選択肢・`F1-VALUES` の文字列を書かない（allowed_in の外） | 直す場所なし（`ac_capabilities.h` が変われば `/api/status` と検査が自動で追従）。画面も `acCapabilities` を描くだけなので直さない |
| F1-TIMER | 未決 | 作らない。キーもエンドポイントも無い。`/api/ac` に来たら `unknown key` で 400 | D1 で「対応」と決まったら新しい作業項目で `/api/ac` のキーと `ac` の形に足す。「非対応」ならこのまま |
| F4-LIMIT（10件） | 仮 | `GET /api/schedules` の `max` と上限超過の文言は `kScheduleMax` から作る | `schedule.h` の `kScheduleMax` だけ |
| N-TIME | 仮 | `/api/status` の `clock.synced`・`clock.now`（未取得は `false`・`null`）。警告の文言と出し方は D-05。未取得でもスケジュール API は受け付ける | 方針が変われば `getStatus()` と D-05。`synced` の判定は D-06（`src/clock_esp32.cpp`） |
| F5 の更新間隔 30 秒 | 仮（F5 は決定） | API は `Hub::lastClimate()` を返すだけで周期を知らない | `hub.h` の `kClimateIntervalMs` だけ（API は直さない） |
| 本文の上限 512 バイト | 推測 | `api.h` の `kApiSmallBodyMaxBytes`。RAM の守りではなく、断るための上限（WebServer が先に Content-Length 分を確保するため。12節） | その定数1つ |
| C-TECH の WebServer（原本では「ESPAsyncWebServer は使わない」が仮） | 仮扱い | WebServer に触るのは `src/web_bridge.*` だけ | 変わっても `src/web_bridge.*` だけ |

---

## テスト観点

### native 単体テスト（`pio test -e native`、`test/test_api/`）で確かめること

`ApiRouter` を `Hub`（FakeIrSender・FakeClock・FakeClimateSensor）とつないで、`handle(ApiRequest)` の戻り値の `status` と、`body` を ArduinoJson で読み直した中身を確かめる。期待値の数値・選択肢は `cap::`・`kScheduleMax` から作り、直書きしない。

- ルーティング（API）：2節の各メソッド・パスが 200 系の処理に行く。`/api/foo`・`/api/status/`・`/favicon.ico` → 404 `not found`。`POST /`・`PUT /` → 404 `not found`（405 ではない、10.1）。`POST /api/status`・`GET /api/ac`・`PUT /api/light`・`Other` で `/api/schedules` → 405 `method not allowed`。404／405 のときに送信 0 回・状態不変。
- `/api/status`（F1・F5・N-TIME）：
  - 起動直後：`ac` が F2 の初期値、`climate.valid=false` で `temperature`・`humidity` が null、`clock.synced=false` で `now` が null。呼んでも `acCount==0`（N-BOOT）。
  - `acCapabilities` が `cap::` と一致（`tempMin/Max/Step`、`fan`・`swingV`・`swingH` の並び順、`modes` が4つ、`tempModes` が `cap::tempSupported` の真のモード）。
  - FakeClock を synced にすると `now` が `+09:00` 付きゼロ埋め（例 2026-01-05 07:03:09 → `"2026-01-05T07:03:09+09:00"`）。同じ時刻で export した本文の `exportedAt` と `/api/status` の `now` が同じ文字列（どちらも `formatJstIso` で作ることの確認）。
  - センサーを読ませると `temperature` が小数1桁（24.46 → 24.5、55.24 → 55.2）。読み取り失敗にすると `valid=false`・null。
  - `hasTemp==false` のモードがあれば `ac.temp` が null（無ければ `TEST_IGNORE_MESSAGE`、D-02 と同じ流儀）。
- `/api/ac`（F1）：
  - 先に `{"power":true}` で運転中にしてから（`acCount==1`）`{"temp":27}` → 200、応答の `ac.temp==27`、他は変わらず、`acCount` が1増えて2、`lastAc` が `acState()` と一致。
  - 生成直後（停止中）に `{"temp":27}` → 200、応答の `ac.temp==27`・`ac.power==false`、`acCount==0`（停止中に power を含まないパッチは送らない。D-01・D-02 6節）。
  - 全項目の本文 → 200。`{"mode":"heat"}` で暖房の記憶が復元される（D-02 4節の遷移表を API 経由で数行）。
  - 形のエラー（10.3 の api.cpp の行を全部）：`{"temp":26.5}`、`{"temp":"26"}`、`{"temp":null}`、`{"power":1}`、`{"mode":3}`、`{"mode":"Cool"}`、`{"fan":"turbo"}`、`{"swingV":"up"}`、`{"swingH":"x"}`、`{"timer":60}`（F1-TIMER を作っていないことの確認）→ それぞれの文言で 400。
  - 値のエラー：`{}` → `no ac fields`、`cap::kTempMaxC+1` → `temp out of range`、`cap::fanSupported` が false の列挙の文字列 → `fan not supported`。
  - 400 のときは必ず `acCount` が増えず、`acState()` が変わらない。
  - 検査の順：`{"x":1,"temp":"a"}` → `unknown key "x"`（知らないキーが先）。`{"fan":"turbo","temp":"a"}` → `temp: must be integer`（決まった順 power, mode, temp, fan…）。
  - `invalid json`：空文字、`[1]`、`"x"`、壊れた JSON。`body too large`：513 バイトの本文（512 は通る形で作って確かめる）。
  - FakeIrSender の `sendAc` が false を返しても 200。
- `/api/light`（F3）：6つの文字列それぞれで 200 `{"ok":true}`、`lightCount==1`、`lastLight` が一致。`{"button":"on"}`・`{"button":1}`・`{}`・`{"button":"night","x":1}` → 10.4 の文言で 400、送信 0 回。`sendLight` が false でも 200。
- スケジュール（F4・F4-IO）：
  - `GET /api/schedules`：起動直後 `{"max":kScheduleMax,"schedules":[]}`。
  - `PUT` で id 無し2件 → 200、応答に id 1・2。続く `GET` が同じ。
  - `PUT` の失敗（`kScheduleMax+1` 件、`time` 不正、重複 id → `schedules[1]: duplicate id`）で 400、`GET` の一覧が変わらない。
  - `GET /api/schedules/export`：200、`downloadFilename` が synced なら `irhub-schedules-YYYYMMDD-HHMM.json`、未取得なら `irhub-schedules.json`。本文の `exportedAt` とファイル名の時刻が一致（`clockNow()` を1回だけ使う）。
  - 往復：export の本文をそのまま `POST /api/schedules/import` → 200、一覧が元と同じ（id を含む）。
  - import の失敗：`version` 無し → `version missing`、`version: 2` → `unsupported version`。一覧は変わらない。
  - NTP 未取得（N-TIME）でも GET・PUT・export・import が 200。
  - スケジュール API のどれを呼んでも送信 0 回（D-03 5.2）。
- エラー本文の形：どの 400／404／405 も `{"error":"..."}` の1キーだけで、`unknown key "tmp"` のような `"` を含む文言も正しい JSON として読み直せる。
- N-AUTH：認証に当たる入力が `ApiRequest` に無い（型で保証。テストは不要だが、どのリクエストもヘッダ無しで通ることを上のテスト全体で確かめている）。

### 実機でしか確かめられないこと

- `pio run -e esp32` で `api.cpp`（`serializeJson(doc, std::string&)`、`o["temp"] = nullptr`）と `web_bridge.cpp` がビルドできる。
- スマホのブラウザで `GET /api/status` を開き、JSON が返る。温度・湿度が DHT20 の値、時刻が JST（F5・N-TIME）。
- `curl -X POST -H 'Content-Type: application/json' -d '{"power":true}' http://<IP>/api/ac` で運転中にしてから `-d '{"temp":27}'` → 200、エアコンが 27℃ になる（F1）。停止中に `{"temp":27}` を送ると 200 でエアコンには何も届かない（D-02 6節）。`-d '{"temp":99}'` → 400 で動かない。
- `curl -X POST ... -d '{"button":"night"}' http://<IP>/api/light` → 照明が常夜灯になる（F3）。
- `curl -X PUT -H 'Content-Type: application/json' -d '{"schedules":[...]}' http://<IP>/api/schedules` → 200（WebServer が PUT の本文を `arg("plain")` に入れることの確認。12節の要確認）。
- `curl 'http://<IP>/api/status?x=1'` → 200（`uri()` がクエリを含まないことの確認）。
- スマホで `/api/schedules/export` を開くとファイルとして保存され、名前が `irhub-schedules-YYYYMMDD-HHMM.json`（`Content-Disposition` が効く。iOS Safari・Android Chrome で確認）。
- 保存したファイルを画面から読み込むと一覧が戻る（再起動後、フェーズ5の判定。画面は D-05）。
- `Content-Type` を付けない・`application/x-www-form-urlencoded` で送ると本文が届かず `invalid json` になる（1節の決まりの根拠の確認）。
- Tailscale 経由（F6）でも同じ応答が返る（ESP32 側の対応なし）。

---

## 要件への疑問

1. **`/api/status` に選択肢を含めるか。** brief でここで決めることになっている。推測：含める（`acCapabilities`、3節）とした。要件5章の API 表にエンドポイントを足さずに済み、F1-VALUES の rule に合う。照明のボタン一覧とスケジュール上限は載せない（前者は F3 で決定済みの固定値、後者は `GET /api/schedules` の `max`）。
2. **送信の失敗を応答に出すか。** 要件に定めがない。推測：エアコン・照明とも、`IIrSender` が false を返しても 200 とした（D-01 5節・疑問8と同じ考え方。赤外線は一方通行で、届いたかはもともと分からない）。`Hub::pressLight` の戻り値は API では使わない。
3. **404／405 のステータス。** 要件はエラーを「400 と `{error}`」とだけ書く。推測：本文の検証エラーは要件どおり 400、パスが無い・メソッドが違うというルーティングのエラーは 404／405（本文は同じ `{"error":...}` の形）とした。すべて 400 にする方がよければ `ApiRouter::handle` の振り分けの2か所だけで変えられる。
4. **インポートの送り方。** 要件は「JSONファイルの内容で置き換える」だけ。推測：multipart ではなく、画面の JS がファイルを読んで JSON 本文として送るとした（D-01 疑問4・D-03 6.3 の推測を確定。9節）。
5. **`POST /api/ac` の成功時の本文。** 要件に定めがない。推測：更新後の状態を `{"ac":{...}}`（`/api/status` の `ac` と同じ形）で返すとした。画面がもう一度 `/api/status` を読まずに描き直せる。照明は状態を持たないので `{"ok":true}`（D-01 の例）とした。
6. **`/api/ac` の知らないキーと null。** 推測：知らないキーは 400（本体タイマーなど作っていない項目を黙って捨てないため）、値の `null` も 400 とした。そのため `/api/status` の `ac` をそのまま送り返すと、温度を持たないモードでは `"temp": null` で 400 になる。画面は変えた項目だけを送る前提（D-05 への申し送り）。
7. **本文の上限。** 要件に数値がない。推測：`/api/ac`・`/api/light` は 512 バイト、スケジュール系は D-03 の 8192 バイトとした。
8. **WebServer が PUT の本文を受け取れるか。** 要件5章は `PUT /api/schedules` を指定している。Arduino core 2.x の `WebServer` が PUT の本文を `arg("plain")` に入れるかは要確認（12節）。受け取れなかった場合に `POST` へ変えるかは要件（API、仮）の変更になるので、実装で分かった時点で人に判断してもらう。
9. **室温・湿度の表示の細部。** 要件は「値を表示する」だけ。推測：小数1桁に丸めて返し、最後の読み取りが失敗していたら（前に読めた値があっても）null にするとした（D-01 の「読み取り失敗が `lastClimate()` に反映される」に合わせた。古い値を出し続けない）。
10. **`/api/status` の時刻の形。** 要件は「時刻」とだけ書く。推測：エクスポートの `exportedAt` と同じ `"YYYY-MM-DDTHH:MM:SS+09:00"`、未取得なら null とした。画面は秒まで出すか分までにするかを D-05 で決める。
11. **時刻の文字列を作る関数の置き場所（D-03 への申し送り）。** `clock.now` と `exportedAt` の書式を1か所で作るため、api は schedule_json の公開関数 `std::string formatJstIso(const LocalTime& t)` を使う形にした（11節）。D-03 の `schedule_json.h`（6節）には今この関数が公開されていないので、D-03 に公開関数として足す必要がある（D-04 の範囲では直せない）。足すまで api.cpp に別のフォーマッタは作らない。
