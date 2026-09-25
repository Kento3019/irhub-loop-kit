---
name: hw-gate
description: 人間ゲート（実機確認・設計承認）や、ループが止めた項目の結果を記録する。例：/hw-gate H-0 pass、/hw-gate H-DESIGN fail
disable-model-invocation: true
argument-hint: <ID> [pass|fail]
arguments: [id, result]
allowed-tools: Bash(python scripts/state.py *) Read
---

対象：$id　結果：$result

1. `python scripts/state.py show $id` で項目を表示し、`how`（手順）と deps を確認する
2. 結果が指定されていなければ、手順を人に示し、pass / fail とメモを聞く。人が確認していないことを pass にしない
3. 実機の項目なら、docs/test/test-plan.md の該当 TC-H を読み、各ケースの結果を人に確認する
4. fail のとき：原因を人に聞き、差し戻す項目があれば決める（例：H-DESIGN で 02 の設計を直したい → D-02 を差し戻し。差し戻すと、その項目に依存する後続も作り直しになるので人に確認する）
5. 記録：
   - pass：`python scripts/state.py resolve $id pass --note "<人のメモ>"`
   - fail：`python scripts/state.py resolve $id fail --note "<原因と直してほしい点>" --reopen <差し戻す項目ID ...>`
   - ループが止めた自動項目（status=human）を再挑戦させる：原因を人と直してコミットしてから `resolve <ID> pass --note ...`
6. H-1 の場合は、docs/hw/phase1-capture.md に受信結果が貼られていることを確認し、続けて /decide D1 を案内する
7. 結果を2行で報告し、ループを再開できるか（`python scripts/state.py next`）を伝える
