VERDICT: REJECT
## 指摘
1. [必須] docs/design/01-architecture.md「1. 層の分け方」原則と「7. src/ 側のモジュール」`src/wifi_manager` の記述が食い違っている。原則は「src/ は core のインターフェースを実装するか core の関数を呼ぶだけで、周期の判断などの業務ルールを持たない」とする。一方で wifi_manager は src にあり、`wifi.tick(millis())` で N-WIFI の再接続3回・約30秒・失敗で再起動を自分で判断する。このままでは、再接続の判断を core に置いて native でテストするのか、src に置いて実機だけで確かめるのかを、D-06・T-01（N-WIFI を含む）・I-05 が推測で決めることになる。→ どちらかに決めて明記する。案A：core に純粋な判断クラス（例 `WifiReconnectPolicy::tick(nowMs, connected) → {None, Reconnect, Restart}`）と、必要なら `IWifi` のようなポートを置き、wifi_manager は実行するだけにする（モジュール一覧・依存図・ファイル配置・テスト観点に追加）。案B：N-WIFI の判断は例外として src に置き、実機でだけ確かめると原則に書き足し、テスト観点の「実機でしか確かめられないこと」に N-WIFI を加える。
2. [推奨] 「4. 境界のインターフェース」の `light.h` で `enum class LightButton : uint8_t` を使っているのに `<cstdint>` を include していない（`api.h` の `HttpMethod : uint8_t` も、hub.h を経由した間接 include 頼み）。→ `#include <cstdint>` を足す。
3. [推奨] 「7. src/ 側」の DHT20 の行で `int8_t read()` を「実在を確認済み」としているが、RobTillaart DHT20 の `read()` は `int` を返す版がある。→ 戻り値の型は「要確認」とし、`DHT20_OK`（0）との比較だけに依存すると書く。
4. [推奨] 「11. 画面の組み込み」で `send(200, text/html, kIndexHtml)` と書いている。このままでは HTML 全体がいったん Arduino の `String` にコピーされ、ヒープを HTML の大きさ分使う。→ D-05 に任せる前提で、`send_P(code, type, content)`（または長さ指定版）でコピーを避けると一言足す。
5. [推奨] 「5. Hub」の `applyAc` の戻り値が、検証に通ったことを表すのか、`sendAc` が true だったことまで含むのかが書かれていない。`sendAc` が false のときに状態を更新済みのまま残すかも書かれていない。→ 意味を1行で決めるか、D-02／D-04 に送ると明記する。
6. [推奨] 「2. 依存方向」の図に、`ApiRouter` を作る `main.cpp → api` の矢印が無い（規則4では web_bridge は ApiRouter だけを知る）。また「依存の規則（verify・レビューで確かめる）」とあるが、scripts/verify.py は include の規則を検査していない。→ 矢印を足し、「native ビルドとレビューで確かめる」に直す。
## 確かめたこと
- verify: PASS（loop/verify/D-01.md、scope/protected/pending/design_doc/design_tags すべて OK）
- 差分：成果物は新規の docs/design/01-architecture.md だけ（loop/state.json・loop/log.md はループの管理ファイル）
- 見出し：6つの必須見出しがそろい、reqs の10件（F1, F3, F4, F5, N-STATE, N-BOOT, UI, API, HW-PINS, C-TECH）がすべて「対象要件」に並んでいる
- brief：lib/core と src の境界、赤外線・時計・センサーのインターフェース（ports.h のシグネチャ）、native テスト用のフェイクの形、モジュール一覧と責務、依存図（mermaid）、ファイル配置のすべてがある。境界の一部（N-WIFI）に食い違いがある（指摘1）
- やらないこと：永続化、OTA、クラウド、自動運転、照明状態の検出、同期、本体タイマーを12章で構成上禁止している。反する設計は無い
- 未決：F1-TIMER は作らない。F1-VALUES は ac_capabilities.h に置き場所だけ決め、値は書いていない。F2・F5 の周期は `// 仮(...)` で1か所に置いている
- N-BOOT：送信経路を IIrSender の2メソッドに限り、呼んでよい場所を3か所に限っている。コンストラクタ・setup・begin で送らない。NTP 取得後に過ぎた時刻を遡って実行しない（疑問2に明記）
- 既存と矛盾しないか：platformio.ini（espressif32@^6.9.0、ArduinoJson ^7.2.0、IRremoteESP8266 ^2.8.6、native unity）、core_version.h、test_smoke、.gitignore（src/generated/、include/secrets.h）、irhub-conventions、state.json の後続項目の allowed_paths（test/test_<name>/**、tools/phase1_dump/**、src/generated/**）と整合している
- ライブラリ API：IRac(pin)、IRac::sendAc(state_t)、stdAc::state_t::protocol、IRsend::begin/sendRaw、WebServer の on/onNotFound/arg("plain")/sendHeader/send/handleClient、configTzTime/getLocalTime はいずれも実在する。DHT20 は登録名と版を要確認としているが、read の戻り値の型は要確認にしていない（指摘3）
- 推測の明記：6件を「要件への疑問」に「推測：」付きで書いている。スレッド前提（単一 loop）には理由が書いてある
- テスト観点：native と実機を分けている。F5 の周期は境界値（29999／30000）と millis 一周（差は 30016）を具体値で書いている
- 過去レビュー：初回（attempts=1）のため無し
