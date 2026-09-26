#!/usr/bin/env python3
"""PC で画面（web/index.html）を確かめるためのモックサーバー（D-05 12節）。

本体の代わりではない。API の形（D-04）だけを真似て、状態はメモリに持つ。
Python 標準ライブラリだけを使う。

  python tools/mock_server.py [--host 127.0.0.1] [--port 8000] [--unsynced] [--no-sensor] [--delay-ms 0]
"""
import argparse
import datetime
import json
import os
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX = os.path.join(ROOT, "web", "index.html")

# 画面確認用の見本値。本体の値ではない（本体の値は lib/core/src/ac_capabilities.h だけ）。
# ここを直して、画面が選択肢に追従することを確かめる。
CAPS = {
    "modes": ["auto", "cool", "dry", "heat"],
    "tempMin": 16, "tempMax": 30, "tempStep": 1,
    "tempModes": ["cool", "dry", "heat"],
    "fan": ["auto", "min", "low", "medium", "high"],
}
SCHEDULE_MAX = 10  # 見本値
AC_KEYS = ["power", "mode", "temp", "fan"]
# D-02 2節の風量の列挙。列挙の外は "fan: unknown fan"、列挙にあるが CAPS に無いものは "fan not supported"（D-04 10.3）
FAN_ENUM = ["auto", "min", "low", "medium", "high", "max"]
DAY_KEYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
JST = datetime.timezone(datetime.timedelta(hours=9))


