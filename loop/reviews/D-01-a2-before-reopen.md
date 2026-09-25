VERDICT: REJECT
## 指摘
1. [必須] docs/design/01-architecture.md の「3. モジュール一覧」「6a」「9. ファイル配置」と loop/state.json の作業項目が合っていない。新しく足した `lib/core/src/wifi_policy.h/.cpp` と `test/test_wifi_policy/test_main.cpp` を作れる項目がどこにも無い。N-WIFI を持つ実装項目は I-05 だけだが、書ける場所は `src/**`・`include/secrets.h.example`・`platformio.ini` で、lib/core に書けない。lib/core/** に書ける I-01・I-02・I-03 は reqs に N-WIFI を持たず、brief も別のテスト（test_ac_state／test_schedule／test_api）を通すことだけ。`test/test_wifi_policy/**` に書けるテスト項目も無い。このままだと、I-05 はフックに止められるか、wifi_policy を作らずに src で判断することになる（原則に反する）。test_wifi_policy はいつまでも作られない。→ 「要件への疑問」か「仮・未決の扱い」に1項目足す。そこに、`wifi_policy.*` と `test_wifi_policy` を作る作業項目が今の計画に無いことを書く。どう対応するかも案を書く。例：I-05 の allowed_paths に `lib/core/src/wifi_policy.*` を足し、`test/test_wifi_policy/**` を書くテスト項目（T-05 など）を I-05 の前に足す。state.json はメインが state.py で直す。設計書にこの2点を書き、メインが気付けるようにする。
2. [推奨] 「6a」の `kWifiAttemptTimeoutMs = 10000` は推測で置いた値なのに、コメントが `// 推測：…` になっている。決まり（SKILL.md「値と未決」）の `// 仮(ID)` の形と違う。→ `// 仮(N-WIFI)：「計30秒程度」を 3回×10秒 とした` にそろえ、grep で見つけられるようにする。
3. [推奨] 「6a」の wifi_manager の例で、Reconnect のたびに `WiFi.disconnect(); WiFi.begin()` を呼んでいる。ESP32 Arduino core 2.x の WiFi は自動再接続が既定で有効なので、自前の再接続とぶつかるおそれがある。→ D-06 で `WiFi.setAutoReconnect(false)`（または自動再接続に任せるか）を決めることを、「D-06（呼び方）」の申し送りに一言足す。

## 確かめたこと
- verify: PASS（loop/verify/D-01.md、scope・protected・pending・design_doc・design_tags すべて OK）
- 差分：成果物は新規の docs/design/01-architecture.md だけ。loop/state.json・loop/log.md・loop/inbox.md・loop/reviews/D-01-a1.md はループの管理ファイル
- 前回の指摘1（N-WIFI の判断をどこに置くか）：案A で直っている。原則に「例外は置かない」と書き、判断は core の `WifiReconnectPolicy`、実行は `src/wifi_manager`。モジュール一覧・依存図（WIFI→WP、T→WP）・規則2と4・ファイル配置・テスト観点（native と実機の両方）に反映済み。ただし作る作業項目が無い（指摘1）
- 前回の指摘2：`light.h`・`api.h` に `#include <cstdint>` を足した。直っている
- 前回の指摘3：DHT20 `read()` の戻り値の型を「要確認」にし、`DHT20_OK` との比較だけに頼ると書いた。直っている
- 前回の指摘4：11章と7章で `send_P` を使い、`String` へのコピーを避けると書いた。長さ指定版の型は D-05 で確かめる（要確認）とした。直っている
- 前回の指摘5：`applyAc` の戻り値を「検証に通ったか」に決めた。sendAc が false でも状態は戻さない。疑問8とテスト観点（F1）にも反映した。直っている
- 前回の指摘6：`MAIN → API` の矢印を足し、依存の規則の見出しを「native ビルドとレビューで確かめる。verify.py は include を検査しない」に直した。直っている
- WifiReconnectPolicy の状態遷移を、表とテスト観点の具体値で順に追って確かめた。1000→Reconnect(1)、10999→None、11000→Reconnect(2)、21000→Reconnect(3)、30999→None、31000→Restart で表と一致する。start(0)→10000→20000→30000 で Restart。millis の一周は 0xFFFFF000→0x00001710 で、差 0x2710=10000 になる
- F5 の一周のテスト値：0xFFFFFFF0→0x00007520 の差は 0x7530=30000 で正しい
- やらないこと・未決：12章は前回と同じ。F1-TIMER は作らない。F1-VALUES は置き場所だけ決めた。F2・F5 の仮の値は1か所ずつに置いている
- N-BOOT：10章の構造上の決まりは前回と同じ。wifi_manager が Restart した後も N-BOOT の実機確認の観点に入っている
- ライブラリ API：WiFi.config/begin/disconnect/status、WL_CONNECTED、ESP.restart、WebServer::send_P はいずれも ESP32 Arduino core 2.x に実在する
- brief：lib/core と src の境界、赤外線・時計・センサーのインターフェース、フェイク、モジュール一覧、依存図、ファイル配置がそろっている
