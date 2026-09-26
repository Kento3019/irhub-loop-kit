#!/usr/bin/env python3
"""web/index.html を src/generated/index_html.h（バイト配列）に埋め込む（D-05 10節）。

PlatformIO の [env:esp32] で extra_scripts = pre:tools/embed_html.py として呼ばれる。
単独でも python tools/embed_html.py で動く。Python 標準ライブラリだけを使う。
"""
import os
import sys

MAX_BYTES = 80_000  # scripts/verify.py の ui 検査と同じ上限
SRC_REL = os.path.join("web", "index.html")
OUT_REL = os.path.join("src", "generated", "index_html.h")

_env = None
try:
    Import("env")  # noqa: F821  PlatformIO（SCons）から呼ばれたとき
    _env = env  # noqa: F821
except NameError:
    _env = None


def project_dir():
    if _env is not None:
        return _env.subst("$PROJECT_DIR")
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fail(msg):
    print("embed_html: error: " + msg)
    if _env is not None:
        try:
            from SCons.Script import Exit  # type: ignore
            Exit(1)
        except ImportError:
            pass
    sys.exit(1)


def build_header(data):
    lines = [
        "// src/generated/index_html.h",
        "// 生成物：tools/embed_html.py が web/index.html から作る。手で編集しない。",
        "#pragma once",
        "#include <cstddef>",
        "",
        "namespace irhub {",
        "",
        "// web/index.html の中身（UTF-8）。末尾に 0 を1つ付ける（send_P の長さなし版でも安全なように）",
        "static const char kIndexHtml[] = {",
    ]
    body = list(data) + [0]
    for i in range(0, len(body), 16):
        chunk = body[i:i + 16]
        lines.append("  " + ", ".join("0x%02x" % b for b in chunk) + ",")
    lines += [
        "};",
        "// 末尾の 0 を含まないバイト数",
        "static const size_t kIndexHtmlLen = %d;" % len(data),
        "",
        "}  // namespace irhub",
        "",
    ]
    return "\n".join(lines).encode("utf-8")


def main():
    root = project_dir()
    src = os.path.join(root, SRC_REL)
    out = os.path.join(root, OUT_REL)
    if not os.path.isfile(src):
        fail("%s が無い" % SRC_REL)
    with open(src, "rb") as f:
        data = f.read()
    try:
        data.decode("utf-8")
    except UnicodeDecodeError as e:
        fail("%s が UTF-8 として読めない（%s）" % (SRC_REL, e))
    if b"\x00" in data:
        fail("%s が 0x00 を含む" % SRC_REL)
    if len(data) > MAX_BYTES:
        fail("%s が %d バイト（%d バイト以下）" % (SRC_REL, len(data), MAX_BYTES))
    header = build_header(data)
    if os.path.isfile(out):
        with open(out, "rb") as f:
            if f.read() == header:
                return
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(header)
    print("embed_html: web/index.html (%d bytes) -> src/generated/index_html.h" % len(data))


main()