class Mock:
    def __init__(self, args):
        self.args = args
        self.ac = {"power": False, "mode": "cool", "temp": 26, "fan": "auto"}
        self.temp_by_mode = {"cool": 26}
        self.schedules = []
        self.next_id = 1

    # ---- 状態 ----
    def now(self):
        if self.args.unsynced:
            return None
        return datetime.datetime.now(JST).replace(microsecond=0)

    def now_iso(self):
        n = self.now()
        return None if n is None else n.strftime("%Y-%m-%dT%H:%M:%S+09:00")

    def status(self):
        if self.args.no_sensor:
            climate = {"valid": False, "temperature": None, "humidity": None}
        else:
            climate = {"valid": True, "temperature": 24, "humidity": 55.2}
        return {
            "ac": dict(self.ac),
            "acCapabilities": CAPS,
            "climate": climate,
            "clock": {"synced": not self.args.unsynced, "now": self.now_iso()},
        }

    def schedule_list(self):
        return {"max": SCHEDULE_MAX, "schedules": self.schedules}

    # ---- POST /api/ac ----
    def post_ac(self, body):
        patch = parse_obj(body)
        if patch is None:
            return 400, err("invalid json")
        for k in patch:
            if k not in AC_KEYS:
                return 400, err('unknown key "%s"' % k)
        if "power" in patch and not isinstance(patch["power"], bool):
            return 400, err("power: must be boolean")
        if "mode" in patch:
            if not isinstance(patch["mode"], str):
                return 400, err("mode: must be string")
            if patch["mode"] not in CAPS["modes"]:
                return 400, err("mode: unknown mode")
        if "temp" in patch and (isinstance(patch["temp"], bool) or not isinstance(patch["temp"], int)):
            return 400, err("temp: must be integer")
        if "fan" in patch:
            if not isinstance(patch["fan"], str):
                return 400, err("fan: must be string")
            if patch["fan"] not in FAN_ENUM:
                return 400, err("fan: unknown fan")
        if not patch:
            return 400, err("no ac fields")
        mode = patch.get("mode", self.ac["mode"])
        if "temp" in patch:
            if mode not in CAPS["tempModes"]:
                return 400, err("temp not supported in this mode")
            if not CAPS["tempMin"] <= patch["temp"] <= CAPS["tempMax"]:
                return 400, err("temp out of range")
        if "fan" in patch and patch["fan"] not in CAPS["fan"]:
            return 400, err("fan not supported")
        if "power" in patch:
            self.ac["power"] = patch["power"]
        if "mode" in patch:
            self.ac["mode"] = mode
        if mode in CAPS["tempModes"]:
            if "temp" in patch:
                self.temp_by_mode[mode] = patch["temp"]
            t = self.temp_by_mode.get(mode, (CAPS["tempMin"] + CAPS["tempMax"]) // 2)
            self.temp_by_mode[mode] = t
            self.ac["temp"] = t
        else:
            self.ac["temp"] = None
        if "fan" in patch:
            self.ac["fan"] = patch["fan"]
        return 200, {"ac": dict(self.ac)}

    # ---- スケジュール ----
    def replace(self, body, is_import):
        obj = parse_obj(body)
        if obj is None:
            return 400, err("invalid json")
        if is_import:
            if "version" not in obj:
                return 400, err("version missing")
            if obj["version"] != 1:
                return 400, err("unsupported version")
        items = obj.get("schedules")
        if not isinstance(items, list):
            return 400, err("schedules: must be array")
        if len(items) > SCHEDULE_MAX:
            return 400, err("too many schedules (max %d)" % SCHEDULE_MAX)
        out = []
        for i, s in enumerate(items):
            if not isinstance(s, dict):
                return 400, err("schedules[%d]: must be object" % i)
            out.append(dict(s))
        used = {s["id"] for s in out if isinstance(s.get("id"), int)}
        if used and max(used) >= self.next_id:
            self.next_id = max(used) + 1
        for s in out:
            if "id" not in s:
                while self.next_id in used:
                    self.next_id += 1
                s["id"] = self.next_id
                used.add(self.next_id)
                self.next_id += 1
            s.setdefault("enabled", True)
            s["days"] = [d for d in DAY_KEYS if d in s.get("days", [])]
        self.schedules = out
        return 200, self.schedule_list()

    def export(self):
        n = self.now()
        name = "irhub-schedules.json" if n is None else n.strftime("irhub-schedules-%Y%m%d-%H%M.json")
        body = {"version": 1, "exportedAt": self.now_iso(), "schedules": self.schedules}
        return 200, body, name


def parse_obj(body):
    try:
        obj = json.loads(body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return None
    return obj if isinstance(obj, dict) else None


def err(msg):
    return {"error": msg}


ROUTES = {
    "/api/status": {"GET"},
    "/api/ac": {"POST"},
    "/api/schedules": {"GET", "PUT"},
    "/api/schedules/export": {"GET"},
    "/api/schedules/import": {"POST"},
}


def make_handler(mock):
    class Handler(BaseHTTPRequestHandler):
        def _send(self, status, data, filename=None):
            raw = json.dumps(data, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            if filename:
                self.send_header("Content-Disposition", 'attachment; filename="%s"' % filename)
            self.end_headers()
            self.wfile.write(raw)

        def _handle(self, method):
            path = self.path.split("?", 1)[0]
            if method == "GET" and path == "/":
                with open(INDEX, "rb") as f:
                    raw = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
                return
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else b""
            if mock.args.delay_ms > 0:
                time.sleep(mock.args.delay_ms / 1000.0)
            if path not in ROUTES:
                return self._send(404, err("not found"))
            if method not in ROUTES[path]:
                return self._send(405, err("method not allowed"))
            if path == "/api/status":
                return self._send(200, mock.status())
            if path == "/api/ac":
                return self._send(*mock.post_ac(body))
            if path == "/api/schedules":
                if method == "GET":
                    return self._send(200, mock.schedule_list())
                return self._send(*mock.replace(body, False))
            if path == "/api/schedules/export":
                status, data, name = mock.export()
                return self._send(status, data, name)
            return self._send(*mock.replace(body, True))

        def do_GET(self):
            self._handle("GET")

        def do_POST(self):
            self._handle("POST")

        def do_PUT(self):
            self._handle("PUT")

        def do_DELETE(self):
            self._handle("DELETE")

    return Handler


def main():
    p = argparse.ArgumentParser(description="IRハブの画面確認用モックサーバー")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8000)
    p.add_argument("--unsynced", action="store_true", help="時刻未取得にする")
    p.add_argument("--no-sensor", action="store_true", help="センサー未読み取りにする")
    p.add_argument("--delay-ms", type=int, default=0, help="各 API 応答を遅らせる時間")
    args = p.parse_args()
    mock = Mock(args)
    # 1本ずつ処理する（ESP32 の WebServer と同じ）
    srv = HTTPServer((args.host, args.port), make_handler(mock))
    print("mock_server: http://%s:%d/ （Ctrl+C で終了）" % (args.host, args.port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
