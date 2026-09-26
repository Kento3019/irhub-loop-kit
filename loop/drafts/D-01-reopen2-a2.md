# 基本設計：モジュール構成と依存方向

作業項目：D-01／要件の原本：`docs/requirements.md` v0.4／要件ID：`docs/req-index.json`（D1 closed 後）

この文書では、全体の骨組みを決める。骨組みとは、モジュールの一覧、責務、依存の向き、ファイルの置き場所、lib/core と src/ の境界になるインターフェースのこと。各モジュールの中身（フィールド、検証規則、JSON の細かい形）は後続の設計書が決める。

| 後続の設計書 | 決めること |
|---|---|
| D-02 `02-ac-state.md` | `AcState` のフィールド、部分更新、モード別の記憶、`ac_capabilities.h` の中身 |
| D-03 `03-schedule.md` | `Schedule` のデータ構造、実行判定、エクスポート／インポートの形 |
| D-04 `04-api.md` | 各エンドポイントの JSON、エラー一覧、`/api/status` の形 |
| D-05 `05-ui.md` | 画面、`web/index.html` の埋め込み方式の細部 |
| D-06 `06-runtime.md` | 起動シーケンス、Wi-Fi（`src/wifi_manager` の細部）、NTP、DHT20 の周期、`secrets.h` |

後続の設計書は、この文書で決めたモジュール名・ファイル名・インターフェースの名前と依存の向きに従う。

D1（フェーズ1の受信結果、`docs/hw/phase1-capture.md`）で確定したことのうち、骨組みに関わるもの：
- エアコンのプロトコルは **HITACHI_AC296**。IRac で送れるのは運転・モード・温度・風量だけ（`IRac::hitachi296` は風向を扱わない）。
- 風向（上下・左右）と本体タイマー（F1-TIMER）は作らない。
- 照明（F3）はリモコンが電波式のため**対象外**。照明に関わるモジュール・型・API・画面・信号データは一切作らない。

---

## 対象要件

- [F1] エアコン操作：「3. モジュール一覧」の `ac_state`・`ac_capabilities`、「4. 境界のインターフェース」の `IIrSender::sendAc`（状態一式を1回で送る）、「5. Hub」の `applyAc`（停止中の送り方）、「7. src/ 側のモジュール」の `ir_sender_esp32`（IRac への変換、HITACHI_AC296）、「7a. 風向の扱い」
- [F3] 対象外（照明は作らない）：「12. やらないこと（設計上の禁止）」の照明の行。`light` モジュール・`LightButton`・`sendLight`・`pressLight`・`light_codes.h`・`POST /api/light` はどこにも置かない
- [F4] スケジュール：「3. モジュール一覧」の `schedule`・`schedule_json`、`Hub::tick` からの判定呼び出し、時刻は `IClock` から得て純関数に引数で渡す。対象はエアコンだけ
- [F5] 室温・湿度：`IClimateSensor`、`Hub::tick` での 30 秒周期の読み取り、`climate_dht20`（src 側）
- [N-STATE] 状態はメモリのみ：「12. やらないこと（設計上の禁止）」でフラッシュ保存系を使わないと決め、すべての状態を `Hub` が RAM に持つ
- [N-BOOT] 起動時にエアコンへ赤外線を送らない：「10. 起動時に赤外線を送らないための構造上の決まり」
- [UI] 画面：「9. ファイル配置」と「11. 画面の組み込み」の `web/index.html` → `tools/embed_html.py` → `src/generated/index_html.h` → `web_bridge` が `GET /` で返す流れ
- [API] HTTP API：「6. API ハンドラ」の `lib/core` の `ApiRouter`（HTTP 非依存の `handle(ApiRequest) → ApiResponse`）と `src/web_bridge`（WebServer との橋渡し）の分担
- [HW-PINS] ピン割り当て：「7. src/ 側のモジュール」の `src/pins.h` に 1 か所で定義
- [C-TECH] 技術制約：「8. 使うライブラリと置き場所」の表（espressif32 6.x = Arduino core 2.x、IRremoteESP8266 の IRac、DHT20 0x38、WebServer、ArduinoJson v7）

---

## 設計

### 1. 層の分け方（方針）

| 層 | 場所 | 依存してよいもの | テスト |
|---|---|---|---|
| core（中核） | `lib/core/src/` | C++17 標準ライブラリ、ArduinoJson v7 だけ。`Arduino.h`、IRremoteESP8266、WebServer、WiFi、Wire、DHT20 のヘッダは **include 禁止** | `pio test -e native`（PC の gcc） |
| platform（橋渡し） | `src/` | Arduino core 2.x、IRremoteESP8266、WebServer、WiFi、Wire、DHT20 ライブラリ、lib/core | `pio run -e esp32`（ビルド）と実機 |
| 画面 | `web/index.html` | ブラウザ標準の HTML/CSS/JS のみ（外部 URL・CDN 禁止） | PC 用モックサーバー（`tools/mock_server.py`）と実機 |
| テスト | `test/test_<name>/` | lib/core とテスト内のフェイク | native |

原則：
- **判断は core、ハードウェアに触るのは src**。src/ のクラスは「core のインターフェースを実装する」か「core の関数を呼んでその結果を実行する」だけにする。業務ルール（値の検証、部分更新、スケジュール判定、センサーの周期の判断）は持たない。
  - **例外は1つだけ：Wi-Fi の再接続（N-WIFI）**。再接続の回数・待ち時間・再起動の判断は `src/wifi_manager` に置き、PC（native）ではテストしない。確かめるのは実機だけ（人の判断・案A）。理由：Wi-Fi は `WiFi` と `ESP.restart()` に直接つながる処理で、今の作業項目の計画（I-05 は `src/**` に書く）に合わせるため。core には Wi-Fi に関わるモジュールを置かない。
  - この例外を他に広げない。`src/wifi_manager` 以外の src のファイルに、回数・時間・値の判断を書かない。
- core の中で時刻を得るのは `IClock` 経由だけ。`millis()`・`time()`・`getLocalTime()` を core で呼ばない。周期処理の基準になる経過ミリ秒は `Hub::tick(nowMs)` の引数で受け取る。
- core の型に IRremoteESP8266 の型（`stdAc::state_t` など）を持ち込まない。core は `stdAc::state_t` と同じ考え方の自前の型 `AcState` を持つ。`stdAc::state_t` への変換は src の `ir_sender_esp32.cpp` だけが行う。こうして C-TECH の「IRac 優先」と、core を native でビルドできることを両立させる。
- 文字列は core では `std::string` を使い、Arduino の `String` との変換は src で行う（`String::c_str()` → `std::string`、`std::string::c_str()` → `WebServer::send`）。

### 2. 依存方向

矢印は「include する／呼ぶ」の向き。逆向きの include は禁止。

