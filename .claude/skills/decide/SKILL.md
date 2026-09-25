---
name: decide
description: 要件の未決事項（D1 プロトコル・値・タイマー、D2 初期値）を人の判断で確定し、req-index に反映する。
disable-model-invocation: true
argument-hint: <D1|D2>
arguments: [id]
allowed-tools: Bash(python scripts/state.py *) Read
---

対象：$id

1. `docs/req-index.json` の decisions から $id を読み、affects の要件を確認する
2. 根拠を読む：D1 なら `docs/hw/phase1-capture.md`（受信ダンプ）、D2 なら H-2 のメモ（loop/log.md）
3. 受信結果から読み取れることを人に示す。D1 なら次を表にする：
   - 検出されたプロトコル名（HITACHI_AC424 か、別のものか、UNKNOWN か）
   - 温度の下限・上限、風量の段階、風向（上下・左右）の選択肢
   - タイマー入切の信号が出たか
   読み取れないものは「不明」とし、推測で埋めない
4. 人に確定内容を確認する。**決めるのは人。** 提案はしてよいが、人の返事なしに進めない
5. 記録：
   `python scripts/state.py decide $id --note "<確定内容を1〜3文で>" --set <REQ>=<決定|仮|未決|対象外> ...`
   例（D1、タイマー非対応だった場合）：
   `--set PROTO=決定 F1-VALUES=決定 F1-TIMER=対象外`
6. docs/requirements.md は人の文書なので書き換えない。「requirements.md の F1 表と 9章を更新してください」と人に伝える（版を上げるかは人が決める）
7. 確定した値が既存の設計書と食い違う場合は、差し戻すべき項目（例：D-02、I-06）を挙げ、/hw-gate か `state.py reopen` での差し戻しを提案する
