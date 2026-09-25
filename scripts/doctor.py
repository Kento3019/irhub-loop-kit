#!/usr/bin/env python3
"""ループを回す前の環境チェック。python scripts/doctor.py"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify import find_pio  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def ver(cmd: list[str]) -> str:
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
        return (r.stdout or r.stderr).strip().splitlines()[0] if (r.stdout or r.stderr) else f"exit {r.returncode}"
    except Exception as e:  # noqa: BLE001
        return f"エラー: {e}"


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    ok = True
    print(f"python   : {sys.version.split()[0]}（{sys.executable}）")
    for name, cmd, hint in [
        ("git", ["git", "--version"], "Git for Windows を入れる"),
        ("claude", ["claude", "--version"], "Claude Code CLI を入れる。/goal は v2.1.139 以降"),
        ("g++", ["g++", "--version"], "native テストに必要。Windows なら MSYS2 の mingw-w64 gcc を入れて PATH に追加（WSL で回すなら不要）"),
    ]:
        if shutil.which(cmd[0]):
            print(f"{name:9}: {ver(cmd)}")
        else:
            print(f"{name:9}: 無い → {hint}")
            ok = False
    pio = find_pio()
    if pio:
        print(f"pio      : {ver(pio + ['--version'])}（{pio[0]}）")
    else:
        print("pio      : 無い → VS Code の PlatformIO 拡張を入れ、~/.platformio/penv/Scripts を PATH に追加")
        ok = False
    if not (ROOT / ".git").exists():
        print("git repo : 無い → git init && git add -A && git commit -m init")
        ok = False
    elif subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True).returncode != 0:
        print("git repo : コミットが無い → git add -A && git commit -m init")
        ok = False
    else:
        print("git repo : OK")
    if shutil.which("python") is None:
        print("python   : 'python' コマンドが PATH に無い → フックが動かない。settings.json の command を py か python3 に変える")
        ok = False
    print("\n結果:", "OK" if ok else "上の『無い』を解消する")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