```mermaid
flowchart TD
  subgraph web["web/"]
    HTML["index.html"]
  end
  subgraph tools["tools/"]
    EMB["embed_html.py (pre script)"]
  end
  subgraph src["src/ (Arduino 依存)"]
    MAIN["main.cpp<br/>setup/loop・組み立て"]
    WB["web_bridge<br/>WebServer ⇔ ApiRouter"]
    IRS["ir_sender_esp32<br/>IIrSender 実装 (IRac, HITACHI_AC296)"]
    CLK["clock_esp32<br/>IClock 実装 (NTP/JST)"]
    DHT["climate_dht20<br/>IClimateSensor 実装"]
    WIFI["wifi_manager<br/>Wi-Fi 接続・再接続の判断と実行 (N-WIFI)"]
    PINS["pins.h"]
    GEN["generated/index_html.h"]
  end
  subgraph core["lib/core/src/ (Arduino 非依存)"]
    API["api (ApiRouter)"]
    HUB["hub (Hub)"]
    PORTS["ports.h<br/>IIrSender / IClock / IClimateSensor"]
    AC["ac_state"]
    CAP["ac_capabilities.h"]
    SCH["schedule"]
    SJ["schedule_json"]
    TT["time_types.h"]
  end
  subgraph test["test/ (native)"]
    T["test_*.cpp"]
    F["fake_ports.h"]
  end

  HTML --> EMB --> GEN
  MAIN --> WB & IRS & CLK & DHT & WIFI & HUB & API
  WB --> API & GEN
  IRS --> PORTS & PINS
  CLK --> PORTS
  DHT --> PORTS & PINS
  API --> HUB & SJ & AC
  HUB --> PORTS & AC & SCH & TT
  PORTS --> AC & TT
  AC --> CAP
  SCH --> AC & TT
  SJ --> SCH
  T --> API & HUB & AC & SCH & SJ & F
  F --> PORTS
```

`wifi_manager` は core のどのモジュールにも依存しない（Arduino の `WiFi`・`ESP` と `include/secrets.h` だけを使う）。

依存の規則（native ビルドとレビューで確かめる。scripts/verify.py は include を検査しない）：
1. `lib/core/src/**` から `src/**`・`Arduino.h`・`IRremoteESP8266.h`・`IRac.h`・`IRsend.h`・`WebServer.h`・`WiFi.h`・`Wire.h`・`DHT20.h` を include しない。破れば `pio test -e native` のビルドが失敗する（`env:native` にこれらのライブラリを入れないため）。
2. core の中の向き：`api → hub → ports → (ac_state, time_types)`。`ac_state`・`time_types`・`ac_capabilities` は core の他モジュールを include しない（`ac_state → ac_capabilities` だけ可）。`schedule` は `ac_state`・`time_types` だけに依存する。
3. ArduinoJson を include してよいのは `api` と `schedule_json` だけ。状態モデル（`ac_state`・`schedule`・`hub`）は JSON を知らない。
4. `src/` 同士は `main.cpp` が組み立てる。`Hub`・`ApiRouter`・各実装クラス・`WifiManager` の実体を作るのは `main.cpp` だけ。`web_bridge` は `ir_sender_esp32` などの実装クラスを知らず、`ApiRouter` だけを知る。`wifi_manager` は `Hub`・`ApiRouter` を知らない。

### 3. モジュール一覧（lib/core/src/）

| モジュール（ファイル） | 責務 | 主な型・関数 | 詳細を決める設計書 |
|---|---|---|---|
| `core_version.h`（既存） | バージョン文字列 | `irhub::coreVersion()` | — |
| `time_types.h` | 日本時間の日時の値型 | `LocalTime`、`ClockReading` | D-03 |
| `ac_capabilities.h` | **使える値の唯一の置き場**（D1 の確定値）：モード4種（自動・冷房・除湿・暖房）、モード別の温度指定可否（自動は温度指定なし）、温度 16〜30℃、風量5段階（自動・静音・弱・中・強）、風向の選択肢（上下・左右とも `Off` だけ。7a 節） | `constexpr` の表 | D-02（I-06 で PENDING を外して確定値にする） |
| `ac_state.h/.cpp` | エアコンの状態一式、部分更新、値の検証、モード別の最後の設定、F2 初期値の表（`// 仮(F2)`） | `AcState`、`AcPatch`、`AcModel` | D-02 |
| `ports.h` | 外界との境界になる抽象クラス（純粋仮想） | `IIrSender`、`IClock`、`IClimateSensor`、`ClimateReading` | 本書 |
| `schedule.h/.cpp` | スケジュール一覧（上限件数の固定配列）、追加・編集・削除、実行判定（時刻を引数で受け取る純関数）。対象はエアコンだけ | `Schedule`、`ScheduleList`、`dueSchedules()` など | D-03 |
| `schedule_json.h/.cpp` | スケジュール一覧 ⇔ JSON（一覧・エクスポート・インポート）と検証 | `schedulesToJson()`、`schedulesFromJson()` | D-03 / D-04 |
| `hub.h/.cpp` | アプリの中心。全状態を RAM に持ち、ポートを使って送信・読み取り・スケジュール実行を行う | `Hub` | 本書（骨組み）、D-02/03/06（中身） |
| `api.h/.cpp` | HTTP 非依存の API ハンドラ。メソッド・パス・本文 → ステータス・本文 | `ApiRequest`、`ApiResponse`、`ApiRouter` | D-04 |

`namespace irhub` にすべて入れる。Wi-Fi に関わるモジュールは core に置かない（1節の例外）。照明のモジュール（旧 `light.h/.cpp`）は置かない（F3 対象外）。

### 4. 境界のインターフェース（lib/core/src/ports.h）

ここで名前とシグネチャを固定する。`AcState` の中身は D-02、`LocalTime` の細部は D-03 で決める（フィールドの追加は可、ここに書いた名前の変更は不可）。

```cpp
// lib/core/src/time_types.h
#pragma once
#include <cstdint>

namespace irhub {

// 日本時間（JST）の日時。曜日は 0=日曜 … 6=土曜（struct tm の tm_wday と同じ）
struct LocalTime {
  int16_t year;    // 例 2026
  int8_t  month;   // 1..12
  int8_t  day;     // 1..31
  int8_t  hour;    // 0..23
  int8_t  minute;  // 0..59
  int8_t  second;  // 0..59
  int8_t  wday;    // 0..6
};

struct ClockReading {
  bool      synced;     // NTP で一度でも時刻が取れていれば true。false の間 local は無意味
  LocalTime local;      // JST
  int64_t   epochSec;   // UNIX 時刻（秒）。synced=false のとき 0
};

}  // namespace irhub
```

