#!/usr/bin/env python3
"""ヘッドレスでループを回す（Ralph 方式：毎周まっさらなコンテキストで claude を起動）。

  python scripts/run_loop.py                 設定（loop/state.json の config.runner）で回す
  python scripts/run_loop.py --max 3         最大3周
  python scripts/run_loop.py --dry-run       次に何をするかだけ表示

止まる条件（どれか1つ）:
  - 自動で進める項目が無い（人の対応待ち or 全完了）
  - 周回数の上限
  - 累計コストの上限（claude の JSON 出力の total_cost_usd を合計）
  - 同じ項目が連続で失敗した回数の上限（進んでいない）
  - claude がエラー終了
各周の結果は loop/runs.jsonl に1行ずつ残る。
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUNS = ROOT / "loop" / "runs.jsonl"


def state_next() -> dict:
    out = subprocess.run([sys.executable, "scripts/state.py", "next"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    return json.loads(out.stdout)


def item_status(iid: str) -> str:
    st = json.loads((ROOT / "loop" / "state.json").read_text(encoding="utf-8"))
    return next(i["status"] for i in st["items"] if i["id"] == iid)


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    cfg = json.loads((ROOT / "loop" / "state.json").read_text(encoding="utf-8"))["config"]["runner"]
    p = argparse.ArgumentParser()
    p.add_argument("--max", type=int, default=cfg["max_iterations"])
    p.add_argument("--budget", type=float, default=cfg["max_cost_usd"])
    p.add_argument("--dry-run", action="store_true")
    a = p.parse_args()

    claude = shutil.which("claude")
    if not claude:
        print("claude が PATH に無い")
        return 2

    total_cost = 0.0
    fails: dict[str, int] = {}
    for i in range(1, a.max + 1):
        nx = state_next()
        if nx["result"] != "ITEM":
            print(f"[{i}] 停止：{nx['result']} {nx.get('message', '')} {nx.get('waiting', nx.get('items', ''))}")
            return 0
        iid = nx["id"]
        print(f"[{i}] {iid} {nx['title']}（attempt {nx['attempts'] + 1}）")
        if a.dry_run:
            return 0
        started = dt.datetime.now().astimezone().isoformat(timespec="seconds")
        cmd = [claude, "-p", "/loop-step", "--output-format", "json",
               "--permission-mode", "acceptEdits", "--max-turns", str(cfg["max_turns_per_step"])]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        try:
            res = json.loads(r.stdout)
        except json.JSONDecodeError:
            res = {"is_error": True, "result": (r.stdout + r.stderr)[-2000:]}
        cost = float(res.get("total_cost_usd") or 0)
        total_cost += cost
        status = item_status(iid)
        rec = {"at": started, "iter": i, "item": iid, "status_after": status, "cost_usd": round(cost, 4),
               "turns": res.get("num_turns"), "is_error": res.get("is_error", r.returncode != 0),
               "result": str(res.get("result", ""))[-600:]}
        with RUNS.open("a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
        print(f"    → {status}  ${cost:.2f}（累計 ${total_cost:.2f}）")
        print("    " + str(res.get("result", "")).strip().replace("\n", "\n    ")[:800])

        if rec["is_error"]:
            print("停止：claude がエラー終了。loop/runs.jsonl を確認")
            return 1
        if status != "done":
            fails[iid] = fails.get(iid, 0) + 1
            if fails[iid] >= cfg["stop_after_same_item_failures"]:
                print(f"停止：{iid} が連続 {fails[iid]} 回通らない。loop/reviews と loop/verify を確認")
                return 1
        else:
            fails.pop(iid, None)
        if total_cost >= a.budget:
            print(f"停止：累計コスト ${total_cost:.2f} が上限 ${a.budget:.2f} に達した")
            return 0
    print(f"停止：周回上限 {a.max}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
