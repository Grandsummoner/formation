#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/navyarp2.c on this computer (tests/host.c): build/host/navyarp2.wav and the screens as PNG.
#   tests/run_host.sh [script.txt]        (a script: see tests/host.c; tests/demo.txt is the default)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
rm -f build/host/navyarp2-*.png build/host/navyarp2-*.ppm
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/host/navyarp2 firmware/src/navyarp2.c
build/host/navyarp2 build/host/navyarp2 "${1:-tests/demo.txt}"
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("navyarp2-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