```cpp
// lib/core/src/ports.h
#pragma once
#include "ac_state.h"     // AcState（D-02）
#include "time_types.h"

namespace irhub {

// 赤外線送信。エアコンの状態一式を1回で送る（F1）。
// 戻り値：送信処理を呼べたら true（相手に届いたかは分からない）。
class IIrSender {
 public:
  virtual ~IIrSender() = default;
  virtual bool sendAc(const AcState& state) = 0;
};

// 時計。src では NTP＋JST、テストでは手で進めるフェイク。
class IClock {
 public:
  virtual ~IClock() = default;
  virtual ClockReading now() = 0;
};

struct ClimateReading {
  bool  valid;        // 読み取り成功で true
  float temperatureC; // ℃
  float humidityPct;  // %RH
};

// 温湿度センサー。呼ばれたときに1回読む。周期の判断は Hub が行う。
class IClimateSensor {
 public:
  virtual ~IClimateSensor() = default;
  virtual ClimateReading read() = 0;
};

}  // namespace irhub
```

`IIrSender` に照明用・本体タイマー用のメソッドは**置かない**（F3・F1-TIMER は対象外）。赤外線の受信（IO14）もポートにしない。本体ファームは受信しない（受信は「やらないこと」の同期に当たるため）。Wi-Fi もポートにしない（1節の例外。判断と実行はどちらも `src/wifi_manager`）。

### 5. Hub（lib/core/src/hub.h）

アプリの状態を1か所に集め、`main.cpp` の `loop()` と `ApiRouter` の両方から使う。ESP32 の Arduino `loop()` は1本のタスクで回り、WebServer の `handleClient()` も同じ `loop()` の中で呼ぶ。このため `Hub` に排他制御は付けない。

```cpp
// lib/core/src/hub.h
#pragma once
#include <cstdint>
#include <string>
#include "ports.h"
#include "ac_state.h"
#include "schedule.h"

namespace irhub {

// センサーの読み取り周期。仮(F5)：要件「仮に30秒」
constexpr uint32_t kClimateIntervalMs = 30000;

class Hub {
 public:
  Hub(IIrSender& ir, IClock& clock, IClimateSensor& sensor);
  // コンストラクタは赤外線を送らない（N-BOOT）。状態は初期値（N-STATE、F2 の表）。

  // loop() から毎回呼ぶ。nowMs は millis() の値（49日で一周するので差は uint32_t の引き算で取る）。
  //  - センサー：一度も読んでいない、または前回から kClimateIntervalMs 以上経っていれば sensor.read()
  //  - スケジュール：clock.now() を1回取り、synced==false なら何もしない（N-TIME）。
  //    synced なら ScheduleList の判定関数（D-03）に LocalTime を渡し、実行すべきものを送る
  void tick(uint32_t nowMs);

  // API から使う操作（詳細な引数・エラーは D-02 / D-04）
  // 戻り値＝検証に通ったか。true なら内部の状態（モード別の記憶も含む）を更新し済み。
  // acState() が返すのは現在の AcState だけで、モード別の記憶は Hub の外から読めない。
  // 送信の条件（F1「停止中は運転の入切を含まない変更を送らない」。詳細は D-02）：
  //  - patch が power を含む（AcPatch::power.has_value()。値が true か false かは見ない）、
  //    または更新前が運転中（power=true）
  //    → sendAc(状態一式) をちょうど1回呼ぶ
  //  - 更新前が停止中（power=false）で、patch が power を含まない（!AcPatch::power.has_value()。温度・モード・風量だけ）
  //    → 状態は更新するが sendAc は呼ばない（戻り値は true）
  // false なら状態は変えず、送信もしない（*error に理由）。
  // sendAc の戻り値は applyAc の戻り値に含めない。sendAc が false でも更新した状態は戻さない
  // （赤外線は一方通行で、状態は「最後に指示した内容」を表すため）。
  bool applyAc(const AcPatch& patch, std::string* error);

  // 参照（/api/status など）
  const AcState&        acState() const;
  ScheduleList&         schedules();
  const ClimateReading& lastClimate() const;  // まだ読めていなければ valid=false
  ClockReading          clockNow();

 private:
  IIrSender&      ir_;
  IClock&         clock_;
  IClimateSensor& sensor_;
  AcModel         ac_;
  ScheduleList    schedules_;
  ClimateReading  climate_{false, 0.0f, 0.0f};
  bool            climateEverRead_ = false;
  uint32_t        lastClimateMs_   = 0;
  // スケジュールの二重実行防止のための記録は D-03 で追加する
};

}  // namespace irhub
```

`AcPatch` の `swingV`・`swingH`（D-02、実装済み）は型としては残るが、API もスケジュールもこれらを埋めない（7a 節）。`applyAc` の中身は変えない。

呼び出しの流れ：

```mermaid
sequenceDiagram
  participant Phone as スマホ
  participant WS as WebServer (src/web_bridge)
  participant R as ApiRouter (core)
  participant H as Hub (core)
  participant IR as IIrSender (src/ir_sender_esp32)
  Phone->>WS: POST /api/ac {"temp":27}
  WS->>R: handle({POST, "/api/ac", body})
  R->>R: JSON 解析・AcPatch に変換（D-04。swingV/swingH は受け付けない）
  R->>H: applyAc(patch, &err)
  H->>H: 検証・部分更新（D-02）
  alt patch.power.has_value()、または更新前が運転中
    H->>IR: sendAc(状態一式)
    IR-->>H: true
  else 停止中に power を含まないパッチ
    Note over H: 状態だけ更新し、送らない
  end
  H-->>R: true
  R-->>WS: {200, "application/json", {...}}
  WS-->>Phone: 200
```

### 6. API ハンドラ（lib/core/src/api.h）

```cpp
// lib/core/src/api.h
#pragma once
#include <cstdint>
#include <string>
#include "hub.h"

namespace irhub {

enum class HttpMethod : uint8_t { Get, Post, Put, Other };

struct ApiRequest {
  HttpMethod  method;
  std::string path;   // クエリ文字列を除いたパス。例 "/api/ac"
  std::string body;   // 本文（JSON 文字列）。GET は空
};

struct ApiResponse {
  int         status = 200;                     // 200 / 400 など（一覧は D-04）
  std::string contentType = "application/json";
  std::string body;                             // エラー時は {"error":"理由"}
  std::string downloadFilename;                 // 空でなければ Content-Disposition: attachment を付ける（export 用）
};

class ApiRouter {
 public:
  explicit ApiRouter(Hub& hub);
  // "/api/" で始まるパスだけを扱う。GET / は扱わない（web_bridge が HTML を返す）
  ApiResponse handle(const ApiRequest& req);
 private:
  Hub& hub_;
};

}  // namespace irhub
```

ルーティング表（要件5章 v0.4 のとおり。中身の JSON は D-04）：

