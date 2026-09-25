---
name: loop-step
description: IRハブのループを1周回す。次の作業項目を選び、担当サブエージェントに作らせ、機械検査と審査を通し、状態を更新する。/goal や scripts/run_loop.py から繰り返し呼ばれる。
allowed-tools: Bash(python scripts/state.py *) Bash(python scripts/verify.py *) Bash(git status*) Bash(git diff*)
---

# ループ1周

あなた（メイン）は**段取りだけ**を行う。設計書・テスト・コードを自分で書かない、直さない。成果物を作るのはサブエージェント、合否を決めるのは verify.py と reviewer。1周で扱う項目は1つだけ。

## 手順

### 0. 前回の後始末
`python scripts/state.py summary` を実行する。「作業中のまま」がある場合、その項目は前回の周回が途中で終わっている。`python scripts/state.py fail <ID> --reason "前回の周回が中断"` で戻してから 1 へ進む。

### 1. 次の項目を選ぶ
`python scripts/state.py next`

- `"result": "ITEM"` → 2 へ
- `"result": "HUMAN"` → loop/inbox.md の要点を3行で報告して**この周回を終える**（人の対応待ち。ここで止まるのが正しい動作）
- `"result": "NONE"` → 「全項目完了」と報告して終える

### 2. 着手
`python scripts/state.py start <ID>` の出力（JSON）が作業指示。以降これを **brief** と呼ぶ。エラーが出たら内容をそのまま報告して終える（未コミットの変更がある等は人の判断が要る）。

### 3. 作らせる
Agent ツールで brief の `agent`（designer / test-designer / implementer）を呼ぶ。プロンプトには次をそのまま渡す：
- brief の JSON 全文
- `human_feedback` があれば、それ（`python scripts/state.py show <ID>` で見られる）
- 「作業項目1つ分だけを行い、終わったら短く報告して終わる」

サブエージェントの報告の1行目が `TEST-DEFECT:` なら、`python scripts/state.py fail <ID> --reason "<報告1行目>" --escalate` を実行して終える。

### 4. 機械検査
`python scripts/verify.py <ID>`

- 終了コード 1（不合格）→ `python scripts/state.py fail <ID> --reason "verify: <NG の項目名>"` を実行して終える。**自分で直さない。** 次の周回で同じ担当が loop/verify/<ID>.md を読んで直す
- 終了コード 0 → 5 へ

### 5. 審査
Agent ツールで reviewer を呼ぶ。プロンプトに渡すもの：
- brief の JSON 全文
- base_commit（`python scripts/state.py show <ID>` の base_commit）
- 「loop/reviews/<ID>-a<attempts>.md に判定を書く」

### 6. 判定を反映
`python scripts/state.py review <ID>`
- PASS なら done になり自動でコミットされる
- REJECT なら todo（上限到達なら human）に戻る

### 7. 報告
次の形式で3行以内：
```
<ID> <title>: PASS|REJECT|VERIFY-FAIL|ESCALATE（attempt N）
理由：<1行>
次：<state.py next の結果の見込み、または人の対応待ち>
```

## やってはいけないこと
- 成果物を自分で書く・直す（検証の独立が崩れる）
- verify や review の結果に反して状態を進める
- 1周で2項目以上に着手する
- loop/state.json を直接編集する（フックで止まる。必ず state.py）
