---
name: irhub-conventions
description: IRハブのコードの決まり（ディレクトリの役割、lib/core と src の境界、テスト、ライブラリ、ESP32 の制約）。コードを書く・読むときに使う。
user-invocable: false
---

# IRハブのコードの決まり

## ディレクトリ
| 場所 | 中身 | 依存してよいもの |
|---|---|---|
| `lib/core/src/` | 状態モデル、スケジュール、API ハンドラ、JSON 検証。純 C++17 | 標準ライブラリ、ArduinoJson のみ。`Arduino.h` 禁止 |
| `src/` | main、WiFi、NTP、WebServer、DHT20、IRremoteESP8266 との橋渡し | Arduino core 2.x、各ライブラリ、lib/core |
| `src/generated/` | web/index.html を埋め込んだヘッダ（tools/embed_html.py が生成） | 手で編集しない |
| `web/index.html` | 画面の唯一のソース。HTML+CSS+JS 1ファイル | 外部 URL・CDN 禁止 |
| `test/test_<name>/` | Unity の native テスト | lib/core とテスト内のフェイク |
| `tools/` | 埋め込みスクリプト、PC 用モックサーバー、フェーズ1のダンプ | Python 標準ライブラリのみ |
| `include/secrets.h` | SSID/パスワード。Git 管理外 | — |

## 環境
- `env:esp32`：本体（espressif32 6.x = Arduino core 2.x、board esp32dev、115200bps）。テストは実行しない
- `env:native`：単体テスト。PC の gcc でビルド
- `env:dump`：フェーズ1用の受信ダンプ（I-HW1 で追加）

## 境界の作り方
- 赤外線送信、時計、センサーは lib/core 側に抽象クラス（純粋仮想）を置き、src/ で実装、テストはフェイク
- API ハンドラは `(method, path, body) → (status, body)` の HTTP 非依存関数。WebServer は src/ で薄く橋渡し
- 時刻を使うロジックは現在時刻を引数で受け取る（`millis()` や `time()` を lib/core で直接呼ばない）

## 値と未決
- 受信結果待ちの値（温度範囲・風量・風向の選択肢）は `lib/core/src/ac_capabilities.h` だけに置き、`// PENDING(F1-VALUES)` を付ける
- 仮の初期値（F2 の暖房・除湿・自動）は1か所の定数表に置き、`// 仮(F2)` を付ける
- 本体タイマー（F1-TIMER）は作らない

## ESP32 の制約
- RAM は限られる。巨大な固定長配列や String の連結を繰り返さない。スケジュールは上限件数の固定配列でよい
- `delay()` で長く止めない。loop() は周期処理を millis() で回す
- 起動・再起動時に赤外線を送らない（N-BOOT）
- ArduinoJson は v7 の `JsonDocument`（サイズ指定なし）を使う

## テスト
- 各テストの直前に `// TC: TC-Nxx` と `// REQ: <要件ID>` を書く
- 実行：`pio test -e native`（特定のみ `-f test_ac_state`）
- 本体ビルド：`pio run -e esp32`