| メソッド | パス | ApiRouter が呼ぶもの |
|---|---|---|
| GET | `/api/status` | `hub.acState()`、`hub.lastClimate()`、`hub.clockNow()`、`ac_capabilities.h` の選択肢（風向を除く） |
| POST | `/api/ac` | `hub.applyAc()` |
| GET | `/api/schedules` | `schedulesToJson(hub.schedules())` |
| PUT | `/api/schedules` | `schedulesFromJson()` → 置き換え |
| GET | `/api/schedules/export` | `schedulesToJson()`（エクスポート形式）＋ `downloadFilename` |
| POST | `/api/schedules/import` | `schedulesFromJson()`（エクスポート形式）→ 置き換え |

`POST /api/light` は作らない（F3 対象外）。上の表に無いパス・メソッドは D-04 のエラー（404 など）で返す。

入出力の実例（値の形は D-04 で確定）：

```json
// ApiRequest
{ "method": "POST", "path": "/api/ac", "body": "{\"power\":true,\"mode\":\"cool\",\"temp\":26,\"fan\":\"auto\"}" }
// ApiResponse（成功）
{ "status": 200, "contentType": "application/json", "body": "{\"ok\":true}", "downloadFilename": "" }
// ApiResponse（失敗：温度が範囲外）
{ "status": 400, "contentType": "application/json", "body": "{\"error\":\"temp out of range\"}", "downloadFilename": "" }
// ApiRequest（風向を含む：受け付けない。7a 節）
{ "method": "POST", "path": "/api/ac", "body": "{\"swingV\":\"auto\"}" }
// ApiResponse
{ "status": 400, "contentType": "application/json", "body": "{\"error\":\"unknown field: swingV\"}", "downloadFilename": "" }
```

### 6a. Wi-Fi の接続と再接続（src/wifi_manager、N-WIFI）

要件（決定）は「Wi-Fi が切れたら再接続を最大3回（計30秒程度）試み、すべて失敗したら再起動して再接続をやり直す」。判断と実行の両方を `src/wifi_manager` に置く（1節の例外）。native テストは作らず、実機で確かめる（「テスト観点」）。作るのは I-05（書ける場所 `src/**`）。

```cpp
// src/wifi_manager.h
#pragma once
#include <cstdint>

namespace irhub {

constexpr uint8_t  kWifiMaxAttempts      = 3;      // N-WIFI（決定）：最大3回
constexpr uint32_t kWifiAttemptTimeoutMs = 10000;  // 仮(N-WIFI)：「計30秒程度」を 3回×10秒 とした

class WifiManager {
 public:
  WifiManager(const char* ssid, const char* pass);  // secrets.h の値を main.cpp が渡す
  // setup() で1回呼ぶ。IP 固定（N-IP）の WiFi.config() → WiFi.begin(ssid, pass)。
  // 最初の接続開始も「1回目の試行」として数える（試行中、attempts_=1、attemptMs_=nowMs）。
  void begin(uint32_t nowMs);
  // loop() から毎回呼ぶ。WiFi.status() == WL_CONNECTED を見て、下の表のとおり動く。ブロックしない。
  void tick(uint32_t nowMs);
  bool connected() const;    // WiFi.status() == WL_CONNECTED
 private:
  const char* ssid_;
  const char* pass_;
  bool        trying_    = false;
  uint8_t     attempts_  = 0;
  uint32_t    attemptMs_ = 0;   // 今の試行を始めた時刻（millis()）
};

}  // namespace irhub
```

`tick` の状態遷移（経過時間は `nowMs - attemptMs_` を uint32_t の引き算で取る。`millis()` の一周でも正しく数える）：

| 今の状態 | 入力 | すること | 次の状態 |
|---|---|---|---|
| 接続中（trying_=false） | 接続している | 何もしない | 接続中 |
| 接続中 | 接続していない | `WiFi.disconnect()` → `WiFi.begin(ssid_, pass_)` | 試行中、attempts_=1、attemptMs_=nowMs |
| 試行中 | 接続している | 何もしない | 接続中、attempts_=0 |
| 試行中 | 接続していない、経過 < 10000 | 何もしない | 試行中（変化なし） |
| 試行中 | 接続していない、経過 ≥ 10000、attempts_ < 3 | `WiFi.disconnect()` → `WiFi.begin(ssid_, pass_)` | 試行中、attempts_+1、attemptMs_=nowMs |
| 試行中 | 接続していない、経過 ≥ 10000、attempts_ == 3 | `ESP.restart()` | （再起動するので以後なし） |

例：`begin(0)` の後つながらなければ 10000ms・20000ms で再試行、30000ms で再起動する（起動時も約30秒で再起動）。運用中に 1000ms で切れたと気付けば、1000・11000・21000ms に試行して 31000ms で再起動する。

D-06 への申し送り：
- ESP32 Arduino core 2.x の `WiFi` は自動再接続が既定で有効で、自前の再接続とぶつかるおそれがある。`WiFi.setAutoReconnect(false)` で自前の再接続だけにするか、自動再接続に任せて回数・時間だけを数えるかを D-06 で決める。
- `WiFi.config()`（N-IP、仮）の呼び方（固定 IP を使うか DHCP 予約に任せるか）も D-06 で決める。
- 判断が src にあって native テストが無いので、`WifiManager` には上の表以外の判断を足さない。

### 7. src/ 側のモジュール

