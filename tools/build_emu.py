#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Grand (Grandsummoner)
"""Compile the app to WebAssembly for the browser emulator (web/emulator.html): build/emu.wasm.

The app is compiled unchanged against tests/web.c, a fake FM-1 for a browser (DINGHY_WEB), with the zig compiler
(`pip install ziglang`), a clang with the wasm32 target, or whatever CC says.

  python3 tools/build_emu.py [VERSION]
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "emu.wasm"


def compiler():
    if os.environ.get("CC"):
        return os.environ["CC"].split() + ["--target=wasm32"]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "cc", "-target", "wasm32-freestanding"]
    except ImportError:
        pass
    if shutil.which("clang"):
        return ["clang", "--target=wasm32"]
    raise SystemExit("no compiler for WebAssembly: pip install ziglang (or install clang with the wasm32 target)")


def main(version="dev"):
    OUT.parent.mkdir(exist_ok=True)
    cmd = compiler() + [
        "-O2", "-ffreestanding", "-fno-builtin", "-Wall", "-Wno-unused-function", "-Wno-unused-const-variable",
        "-DDINGHY_HOST", "-DDINGHY_WEB", f'-DAPP_VER="{version}"',
        "-I", str(ROOT / "firmware" / "src"), "-I", str(ROOT / "firmware" / "gen"),
        str(ROOT / "firmware" / "src" / "formation.c"),
        "-nostdlib", "-Wl,--no-entry", "-Wl,-z,stack-size=262144", "-o", str(OUT),
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    print(f"{OUT.relative_to(ROOT)}: {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    main(*sys.argv[1:2])
