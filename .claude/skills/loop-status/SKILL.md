---
name: loop-status
description: IRハブのループの進み具合、人の対応待ち、直近の失敗を一覧にする。
disable-model-invocation: true
allowed-tools: Bash(python scripts/state.py *) Bash(git log*) Read
---

1. `python scripts/state.py summary` を実行
2. `loop/inbox.md` を読む（無ければ `python scripts/state.py next` で作られる。ただし next は状態を変えない）
3. `loop/log.md` の末尾20行を読む
4. 次の形でまとめる：
   - 完了 N / 全 M 項目
   - 人の対応待ち：項目ごとに1行（何をすればよいか）
   - 直近の失敗：RETRY / ESCALATE の項目と理由
   - 次に自動で進む項目
