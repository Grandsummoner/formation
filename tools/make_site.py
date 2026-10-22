#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Grand (Grandsummoner), after Felucca's web/make_site.py (Leo Kuroshita, Hügelton Instruments)
"""Make NavyArp2's site (GitHub Pages: .github/workflows/release.yml publishes it):

  index.html                 the web installer: web/index.html with web/fm1pkg.js, web/fm1ota.js and the package's
                             metadata inlined (one file, no build step in the browser)
  manual.html                docs/MANUAL.md as a page
  firmware/navyarp2-VER.fwsc the package, with LICENSE, LICENSING.md and LICENSES/ beside it (the package holds three
                             JieLi AC79 SDK files, Apache-2.0)

  tools/make_site.py build/navyarp2-X.Y.fwsc X.Y [OUT_DIR, default site] [SOURCE_URL]

Only a package made by this tree's build is accepted: a release identity (FM-1_96N) and Felucca's update loader.
"""
import html
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def manual_html(md_text):
    try:
        import markdown
        body = markdown.markdown(md_text, extensions=["tables", "fenced_code"])
    except ImportError:                                  # no python-markdown: still readable
        body = "<pre>" + html.escape(md_text) + "</pre>"
    return ("<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,"
            "initial-scale=1\"><title>NavyArp2 manual</title><style>body{font:16px/1.6 system-ui,sans-serif;max-width:760px;"
            "margin:0 auto;padding:32px 16px;background:#04100f;color:#e4f6f4}a{color:#3fd9d2}table{border-collapse:collapse}"
            "td,th{border:1px solid #17302f;padding:4px 10px;text-align:left}code,pre{background:#0b2523;padding:2px 5px}"
            "h1,h2,h3{color:#3fd9d2}</style><body><p><a href=\"./\">Install NavyArp2</a></p>" + body + "</body></html>\n")


def main(pkg, version, out="site", source=""):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_96\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a NavyArp2 release package (FM-1_96N)")
    if b"FELUCCA-LOADER-1" not in raw:                   # never publish a package carrying vendor files
        raise SystemExit(f"{pkg}: no Felucca update loader in it; the site ships only packages made by tools/build.py")
    page = (WEB / "index.html").read_text(encoding="utf-8")
    lib = strip_module((WEB / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((WEB / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"navyarp2-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "firmware/" + name, "source": source})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"web/index.html must contain {mark} once; update make_site.py")
    page = page.replace("/*LIB*/", lib).replace("/*META*/", meta)
    fw = out / "firmware"
    fw.mkdir(parents=True, exist_ok=True)
    for old in fw.glob("navyarp2-*.fwsc"):               # one package: the current one
        old.unlink()
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / ".nojekyll").write_text("", encoding="utf-8")
    shutil.copy(pkg, fw / name)
    (out / "manual.html").write_text(manual_html((ROOT / "docs" / "MANUAL.md").read_text(encoding="utf-8")), encoding="utf-8")
    (fw / "LICENSES").mkdir(exist_ok=True)
    names = sorted(f.name for f in (ROOT / "LICENSES").glob("*.txt"))
    for n in names:
        shutil.copy(ROOT / "LICENSES" / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>NavyArp2 licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n', encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(ROOT / doc, fw / doc)
    print(f"site: {out}: index.html ({len(page)} B), manual.html, firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    if not 3 <= len(sys.argv) <= 5:
        sys.exit(__doc__)
    main(*sys.argv[1:5])