| ファイル | 責務 | 使うライブラリ API（実在を確認済みのもの。版の違いは要確認） | 詳細 |
|---|---|---|---|
| `src/main.cpp` | 実体（`Hub`、`ApiRouter`、各実装クラス、`WifiManager`、`WebServer`）の生成と組み立て、`setup()`／`loop()`。`loop()` では `server.handleClient()`、`wifi.tick(millis())`、`hub.tick(millis())` を呼ぶ。`delay()` で長く止めない | `Serial.begin(115200)`、`millis()` | D-06 |
| `src/pins.h` | ピン番号の唯一の定義 | — | 本書 |
| `src/ir_sender_esp32.h/.cpp` | `IIrSender` の実装。`AcState` → `stdAc::state_t` に変換して `IRac::sendAc()`。`protocol` は `decode_type_t::HITACHI_AC296` の1か所。風向は `swingv = kOff`・`swingh = kOff` で渡す（`IRac` の HITACHI_AC296 の経路は風向を使わない）。I-06 までは Serial に出すだけのスタブ | IRremoteESP8266：`IRac(uint16_t pin)`、`bool IRac::sendAc(const stdAc::state_t)`、`stdAc::state_t`、`decode_type_t::HITACHI_AC296`。IRac の HITACHI_AC296 の経路（`IRac::hitachi296`）が受け取る項目は運転・モード・温度・風量（D1 の記録）。風量の変換は取得済みのライブラリ（`.pio/libdeps/dump/IRremoteESP8266/src/ir_Hitachi.cpp` の `IRHitachiAc296::convertFan`）で確かめた：`kMin`→`kHitachiAc296FanSilent`（静音）、`kLow`→Low、`kMedium`→Medium、`kHigh`・`kMax`→High、それ以外（`kAuto`）→Auto。よって静音＝`AcFan::Min`→`kMin`。`kMax` は `kHigh` と同じ信号になるので、`cap::kFanChoices` に `Max` を入れない（自動・静音(Min)・弱・中・強の5つ）。温度は `IRac::hitachi296` が `setMode()` の後に `setTemp()` を呼び、`IRHitachiAc296::setTemp` はモードが自動のとき温度を `kHitachiAc296TempAuto` に置き換える（同ファイルで確認）。よって自動のときは `degrees` に何を入れても温度は送られず、「自動は温度指定なし」は IRac の経路でも守られる。I-06 の変換では自動のとき `degrees` に `AcState::temp` をそのまま入れてよい（特別扱いしない） | D-02、I-06 |
| `src/clock_esp32.h/.cpp` | `IClock` の実装。NTP 設定と JST、`synced` の判定 | Arduino core：`configTzTime()`、`getLocalTime(struct tm*, uint32_t)`、`time()` | D-06 |
| `src/climate_dht20.h/.cpp` | `IClimateSensor` の実装 | `Wire.begin(sda, scl)`、DHT20 ライブラリ（RobTillaart）：`DHT20(TwoWire*)`、`bool begin()`、`read()`（戻り値の型は版により `int`／`int8_t` などがあり**要確認**。実装は戻り値を `DHT20_OK`（0）と比べることだけに依存し、型を決め打ちしない）、`float getTemperature()`、`float getHumidity()`。連続読み取りは 1000ms 以上空ける必要あり（30 秒周期なので問題なし） | D-06 |
| `src/wifi_manager.h/.cpp` | Wi-Fi の接続開始、IP 固定（N-IP）の設定、再接続の判断と実行、全失敗時の再起動（N-WIFI、6a 節）。src に判断を置く唯一の例外 | `WiFi.config()`、`WiFi.begin(ssid, pass)`、`WiFi.disconnect()`、`WiFi.status()`（`WL_CONNECTED`）、`WiFi.setAutoReconnect(bool)`（使うかは D-06）、`ESP.restart()` | D-06 |
| `src/web_bridge.h/.cpp` | WebServer と `ApiRouter` の橋渡し。`GET /` は `kIndexHtml` を返す。それ以外は `onNotFound` で受けて `ApiRequest` に詰めて `ApiRouter::handle()` を呼び、`ApiResponse` をそのまま返す | `WebServer(int port)`、`on(uri, HTTPMethod, handler)`、`onNotFound(handler)`、`method()`、`uri()`、`arg("plain")`（本文）、`sendHeader()`、`send(code, type, content)`（API 応答）、`send_P(code, type, content)`（`GET /` の HTML。`String` へのコピーを避ける）、`handleClient()`、`begin()` | D-04、D-05 |
| `src/generated/index_html.h` | `web/index.html` を埋め込んだ `const char kIndexHtml[]`。`tools/embed_html.py` が生成。手で編集しない | — | D-05 |
| `include/secrets.h` | SSID／パスワード（Git 管理外）。見本は `include/secrets.h.example` | — | D-06 |

照明の信号データ（旧 `src/light_codes.h`）と、照明用の `IRsend::sendRaw` は使わない。送信は `IRac` だけ。

**押したボタンを表すバイトについて（PROTO、H-2 で確かめる）**：受信した HITACHI_AC296 の信号には、押したボタンを表すバイト（`state[11]`：運転 0x13、モード 0x41、風量 0x42、温度 0x43／0x44）がある（`docs/hw/phase1-capture.md`）。`IRac` はこのバイトを埋めない（`stdAc::state_t` にボタンの概念が無い）。エアコンがこのバイトの値によらず状態一式を受け付けるかは H-2（実機）で確かめる。受け付けない場合の対処（`IRHitachiAc296` クラスを直接使ってボタンのバイトを設定するなど）は、その結果を見て I-06 で `ir_sender_esp32.cpp` の中だけで行う。core・`IIrSender` のシグネチャは変えない（`sendAc` は「状態一式」だけを受け取る。どの項目が変わったかを src に渡す必要が出たら、そのときに要件への疑問として上げる）。

### 7a. 風向の扱い（F1、D1 で「作らない」）

| 場所 | 扱い |
|---|---|
| `AcState`・`AcPatch`・`AcModel`（D-02、実装済み T-02・I-01） | `swingV`・`swingH` のフィールドと列挙は**残す**（インターフェースを変えない。手戻りを小さくするため） |
| `ac_capabilities.h` | 上下・左右とも選択肢は `Off` だけ。標準値も `Off`（D-02 の「抑えられない場合、上下風向の選択肢を Off だけにする」と同じ扱い） |
| `ir_sender_esp32.cpp` | `stdAc::state_t::swingv = kOff`、`swingh = kOff` で渡す |
| API（`POST /api/ac` の受け付け、`GET /api/status` の状態と選択肢） | 出さない・受け付けない。`ApiRouter` は `AcPatch::swingV`・`swingH` を常に空のままにする |
| スケジュール（`Schedule` の操作、エクスポート／インポート） | 風向を持たない・受け付けない |
| 画面 | 風向の操作を置かない |
| テスト（T-02 の `test/test_ac_state/test_main.cpp` とテスト計画） | TC-N06 `test_apply_swingv_only` は `swingV=AcSwingV::Auto` を直書きしており、`cap::kSwingVChoices` を `Off` だけにすると `SwingVNotSupported` になって落ちる。T-02 は差し戻し済み（人の判断付き）で、test-designer が TC-N06 を TC-N07 と同じ形（`cap::kSwingVChoices` から `cap::kSwingVDefault` 以外の選択肢を探し、無ければ `TEST_IGNORE_MESSAGE`）に直し、ほかの仮値の直書き（モード・温度・風量など）も `cap::` の定数から作る形に直す。テスト計画の TC-N06 の記述も合わせて直す |

**順番**：H-DESIGN → T-02（テストの直し）→ I-06（`ac_capabilities.h` を確定値にする）。I-06（implementer）は test/ を書けないため、T-02 を先に済ませる。こうすれば I-06 で風向の選択肢を `Off` だけにしても既存テストは落ちない（TC-N06・TC-N07 は IGNORE になる）。

### 8. 使うライブラリと置き場所（C-TECH）

| ライブラリ | 使う場所 | platformio.ini | 状態 |
|---|---|---|---|
| ArduinoJson v7（`JsonDocument`、サイズ指定なし） | lib/core（api, schedule_json） | `[common] lib_deps_core`（既存、`^7.2.0`） | 既存 |
| IRremoteESP8266 | src のみ（`ir_sender_esp32` の `IRac`） | `env:esp32`（既存、`^2.8.6`） | 既存 |
| DHT20（RobTillaart） | src のみ | `env:esp32` に追加（I-05）。登録名・版は要確認（推測：`robtillaart/DHT20`） | 追加 |
| WebServer / WiFi / Wire | src のみ | Arduino core 2.x に同梱 | — |
| Unity | test のみ | `env:native` の `test_framework = unity`（既存） | 既存 |

