# IRハブ（ESP32 赤外線リモコンハブ）

ESP32 でエアコンと照明を赤外線で操作する自作ハブ。設計からテストまでを「ループ」で進める。

## 正とするもの
- 要件の原本：`docs/requirements.md`（人の文書。Claude は編集しない）
- 要件ID と状態：`docs/req-index.json`（**決定**＝確定／**仮**＝進めてよいが変わりうる／**未決**＝作らない）
- 作業項目と進み具合：`loop/state.json`（変更は `python scripts/state.py` だけ）

## ループの仕組み
- 1周＝`/loop-step`：次の項目を選ぶ → 担当サブエージェントが作る → `scripts/verify.py` が機械検査 → `reviewer` が審査 → state.py が状態更新・コミット
- 作る担当と確かめる担当は別。メインは段取りだけで、成果物を書かない
- 実機の確認と未決事項の確定は人が行う（`/hw-gate`、`/decide`）。ループは人の対応待ちになったら止まる
- 人への連絡は `loop/inbox.md`、履歴は `loop/log.md`、審査記録は `loop/reviews/`

## サブエージェント
| 名前 | 作るもの | 書ける場所 |
|---|---|---|
| designer | 設計書 | docs/design, docs/ops |
| test-designer | テスト計画、Unity テスト | docs/test, test |
| implementer | 実装 | lib, src, web, tools, platformio.ini |
| reviewer | 審査結果 | loop/reviews |

書ける場所は `.claude/hooks/guard.py` が強制する。

## コマンド
- 単体テスト：`pio test -e native`
- 本体ビルド：`pio run -e esp32`
- ESP32 への書き込み（upload）は人だけが行う

## 守ること
- 要件の「やらないこと」を作らない。未決の要件を作らない
- 実在を確かめていないライブラリ API を使わない・書かない
- コードの決まりは `.claude/skills/irhub-conventions/SKILL.md`
