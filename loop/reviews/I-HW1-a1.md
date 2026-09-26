VERDICT: PASS
## 指摘
1. [推奨] platformio.ini「env:dump」: `crankyoldgit/IRremoteESP8266@^2.8.6` が env:esp32 と重複している → 版を揃えたままにするなら `[common]` に `lib_deps_ir` として1か所にまとめる（今は両方とも 2.9.0 に解決されるので実害は無い）
## 確かめたこと
- verify: PASS（scope 2 件、pio_build:dump exit 0）
- 変更範囲: platformio.ini（env:dump 追加とコメント1行）と新規 tools/phase1_dump/main.cpp だけ。allowed_paths の中に収まっている（loop/log.md、loop/state.json はループ側の更新）
- brief「env:dump を足す」: platform espressif32@^6.9.0（core 2.x、C-TECH どおり）、board esp32dev、monitor_speed 115200
- brief「tools/phase1_dump/ のソースでビルド」: `build_src_filter = -<*> +<../tools/phase1_dump/>` で src/ を外している。firmware.elf に `(anonymous namespace)::irrecv` と setup()/loop() があり、phase1_dump/main.cpp が実際にリンクされていることを確かめた
- brief「IRrecvDumpV2 相当」: 受信ピン 14（HW-PINS、要件6章「IRrecvDumpV2 の初期値と同じ」）、115200bps、バッファ 1024、タイムアウト 50ms、UNKNOWN の閾値 12、kTolerance、resultToHumanReadableBasic／IRAcUtils::resultAcToString／resultToSourceCode を出す。サンプルと同じ構成で、TC-H02〜H07 に必要な出力（プロトコル名、Mode・Temp、生データ）が出る
- brief「env:esp32 と env:native を壊さない」: env:esp32 と env:native の節は変更なし。`pio run -e esp32` SUCCESS、`pio test -e native` 35 件中 32 成功・3 skip・失敗 0
- 設計 01-architecture.md 7章「ピン番号を書いてよいのは src/pins.h と tools/phase1_dump/ だけ」「IO14 は env:dump だけが使う」と一致。9章の配置 tools/phase1_dump/ と一致
- lib/core に Arduino 依存を入れていない（lib/core は未変更。env:dump は `lib_ignore = core` で core を取り込まない）
- 赤外線を送らない: IRsend を含まず受信のみ（N-BOOT に反しない）
- メモリ: 捕捉バッファ 1024×uint16 の2面（save_buffer=true）で約 4KB。RAM 使用 6.7%。無駄な構造なし
- 実在しない API: ビルドが通っており、使っている API（IRrecv(pin,size,timeout,save)、setUnknownThreshold、setTolerance、enableIRIn、decode、D_STR_* マクロ）はすべて IRremoteESP8266 2.9.0 に存在する
- 未決の要件を作っていない: プロトコルや温度範囲を決め打ちしていない（受信して表示するだけ）
- 設計書に無い振る舞いの追加: なし
- 再挑戦ではない（attempts=1、過去レビューなし）