`env:native` に IRremoteESP8266・DHT20 を入れない（core がそれらに依存していないことを native ビルドで確かめるため）。platform は `espressif32@^6.9.0`（Arduino core 2.x）のまま。3.x 系（pioarduino）にしない。

### 9. ファイル配置（完成時）

```
platformio.ini                 # env:esp32 / env:native / env:dump、extra_scripts = pre:tools/embed_html.py
include/
  secrets.h                    # Git 管理外
  secrets.h.example
lib/core/src/
  core_version.h
  time_types.h
  ac_capabilities.h            # F1-VALUES の確定値はここだけ
  ac_state.h / ac_state.cpp
  ports.h
  schedule.h / schedule.cpp
  schedule_json.h / schedule_json.cpp
  hub.h / hub.cpp
  api.h / api.cpp
src/
  main.cpp
  pins.h
  ir_sender_esp32.h / ir_sender_esp32.cpp
  clock_esp32.h / clock_esp32.cpp
  climate_dht20.h / climate_dht20.cpp
  wifi_manager.h / wifi_manager.cpp   # N-WIFI の判断と実行（native テストなし）
  web_bridge.h / web_bridge.cpp
  generated/index_html.h       # 生成物
web/
  index.html                   # 画面の唯一のソース
tools/
  embed_html.py
  mock_server.py
  phase1_dump/                 # I-HW1
test/
  test_smoke/test_main.cpp     # 既存
  test_ac_state/{test_main.cpp, fake_ports.h}
  test_schedule/{test_main.cpp, fake_ports.h}
  test_api/{test_main.cpp, fake_ports.h}
```

照明のファイル（`light.h/.cpp`、`light_codes.h`、照明のテスト）は作らない。Wi-Fi のテストディレクトリも作らない。フェイクは各テストディレクトリに `fake_ports.h` として置く。作業項目ごとに書ける場所が `test/test_<name>/**` に限られるので、共有ディレクトリは作らない。フェイクの最低限の形：

```cpp
// test/test_<name>/fake_ports.h（例）
struct FakeIrSender : irhub::IIrSender {
  int acCount = 0; irhub::AcState lastAc{};
  bool result = true;   // false にすると送信失敗を真似る
  bool sendAc(const irhub::AcState& s) override { ++acCount; lastAc = s; return result; }
};
struct FakeClock : irhub::IClock {
  irhub::ClockReading r{false, {}, 0};
  irhub::ClockReading now() override { return r; }
};
struct FakeClimateSensor : irhub::IClimateSensor {
  int readCount = 0; irhub::ClimateReading next{true, 25.0f, 50.0f};
  irhub::ClimateReading read() override { ++readCount; return next; }
};
```

`src/pins.h`（HW-PINS、決定）：

```cpp
// src/pins.h
#pragma once
#include <cstdint>

namespace irhub::pins {
constexpr uint8_t kIrSend = 4;   // IO4 → 1kΩ → 2SC1815 ベース。赤外線 LED（エアコン向き。2個目は予備）
constexpr uint8_t kI2cSda = 21;  // DHT20 SDA（10kΩ で 3.3V へプルアップ）
constexpr uint8_t kI2cScl = 22;  // DHT20 SCL（10kΩ で 3.3V へプルアップ）
constexpr uint8_t kDht20Addr = 0x38;  // DHT20 の I2C アドレス（固定）
// IO14（受信モジュール OUT）は本体ファームでは使わない。フェーズ1の env:dump（tools/phase1_dump/）だけが使う
}  // namespace irhub::pins
```

ピン番号を書いてよいのは `src/pins.h` と `tools/phase1_dump/` だけ。

### 10. 起動時に赤外線を送らないための構造上の決まり（N-BOOT）

1. 赤外線を送る経路は `IIrSender::sendAc` の1つだけ。src/ で `IRac` の送信メソッドを呼んでよいのは `ir_sender_esp32.cpp` だけ。
2. `IIrSender::sendAc` を呼んでよいのは `Hub::applyAc` と `Hub::tick` のスケジュール実行の2か所だけ。
3. `Hub` のコンストラクタと `setup()` は `sendAc` を呼ばない。`IrSenderEsp32` の初期化（`IRac` の生成など）は送信しない。
4. 起動直後はスケジュール一覧が空（N-STATE）。`IClock` が `synced=true` になる前は判定しない。NTP が取れた時点で、起動前や未取得の間に過ぎた時刻の分を遡って実行しない（詳細は D-03）。
5. `WifiManager` の `ESP.restart()` による再起動も、ほかの再起動と同じく上の1〜4に従う（再起動後に送らない）。
6. 以上により、起動・再起動から最初のエアコンへの送信までには「画面からの操作」か「起動後に画面から登録したスケジュールの時刻到来」が必ず挟まる。

### 11. 画面の組み込み（UI）

```mermaid
flowchart LR
  A["web/index.html<br/>（HTML+CSS+JS 1ファイル）"] -- "pio run の前に<br/>extra_scripts pre:" --> B["tools/embed_html.py"]
  B --> C["src/generated/index_html.h<br/>const char kIndexHtml[]"]
  C --> D["web_bridge: GET / → send_P(200, text/html, kIndexHtml)"]
  A -- "PC で確認" --> E["tools/mock_server.py<br/>（API を真似る）"]
```

- `send(200, "text/html", kIndexHtml)` と書くと、HTML 全体がいったん Arduino の `String` にコピーされ、HTML の大きさ分のヒープを使う。これを避けるため `send_P(200, "text/html", kIndexHtml)`（または長さ指定版 `send_P(code, type, content, contentLength)`）を使う。どちらを使うかと、ESP32 の Arduino core 2.x での引数の型は D-05 で確かめる（要確認）。
- ビルドツール（npm、React など）は使わない。埋め込みの細部（エスケープ方式、生文字列リテラルか配列か）は D-05 で決める。
- 画面の構成は「上部（室温・湿度、時刻、NTP の状態）」「エアコンカード（運転／停止、モード、温度、風量）」「スケジュール」。照明カードと風向の操作は置かない。
- 画面は選択肢（モード・温度範囲・風量）を HTML に直書きせず、API から受け取って描く（F1-VALUES の rule）。どのエンドポイントで渡すかは D-04。

### 12. やらないこと（設計上の禁止）

要件1章「やらないこと」と、D1 で「作らない」と決まった項目を、このモジュール構成では次のように守る。

