#!/usr/bin/env python3
"""作業項目の決定的な検証。LLM を使わない。

  python scripts/verify.py <ID>

常に行う検査:
  scope      base_commit からの変更が、項目の allowed_paths の中だけか（loop/ は除外）
  protected  守るファイル（要件・状態・ハーネス・秘密情報）が変わっていないか
  pending    status=未決 の要件IDが、allowed_in 以外のコードに出てこないか
項目の checks に書かれた検査:
  design_doc / test_plan / exists / req_tags / test_count / pio_test / pio_build / ui

結果は loop/verify/<ID>.json（機械用）と loop/verify/<ID>.md（人とレビュー担当用）に書く。
終了コード 0=合格 1=不合格 2=使い方の誤り
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from state import ROOT, STATE, REQ_INDEX, VERIFY_DIR, git, in_globs, item_of, load  # noqa: E402

PROTECTED = [
    "docs/requirements.md", "docs/req-index.json", "loop/state.json", "include/secrets.h",
    "scripts/**", ".claude/**", "CLAUDE.md",
]
CODE_ROOTS = ["src", "lib", "web", "test", "tools", "include"]
CODE_EXT = {".c", ".cpp", ".h", ".hpp", ".ino", ".html", ".js", ".css", ".py", ".ini"}

DESIGN_HEADINGS = ["## 対象要件", "## 設計", "## 仮・未決の扱い", "## テスト観点", "## 要件への疑問"]
TEST_PLAN_HEADINGS = ["## 方針", "## テストケース（native）", "## テストケース（実機）", "## 要件カバレッジ"]
API_PATHS = {"/api/status", "/api/ac", "/api/light", "/api/schedules", "/api/schedules/export", "/api/schedules/import"}


class Result:
    def __init__(self) -> None:
        self.rows: list[tuple[str, bool, str]] = []

    def add(self, name: str, ok: bool, detail: str = "") -> None:
        self.rows.append((name, ok, detail))

    @property
    def ok(self) -> bool:
        return all(ok for _, ok, _ in self.rows)


def changed_files(base: str) -> list[str]:
    tracked = git("diff", "--name-only", base, check=False).splitlines()
    untracked = git("ls-files", "--others", "--exclude-standard").splitlines()
    files = sorted({f.strip().replace("\\", "/") for f in tracked + untracked if f.strip()})
    return [f for f in files if not f.startswith("loop/")]


def check_scope(item: dict, r: Result) -> None:
    files = changed_files(item["base_commit"])
    outside = [f for f in files if not in_globs(f, item["allowed_paths"])]
    prot = [f for f in files if in_globs(f, PROTECTED)]
    r.add("scope", not outside, f"変更 {len(files)} 件" + (f"。allowed_paths 外: {outside}" if outside else ""))
    r.add("protected", not prot, "" if not prot else f"守るファイルが変更された: {prot}")


def iter_code_files():
    for root in CODE_ROOTS:
        base = ROOT / root
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if p.is_file() and p.suffix in CODE_EXT and ".pio" not in p.parts:
                yield p


def check_pending(r: Result) -> None:
    req = load(REQ_INDEX)
    bad = []
    for q in req["reqs"]:
        if q["status"] != "未決":
            continue
        pat = re.compile(rf"(?<![\w-]){re.escape(q['id'])}(?![\w-])")
        for p in iter_code_files():
            rel = str(p.relative_to(ROOT)).replace("\\", "/")
            if in_globs(rel, q.get("allowed_in", [])):
                continue
            if pat.search(p.read_text(encoding="utf-8", errors="ignore")):
                bad.append(f"{q['id']} in {rel}")
    r.add("pending", not bad, "" if not bad else "未決の要件が許可外のファイルに出ている: " + ", ".join(bad))


def read(path: str) -> str | None:
    p = ROOT / path
    return p.read_text(encoding="utf-8") if p.exists() else None


def check_headings(name: str, path: str, heads: list[str], r: Result) -> str | None:
    text = read(path)
    if text is None:
        r.add(name, False, f"{path} が無い")
        return None
    missing = [h for h in heads if not re.search(rf"^{re.escape(h)}\s*$", text, re.M)]
    r.add(name, not missing, f"{path}" + (f" 見出し不足: {missing}" if missing else ""))
    return text


def check_tags_in_text(name: str, text: str, reqs: list[str], r: Result, style: str) -> None:
    if style == "md":
        missing = [q for q in reqs if f"[{q}]" not in text]
    else:
        tags = set()
        for m in re.finditer(r"REQ:\s*([\w\-, ]+)", text):
            tags.update(t.strip() for t in m.group(1).split(",") if t.strip())
        missing = [q for q in reqs if q not in tags]
    r.add(name, not missing, "" if not missing else f"要件IDのタグが無い: {missing}")


def find_pio() -> list[str] | None:
    for c in ("pio", "platformio"):
        w = shutil.which(c)
        if w:
            return [w]
    home = Path.home() / ".platformio" / "penv"
    for c in (home / "Scripts" / "pio.exe", home / "bin" / "pio"):
        if c.exists():
            return [str(c)]
    return None


def run(cmd: list[str], timeout: int = 1200) -> tuple[int, str]:
    try:
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
        return p.returncode, (p.stdout + p.stderr)
    except subprocess.TimeoutExpired:
        return 124, f"timeout {timeout}s"


def tail(s: str, n: int = 60) -> str:
    return "\n".join(s.splitlines()[-n:])


def check_item(item: dict, r: Result, logs: dict) -> None:
    pio = find_pio()
    req = load(REQ_INDEX)
    code_reqs = [q for q in item.get("reqs", []) if next((x for x in req["reqs"] if x["id"] == q), {}).get("code", True)]
    for c in item.get("checks", []):
        t = c["type"]
        if t == "design_doc":
            text = check_headings("design_doc", c["path"], DESIGN_HEADINGS, r)
            if text is not None:
                check_tags_in_text("design_tags", text, item["reqs"], r, "md")
        elif t == "test_plan":
            text = check_headings("test_plan", c["path"], TEST_PLAN_HEADINGS, r)
            if text is not None:
                check_tags_in_text("test_plan_tags", text, item["reqs"], r, "md")
                ids = re.findall(r"\bTC-[NH]\d{2,3}\b", text)
                r.add("test_plan_cases", len(set(ids)) >= 10, f"テストケースID {len(set(ids))} 件（10件以上）")
        elif t == "exists":
            miss = [p for p in c["paths"] if not (ROOT / p).exists()]
            r.add("exists", not miss, "" if not miss else f"無い: {miss}")
        elif t == "req_tags":
            files = sorted(ROOT.glob(c["glob"]))
            text = "\n".join(p.read_text(encoding="utf-8") for p in files)
            if not files:
                r.add("req_tags", False, f"{c['glob']} に一致するファイルが無い")
            else:
                check_tags_in_text("req_tags", text, code_reqs, r, "code")
        elif t == "test_count":
            files = sorted(ROOT.glob(c["glob"]))
            n = sum(len(re.findall(r"\bRUN_TEST\s*\(", p.read_text(encoding="utf-8"))) for p in files)
            r.add("test_count", n >= c["min"], f"RUN_TEST {n} 件（{c['min']}件以上）")
        elif t in ("pio_test", "pio_build"):
            if not pio:
                r.add(t, False, "pio が見つからない（PlatformIO Core を入れて PATH を通す）")
                continue
            if t == "pio_test":
                cmd = pio + ["test", "-e", c["env"]] + (["-f", c["filter"]] if c.get("filter") else [])
            else:
                cmd = pio + ["run", "-e", c["env"]]
            code, out = run(cmd)
            key = f"{t}:{c['env']}" + (f":{c['filter']}" if c.get("filter") else "")
            logs[key] = tail(out)
            r.add(key, code == 0, " ".join(cmd[1:]) + f" → exit {code}")
        elif t == "ui":
            check_ui(c["path"], r)
        else:
            r.add(t, False, f"未知の check: {t}")


def check_ui(path: str, r: Result) -> None:
    text = read(path)
    if text is None:
        r.add("ui", False, f"{path} が無い")
        return
    problems = []
    if re.search(r"<script[^>]+src\s*=", text, re.I):
        problems.append("外部 script を読んでいる（1ファイル要件）")
    if re.search(r"<link[^>]+href\s*=\s*[\"']https?:", text, re.I):
        problems.append("外部 stylesheet を読んでいる")
    if re.search(r"https?://(?!www\.w3\.org)", text):
        problems.append("外部URLを含む（ESP32はインターネット前提にしない）")
    if re.search(r"switchbot", text, re.I):
        problems.append("SwitchBot の名称を含む")
    used = set(re.findall(r"[\"'`](/api/[\w/]+)", text))
    unknown = sorted(u for u in used if u.rstrip("/") not in API_PATHS)
    if unknown:
        problems.append(f"API一覧に無いパス: {unknown}")
    size = len(text.encode("utf-8"))
    if size > 80_000:
        problems.append(f"サイズ {size} bytes（80KB以下）")
    r.add("ui", not problems, f"{size} bytes" + ("。" + " / ".join(problems) if problems else ""))


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    state = load(STATE)
    item = item_of(state, sys.argv[1])
    if item["status"] != "in_progress" or not item.get("base_commit"):
        print(f"{item['id']} は in_progress ではない（先に state.py start）")
        return 2
    r = Result()
    logs: dict[str, str] = {}
    check_scope(item, r)
    check_pending(r)
    check_item(item, r, logs)

    VERIFY_DIR.mkdir(parents=True, exist_ok=True)
    out = {"id": item["id"], "attempt": item["attempts"], "ok": r.ok,
           "checks": [{"name": n, "ok": ok, "detail": d} for n, ok, d in r.rows]}
    (VERIFY_DIR / f"{item['id']}.json").write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    md = [f"# verify {item['id']} attempt {item['attempts']}: {'PASS' if r.ok else 'FAIL'}", ""]
    md += [f"- {'OK ' if ok else 'NG '} **{n}** {d}" for n, ok, d in r.rows]
    for k, v in logs.items():
        md += ["", f"## {k}（末尾60行）", "", "```", v, "```"]
    (VERIFY_DIR / f"{item['id']}.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    print("\n".join(md[:2 + len(r.rows)]))
    return 0 if r.ok else 1


if __name__ == "__main__":
    sys.exit(main())
