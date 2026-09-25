#!/usr/bin/env python3
"""PreToolUse フック。人の注意に頼らず、構造で止める。

- 守るファイル（要件・状態・ハーネス・秘密情報）への Edit/Write を止める
- サブエージェントごとに書き込める場所を制限する（agent_type で判定）
- 危険な Bash（push、書き込み、強制リセット、ネットワーク）を止める
- サブエージェントからの state.py と git commit を止める（状態遷移はメインだけが行う）

ハーネス自体を人が直したいときは、環境変数 LOOP_HARNESS_EDIT=1 を付けて claude を起動する。
exit 2 = ブロック（stderr が Claude に返る）
"""
import json
import os
import re
import sys
from fnmatch import fnmatch
from pathlib import Path

PROTECTED = [
    "docs/requirements.md", "docs/req-index.json", "loop/state.json", "include/secrets.h",
    "scripts/**", ".claude/**", "CLAUDE.md",
]
# サブエージェント名 → 書き込んでよい場所
AGENT_WRITE = {
    "designer": ["docs/design/**", "docs/ops/**"],
    "test-designer": ["docs/test/**", "test/**"],
    "implementer": ["lib/**", "src/**", "web/**", "tools/**", "platformio.ini", "include/secrets.h.example"],
    "reviewer": ["loop/reviews/**"],
}
BASH_DENY = [
    (r"\bgit\s+push\b", "git push はループからは行わない"),
    (r"\bgit\s+(reset\s+--hard|clean\s+-[a-z]*f|checkout\s+--\s|restore\s)", "作業の破棄は人が行う"),
    (r"(-t|--target)\s*=?\s*(upload|uploadfs)\b|\bpio\s+run\b.*\bupload\b", "ESP32 への書き込みは人が行う"),
    (r"\brm\s+-[a-z]*r", "再帰削除は禁止"),
    (r"\b(curl|wget|Invoke-WebRequest|iwr)\b", "ネットワークアクセスは禁止"),
    (r"\bpio\s+(pkg\s+update|upgrade)\b|\bplatformio\s+upgrade\b", "ツールの更新は人が行う"),
    (r">\s*\S*(loop/state\.json|req-index\.json|requirements\.md|secrets\.h\b)", "守るファイルへのリダイレクトは禁止"),
    (r"\b(sed|perl)\s+-i\b", "sed -i / perl -i は使わず Edit を使う"),
]


def root() -> Path:
    return Path(os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()).resolve()


def rel(path: str, cwd: str) -> str:
    p = Path(path)
    if not p.is_absolute():
        p = Path(cwd) / p
    try:
        return str(p.resolve().relative_to(root())).replace("\\", "/")
    except ValueError:
        return "../" + str(p)  # プロジェクト外


def match(path: str, globs) -> bool:
    for g in globs:
        if g.endswith("/**") and (path == g[:-3] or path.startswith(g[:-2])):
            return True
        if fnmatch(path, g):
            return True
    return False


def block(msg: str) -> None:
    print(f"[guard] {msg}", file=sys.stderr)
    sys.exit(2)


def main() -> None:
    data = json.load(sys.stdin)
    tool = data.get("tool_name", "")
    ti = data.get("tool_input") or {}
    agent = data.get("agent_type")  # サブエージェント内のときだけ入る
    cwd = data.get("cwd") or str(root())
    harness_edit = os.environ.get("LOOP_HARNESS_EDIT") == "1"

    if tool in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
        path = ti.get("file_path") or ti.get("notebook_path") or ""
        r = rel(path, cwd)
        if r.startswith("../"):
            block(f"プロジェクト外への書き込みは禁止: {path}")
        if match(r, PROTECTED) and not harness_edit:
            block(f"{r} は守るファイル。状態は scripts/state.py、要件は人が変更する")
        if agent in AGENT_WRITE and not match(r, AGENT_WRITE[agent]):
            block(f"{agent} は {r} に書けない。書ける場所: {AGENT_WRITE[agent]}。範囲外の変更が必要なら報告して止まる")
        sys.exit(0)

    if tool == "Bash":
        cmd = ti.get("command", "")
        for pat, why in BASH_DENY:
            if re.search(pat, cmd, re.I):
                block(f"{why}: {cmd}")
        if agent and re.search(r"state\.py", cmd):
            block("state.py はメインのループだけが実行する。結果を報告して終わる")
        if agent and re.search(r"\bgit\s+(commit|add|stash|merge|rebase)\b", cmd):
            block("サブエージェントは git の書き込み操作をしない")
        sys.exit(0)

    sys.exit(0)


if __name__ == "__main__":
    main()