| やらないこと | 構成上の扱い |
|---|---|
| 状態・スケジュールの永続化 | `Preferences`、`SPIFFS`、`LittleFS`、`EEPROM` を include しない。`Hub` のメンバだけに持つ（`WifiManager` の試行回数も RAM のみで、再起動をまたいで数えない） |
| OTA | `ArduinoOTA`、`Update` を使わない |
| クラウド・通知・スマートスピーカー | 外部へ HTTP 接続するモジュールを作らない（NTP を除く） |
| 室温による自動運転 | `Hub::tick` は `lastClimate()` を送信判断に使わない |
| 照明の操作（F3 対象外） | 照明の型・モジュール・信号データ・ポートのメソッド・API・画面・スケジュールの対象を作らない。電波を出すモジュールも作らない |
| 備品の分解・改造 | ソフトウェアの構成には現れない（配線は要件6章のとおり外付けだけ） |
| 純正リモコンとの同期 | 本体ファームで赤外線受信（IO14）を初期化しない |
| 風向（上下・左右） | 7a 節。状態モデルの項目は残すが、選択肢は `Off` だけにし、API・画面・スケジュールに出さない |
| 本体タイマー（F1-TIMER、対象外） | 型・メソッド・API・画面のどこにも作らない。F4 スケジュールで代替する |

---

## 仮・未決の扱い

| 要件 | 状態 | この文書での扱い | 変わったときに直す場所 |
|---|---|---|---|
| UI | 仮 | 1ファイルの `web/index.html` を埋め込んで `GET /` で返す流れだけを決めた | 画面構成が変わっても `web/index.html` と D-05 だけ。埋め込み方式が変われば `tools/embed_html.py` と `src/web_bridge.cpp` |
| API | 仮 | `ApiRouter::handle(ApiRequest) → ApiResponse` の形とルーティング表だけを決めた | パスや JSON が変われば `lib/core/src/api.cpp`（と D-04、test_api）。`web_bridge` は変えずに済む |
| N-BOOT | 仮（原本は「決定案」） | 「10. 起動時に赤外線を送らないための構造上の決まり」 | 仕様が変わった場合（例：起動時に停止を送る）は、`Hub` のコンストラクタではなく `main.cpp` の `setup()` 末尾に1か所追加する |
| C-TECH の WebServer | 決定（原本では「ESPAsyncWebServer は使わない」が仮） | WebServer に触るのは `src/web_bridge.cpp` だけ | ESPAsyncWebServer に変える場合は `src/web_bridge.*` と `platformio.ini` だけ |
| F5 の 30 秒 | F5 は決定、周期は原本で「仮に30秒」 | `hub.h` の `kClimateIntervalMs = 30000`（`// 仮(F5)`） | その定数1つ |
| PROTO | 決定（HITACHI_AC296） | core はプロトコルを知らない。プロトコル名は `ir_sender_esp32.cpp` で `stdAc::state_t::protocol` に入れる1か所だけ。押したボタンのバイト（`state[11]`）が要るかは H-2 で確かめる（7節） | H-2 で受け付けられなければ `src/ir_sender_esp32.cpp` の中だけ |
| F1-VALUES | 決定 | 値は `lib/core/src/ac_capabilities.h` だけに置く（モード4種、温度 16〜30℃、自動は温度指定なし、風量5段階、風向は `Off` だけ）。この文書では置き場所と値の要旨だけを決め、定数の形は D-02 | I-06 が `ac_capabilities.h` の `PENDING(F1-VALUES)` を外して確定値にする。lib/ と src/ で直すのはこのファイルだけ。テストは仮値を直書きしている箇所（TC-N06 の `swingV=Auto` など）があるので、I-06 より前に T-02（test-designer）が `cap::` の定数から作る形に直す（7a 節） |
| F1-TIMER | 対象外 | 作らない。`IIrSender`・`AcState`・API・画面のどこにも入れない | — |
| F3 | 対象外 | 作らない（12節） | — |
| N-WIFI（本書の対象外だが境界に関わる） | 決定（「計30秒程度」の割り振りは `// 仮(N-WIFI)`） | 判断と実行の両方を `src/wifi_manager` に置く（人の判断・案A）。native テストなし、実機で確かめる（6a 節） | 1回の待ち時間・回数が変われば `src/wifi_manager.h` の `kWifiAttemptTimeoutMs`・`kWifiMaxAttempts` の2つだけ |
| N-IP | 仮 | `WiFi.config()` を `src/wifi_manager` が `WiFi.begin()` の前に呼ぶ、という置き場所だけを決めた | D-06 と `src/wifi_manager.cpp` |
| F2 | 仮（D2 未完） | 初期値の表は `ac_state` の中の1か所（`// 仮(F2)`）。詳細は D-02 | D2 が閉じたらその表だけ |

---

## テスト観点

### native 単体テスト（`pio test -e native`）で確かめること
- 依存の境界：lib/core が `Arduino.h` などなしで native ビルドできる（test_smoke を含む全テストがビルドできること自体で確かめる）。
- N-BOOT：`Hub` を生成しただけでは `FakeIrSender` の `acCount == 0`。`tick()` を何度呼んでも、スケジュールが空なら送信 0 回。
- N-BOOT／N-TIME：`FakeClock` が `synced=false` の間は、スケジュールを登録していても `tick()` で送信しない。
- N-STATE：新しく生成した `Hub` の `acState()` が F2 の初期値、`schedules()` が 0 件、`lastClimate().valid == false`。
- F5：`tick(0)` で `readCount == 1`（初回は即読む）、`tick(29999)` で 1 のまま、`tick(30000)` で 2。`millis()` の一周（`tick(0xFFFFFFF0)` の後 `tick(0x00007520)`、差 30000）でも 30 秒経過として読む。読み取り失敗（`valid=false`）が `lastClimate()` に反映される。
- F1：運転中（power=true）の `applyAc()`、または power を含むパッチ（`AcPatch::power.has_value()`。`power:true`／`power:false` のどちらも）の `applyAc()` 1 回で `acCount` がちょうど 1 増え、`lastAc` が状態一式（変更していない項目も含む）になる。
- F1（停止中の設定変更）：停止中（power=false、初期状態を含む）に power を含まないパッチ（例：`temp` だけ、`mode` だけ）で `applyAc()` を呼ぶと、戻り値 true、`acCount` は 0 のまま、`acState()` は更新後の値になる。続けて `power:true` だけのパッチを送ると `acCount` が 1 増え、`lastAc` にそれまでの変更がまとめて入っている（モード別の記憶の確かめ方は D-02 のテスト観点）。
- F1（エラーと送信失敗）：検証エラー時は戻り値 false、送信 0 回、`acState()` は変わらない。`FakeIrSender::result = false` でも `applyAc()` は true を返し、`acState()` は更新後の値のまま。
- F1（風向を作らない）：`applyAc` で送った `lastAc` の `swingV`・`swingH` は常に `Off`。`POST /api/ac` に `swingV` か `swingH` を含めると 400、送信 0 回、状態は変わらない。`GET /api/status` の本文に `swingV`・`swingH` のキーが無い（test_api、詳細は D-04）。
- F3（対象外）：`POST /api/light` は、ルーティング表に無いパスのエラー（D-04 で決めた値。例 404）を返し、`FakeIrSender` の `acCount` は 0 のまま（送信 0 回）、`acState()` も変わらない。
- F1-VALUES（風向の確定値とテスト）：`ac_capabilities.h` を確定値（風向 `Off` だけ）にした後も test_ac_state がすべて通る（TC-N06・TC-N07 は IGNORE になる。T-02 で直した後の形）。
- API：`ApiRouter::handle` がルーティング表の各メソッド・パスを該当処理へ振り分ける（中身の検証は test_api、D-04）。
- N-WIFI は native では確かめない（`src/wifi_manager` にあるため。下の実機で確かめる）。

