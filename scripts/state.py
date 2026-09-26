#!/usr/bin/env python3
"""ループの状態（loop/state.json）を操作する唯一の入口。

loop/state.json は hooks により Edit/Write できない。状態の変更はすべてこのスクリプト経由で行い、
変更は loop/log.md に1行ずつ追記される。

  python scripts/state.py next                 次に着手する項目を JSON で返す
  python scripts/state.py start <ID>           着手（in_progress、base_commit 記録、attempt+1）
  python scripts/state.py fail <ID> --reason R 不合格（attempt が上限なら human へ）
  python scripts/state.py review <ID>          最新レビューの VERDICT を読み、PASS なら done + commit
  python scripts/state.py resolve <ID> pass|fail [--note N] [--reopen ID ...]   人間ゲートの結果
  python scripts/state.py reopen <ID> --note N 項目を todo に戻す（人の差し戻し）
  python scripts/state.py decide <D> --note N [--set REQ=決定 ...]  未決事項を確定
  python scripts/state.py summary              状況の要約（SessionStart hook でも使う）
  python scripts/state.py show <ID>            項目の詳細
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATE = ROOT / "loop" / "state.json"
REQ_INDEX = ROOT / "docs" / "req-index.json"
LOG = ROOT / "loop" / "log.md"
INBOX = ROOT / "loop" / "inbox.md"
REVIEWS = ROOT / "loop" / "reviews"
VERIFY_DIR = ROOT / "loop" / "verify"

AUTO_TYPES = {"design", "test-design", "test-code", "impl"}


def now() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def save(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def log(msg: str) -> None:
    if not LOG.exists():
        LOG.write_text("# ループ実行ログ\n\n", encoding="utf-8")
    with LOG.open("a", encoding="utf-8") as f:
        f.write(f"- {now()} {msg}\n")


def git(*args: str, check: bool = True) -> str:
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    if check and r.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    return r.stdout.rstrip("\n")  # 先頭の空白を残す（porcelain の " M path"）


def in_globs(path: str, globs: list[str]) -> bool:
    from fnmatch import fnmatch
    path = path.replace("\\", "/")
    for g in globs:
        if g.endswith("/**") and (path == g[:-3] or path.startswith(g[:-2])):
            return True
        if g.endswith("/") and path.startswith(g):
            return True
        if fnmatch(path, g):
            return True
    return False


def item_of(state: dict, iid: str) -> dict:
    for it in state["items"]:
        if it["id"] == iid:
            return it
    raise SystemExit(f"unknown item: {iid}")


def deps_done(state: dict, it: dict) -> bool:
    return all(item_of(state, d)["status"] == "done" for d in it.get("deps", []))


def ready_human(state: dict) -> list[dict]:
    return [it for it in state["items"] if it["type"] == "human" and it["status"] == "todo" and deps_done(state, it)]


def write_inbox(state: dict) -> None:
    lines = ["# 人の対応待ち", "", f"更新: {now()}", ""]
    stuck = [it for it in state["items"] if it["status"] == "human"]
    if stuck:
        lines += ["## ループが止めた項目（上限到達・テスト不備など）", ""]
        for it in stuck:
            lines.append(f"- **{it['id']}** {it['title']} — {it.get('last_reason', '')}")
        lines.append("")
    hr = ready_human(state)
    if hr:
        lines += ["## 実機確認・承認（/hw-gate で結果を記録）", ""]
        for it in hr:
            lines.append(f"- **{it['id']}** {it['title']}  \n  {it.get('how', '')}")
        lines.append("")
    req = load(REQ_INDEX)
    opens = [d for d in req.get("decisions", []) if d["status"] == "open"]
    if opens:
        lines += ["## 未決事項（/decide で確定）", ""]
        for d in opens:
            lines.append(f"- **{d['id']}** {d['title']}（{d['when']}）")
        lines.append("")
    INBOX.write_text("\n".join(lines), encoding="utf-8")


def cmd_next(state: dict, _a) -> None:
    if any(it["status"] == "in_progress" for it in state["items"]):
        cur = [it["id"] for it in state["items"] if it["status"] == "in_progress"]
        print(json.dumps({"result": "IN_PROGRESS", "items": cur}, ensure_ascii=False))
        return
    for it in state["items"]:
        if it["type"] in AUTO_TYPES and it["status"] == "todo" and deps_done(state, it):
            out = {"result": "ITEM", "id": it["id"], "agent": it["agent"], "type": it["type"],
                   "title": it["title"], "attempts": it.get("attempts", 0)}
            print(json.dumps(out, ensure_ascii=False))
            return
    write_inbox(state)
    remaining = [it for it in state["items"] if it["status"] != "done"]
    if not remaining:
        print(json.dumps({"result": "NONE", "message": "全項目完了"}, ensure_ascii=False))
        return
    print(json.dumps({"result": "HUMAN", "message": "自動で進められる項目がない。loop/inbox.md を参照",
                      "waiting": [it["id"] for it in ready_human(state)] + [it["id"] for it in state["items"] if it["status"] == "human"]},
                     ensure_ascii=False))


def cmd_start(state: dict, a) -> None:
    it = item_of(state, a.id)
    if it["type"] not in AUTO_TYPES:
        raise SystemExit(f"{a.id} は人間ゲート。/hw-gate を使う")
    if it["status"] != "todo" or not deps_done(state, it):
        raise SystemExit(f"{a.id} は着手できない（status={it['status']}, deps完了={deps_done(state, it)}）")
    dirty = [ln[3:].strip().strip('"') for ln in git("status", "--porcelain", "--untracked-files=all", "--", ".", ":(exclude)loop").splitlines() if ln.strip()]
    retry = it.get("attempts", 0) > 0 and it.get("base_commit")
    if dirty and not (retry and all(in_globs(p, it["allowed_paths"]) for p in dirty)):
        raise SystemExit("loop/ 以外に未コミットの変更がある。人が確認してコミットか破棄をしてから再開する:\n" + "\n".join(dirty))
    it["status"] = "in_progress"
    it["attempts"] = it.get("attempts", 0) + 1
    if not retry:  # 再挑戦では前回の途中成果を引き継ぐので基準コミットは変えない
        it["base_commit"] = git("rev-parse", "HEAD")
    it["started_at"] = now()
    save(STATE, state)
    log(f"START {a.id} attempt={it['attempts']}")
    brief = {k: it.get(k) for k in ("id", "title", "type", "agent", "reqs", "outputs", "allowed_paths", "checks", "brief", "attempts")}
    fb = sorted(REVIEWS.glob(f"{a.id}-a*.md"))
    brief["previous_reviews"] = [str(p.relative_to(ROOT)).replace("\\", "/") for p in fb]
    vf = VERIFY_DIR / f"{a.id}.md"
    brief["previous_verify"] = str(vf.relative_to(ROOT)).replace("\\", "/") if vf.exists() and it["attempts"] > 1 else None
    print(json.dumps(brief, ensure_ascii=False, indent=2))


def to_retry_or_human(state: dict, it: dict, reason: str) -> None:
    it["last_reason"] = reason
    if it["attempts"] >= state["config"]["max_attempts"]:
        it["status"] = "human"
        log(f"ESCALATE {it['id']} attempts={it['attempts']} reason={reason}")
    else:
        it["status"] = "todo"
        log(f"RETRY {it['id']} attempts={it['attempts']} reason={reason}")


def cmd_fail(state: dict, a) -> None:
    it = item_of(state, a.id)
    if it["status"] != "in_progress":
        raise SystemExit(f"{a.id} は in_progress ではない")
    if a.escalate:
        it["status"] = "human"
        it["last_reason"] = a.reason
        log(f"ESCALATE {it['id']} reason={a.reason}")
    else:
        to_retry_or_human(state, it, a.reason)
    save(STATE, state)
    write_inbox(state)
    print(json.dumps({"id": it["id"], "status": it["status"]}, ensure_ascii=False))


def cmd_review(state: dict, a) -> None:
    it = item_of(state, a.id)
    if it["status"] != "in_progress":
        raise SystemExit(f"{a.id} は in_progress ではない")
    path = REVIEWS / f"{a.id}-a{it['attempts']}.md"
    if not path.exists():
        raise SystemExit(f"レビューファイルが無い: {path.relative_to(ROOT)}")
    first = next((ln.strip() for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip()), "")
    vfile = VERIFY_DIR / f"{a.id}.json"
    verified = vfile.exists() and load(vfile).get("ok") is True and load(vfile).get("attempt") == it["attempts"]
    if first == "VERDICT: PASS" and verified:
        it["status"] = "done"
        it["done_at"] = now()
        it.pop("last_reason", None)
        save(STATE, state)
        log(f"DONE {a.id} attempts={it['attempts']}")
        git("add", "-A")
        git("commit", "-m", f"loop: {a.id} {it['title']}", "-m", f"attempt {it['attempts']}, review {path.name}")
        result = "PASS"
    else:
        reason = "review REJECT" if first == "VERDICT: REJECT" else ("verify 未通過" if not verified else f"VERDICT 行が不正: {first!r}")
        to_retry_or_human(state, it, reason)
        save(STATE, state)
        result = "REJECT"
    write_inbox(state)
    print(json.dumps({"id": a.id, "result": result, "status": it["status"]}, ensure_ascii=False))


def cmd_resolve(state: dict, a) -> None:
    it = item_of(state, a.id)
    if it["type"] != "human" and it["status"] != "human":
        raise SystemExit(f"{a.id} は人間ゲートでも止められた項目でもない")
    if it["type"] == "human" and not deps_done(state, it):
        raise SystemExit(f"{a.id} の前提が終わっていない: {it['deps']}")
    note = a.note or ""
    if a.result == "pass":
        if it["type"] == "human":
            it["status"] = "done"
        else:
            it["status"] = "todo"  # 止められた自動項目を人が直した／再挑戦を許可
            it["attempts"] = 0
        it["resolved_note"] = note
        log(f"RESOLVE {a.id} pass {note}")
    else:
        it["resolved_note"] = note
        log(f"RESOLVE {a.id} fail {note}")
    for rid in a.reopen or []:
        r = item_of(state, rid)
        r["status"] = "todo"
        r["attempts"] = 0
        r["human_feedback"] = note
        log(f"REOPEN {rid} by {a.id}: {note}")
    save(STATE, state)
    write_inbox(state)
    if git("status", "--porcelain"):
        git("add", "-A")
        git("commit", "-m", f"human: {a.id} {a.result}", "-m", note or "-")
    print(json.dumps({"id": a.id, "status": it["status"], "reopened": a.reopen or []}, ensure_ascii=False))


def cmd_reopen(state: dict, a) -> None:
    it = item_of(state, a.id)
    it["status"] = "todo"
    it["attempts"] = 0
    it["human_feedback"] = a.note
    save(STATE, state)
    log(f"REOPEN {a.id}: {a.note}")
    write_inbox(state)
    print(json.dumps({"id": a.id, "status": "todo"}, ensure_ascii=False))


def cmd_decide(state: dict, a) -> None:
    req = load(REQ_INDEX)
    dec = next((d for d in req["decisions"] if d["id"] == a.id), None)
    if not dec:
        raise SystemExit(f"unknown decision: {a.id}")
    dec["status"] = "closed"
    dec["result"] = a.note
    dec["closed_at"] = now()
    for s in a.set or []:
        rid, _, status = s.partition("=")
        r = next((x for x in req["reqs"] if x["id"] == rid), None)
        if not r:
            raise SystemExit(f"unknown req: {rid}")
        if status not in ("決定", "仮", "未決", "対象外"):
            raise SystemExit(f"status は 決定/仮/未決/対象外: {status}")
        r["status"] = status
        if status == "対象外":
            r["code"] = False
    save(REQ_INDEX, req)
    log(f"DECIDE {a.id}: {a.note} {' '.join(a.set or [])}")
    write_inbox(state)
    git("add", "-A")
    git("commit", "-m", f"decide: {a.id}", "-m", a.note)
    print(json.dumps(dec, ensure_ascii=False, indent=2))


def cmd_summary(state: dict, _a) -> None:
    cnt: dict[str, int] = {}
    for it in state["items"]:
        cnt[it["status"]] = cnt.get(it["status"], 0) + 1
    print("[irhub loop] 状態: " + ", ".join(f"{k}={v}" for k, v in sorted(cnt.items())))
    cur = [it["id"] for it in state["items"] if it["status"] == "in_progress"]
    if cur:
        print(f"[irhub loop] 作業中のまま: {cur}（前回の周回が途中で終わった。/loop-step の手順0で扱う）")
    nxt = next((it for it in state["items"] if it["type"] in AUTO_TYPES and it["status"] == "todo" and deps_done(state, it)), None)
    print(f"[irhub loop] 次の自動項目: {nxt['id'] + ' ' + nxt['title'] if nxt else 'なし'}")
    hr = [it["id"] for it in ready_human(state)] + [it["id"] for it in state["items"] if it["status"] == "human"]
    print(f"[irhub loop] 人の対応待ち: {hr or 'なし'}（詳細 loop/inbox.md）")


def cmd_show(state: dict, a) -> None:
    print(json.dumps(item_of(state, a.id), ensure_ascii=False, indent=2))


def main() -> None:
    sys.stdout.reconfigure(encoding="utf-8")
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("next")
    s = sub.add_parser("start"); s.add_argument("id")
    s = sub.add_parser("fail"); s.add_argument("id"); s.add_argument("--reason", required=True); s.add_argument("--escalate", action="store_true")
    s = sub.add_parser("review"); s.add_argument("id")
    s = sub.add_parser("resolve"); s.add_argument("id"); s.add_argument("result", choices=["pass", "fail"]); s.add_argument("--note"); s.add_argument("--reopen", nargs="*")
    s = sub.add_parser("reopen"); s.add_argument("id"); s.add_argument("--note", required=True)
    s = sub.add_parser("decide"); s.add_argument("id"); s.add_argument("--note", required=True); s.add_argument("--set", nargs="*")
    sub.add_parser("summary")
    s = sub.add_parser("show"); s.add_argument("id")
    a = p.parse_args()
    state = load(STATE)
    {"next": cmd_next, "start": cmd_start, "fail": cmd_fail, "review": cmd_review, "resolve": cmd_resolve,
     "reopen": cmd_reopen, "decide": cmd_decide, "summary": cmd_summary, "show": cmd_show}[a.cmd](state, a)


if __name__ == "__main__":
    main()