### 実機でしか確かめられないこと
- `pio run -e esp32` でビルドが通る（IRremoteESP8266・DHT20・WebServer と core の結合）。
- 電源投入・リセット直後に赤外線が出ない（スマホのカメラで LED を見る、または別の受信機で確認）。
- `GET /` でスマホに画面が出る（埋め込み HTML が壊れていない）。照明カードと風向の操作が無い。
- DHT20 が 0x38 で応答し、30 秒ごとに値が更新される。
- IO4 から `IRac`（HITACHI_AC296）で送った状態一式でエアコンが運転・停止・モード・温度・風量を受け付ける（フェーズ2／H-2）。特に、`IRac` が押したボタンのバイト（`state[11]`）を埋めないまま送っても受け付けるか。受信機で送信信号をダンプし、`state[11]` の値を記録する。
- 自動モード（温度指定なし）で送ったときにエアコンが受け付けるか（H-2）。
- N-WIFI（`src/wifi_manager`、6a 節の表）。シリアルに試行回数と時刻を出して確かめる：
  - 運用中にルーターの電源を切ると、約10秒ごとに再接続を試み、3回目から約10秒後（切断から約30秒）に再起動する。
  - 3回以内にルーターを戻すと、再起動せずに再接続し、画面が再び開ける。その後もう一度切ると、また1回目から数える。
  - ルーターを切ったまま起動すると、約30秒で再起動を繰り返す。ルーターを戻すと接続でき、画面が開ける。
  - 再起動後に赤外線が出ない（N-BOOT）。
  - 自前の再接続と core 2.x の自動再接続がぶつからない（D-06 の決め方に従う）。
- `loop()` が長く止まらず、画面のボタンから送信まで 1 秒以内に収まる（N-RESP、D-06 で詳細）。`WifiManager::tick` もブロックしない。

---

## 要件への疑問

1. **DHT20 のライブラリが要件に書かれていない。** 推測：RobTillaart の DHT20 ライブラリ（`DHT20(TwoWire*)`、`begin()`、`read()`、`getTemperature()`、`getHumidity()`）を使うとした。PlatformIO の登録名と版は要確認（推測：`robtillaart/DHT20`）。別ライブラリにする場合も影響は `src/climate_dht20.*` と `platformio.ini` だけ。
2. **N-BOOT の状態が原本と req-index で違う。** 原本は「決定案」、req-index は「仮」。req-index に従い仮として扱った。推測：「NTP 取得直後に、過ぎた時刻のスケジュールを遡って実行しない」ことも N-BOOT の意図に含まれるとした（詳細は D-03）。
3. **F5 の更新間隔。** req-index では F5 は「決定」だが、原本では「仮に30秒」。推測：周期の値だけを仮として `kClimateIntervalMs` の1か所に置いた。推測：初回は起動直後にすぐ読む（画面が長く空にならないように）とした。
4. **インポートの送り方。** 要件は「JSONファイルの内容でスケジュール一覧を置き換える」だけ。推測：画面の JS がファイルを読んで JSON 本文として送る（`WebServer::arg("plain")` で受けられ、`ApiRequest::body` の形がそろう）とした。確定は D-04／D-05。
5. **受信ピン IO14 の本体ファームでの扱い。** 要件はピン割り当てに IO14 を挙げるが、本体の機能で受信を使うものはない（純正リモコンとの同期は「やらないこと」）。推測：本体ファームでは IO14 を初期化せず、フェーズ1のダンプ環境だけが使うとした。
6. **N-WIFI の「計30秒程度」の割り振りと、起動時の最初の接続。** 推測：1回の試行を 10 秒待ち（`kWifiAttemptTimeoutMs = 10000`）、3回とも失敗したら再起動とした。推測：起動時に一度もつながらない場合も同じ扱い（最初の接続を1回目と数え、約30秒で再起動）とした。判断を src に置き native テストをしないことは人の判断（案A）による。
7. **赤外線送信の失敗の扱い。** 要件に定めがない。推測：`IIrSender::sendAc` が false を返しても `Hub` は状態を戻さず、`applyAc` の戻り値にも含めないとした。API の応答にどう出すかは D-04。
8. **原本 v0.4 に風向・旧プロトコルの記述が残っている。** 4章の画面構成「エアコンカード：運転／停止、モード、温度、風量、風向」、5章の `POST /api/ac` の例の `swingV`・`swingH`、F2 の表の「風向：機種の標準」、8章・9章の「仮：HITACHI_AC424」。推測：D1 の結果（風向は作らない、HITACHI_AC296）と F1 の表を優先し、画面・API に風向を出さないとした。原本の直しは人が行う。
9. **API で風向を「受け付けない」ときの応答。** 要件に定めがない。推測：`swingV`・`swingH` を含む `POST /api/ac` は 400（`{"error":"unknown field: swingV"}` の形）とし、黙って無視はしないとした。文言と、ほかの未知のキーの扱いは D-04 で決める。
10. **静音の風量の対応先。** D1 の風量は「自動・静音・弱・中・強」だが、D-02 の `AcFan`（`stdAc::fanspeed_t` の写し）に `Quiet` は無い。推測：インターフェースを変えずに、静音を既存の `AcFan::Min` に当てるとした（API の文字列は D-02／D-04）。`AcFan::Min`→`stdAc::fanspeed_t::kMin`→`kHitachiAc296FanSilent` になることは取得済みのライブラリの `IRHitachiAc296::convertFan` で確かめた（7節）。残る疑問は、`AcFan` の名前（Min）と要件の言葉（静音）がずれていることだけ。
11. **req-index の F1-VALUES の rule が古い。** status は「決定」だが rule は「仮値で置き PENDING を付ける」のまま。推測：human_feedback に従い、I-06 で PENDING を外して確定値にするとした。
12. **自動モードの F2 初期値。** D1 で「自動は温度指定なし」と決まったが、F2 の表は「自動：温度＝機種の標準」。推測：自動では温度を送らない（`ac_capabilities.h` の温度指定可否を false）とし、F2 の自動の温度は使われない値になるとした。詳細は D-02。なお送信側は、`IRHitachiAc296::setTemp` が自動モードのとき温度を `kHitachiAc296TempAuto` に置き換えるため（7節）、`AcState::temp` にどの値が残っていても自動では温度が送られない。
