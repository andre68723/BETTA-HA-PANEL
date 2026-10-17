#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Build one firmware-only classic-script bundle and one stylesheet from the ordered editor sources.

The checked-in www/ tree stays directly serveable for development. Generated
index.html and assets live only in the build directory; no large generated
JavaScript file is committed to the repository.
"""
import re
import sys
from pathlib import Path


def build(www: Path, output: Path, js_name: str, css_name: str, sources: list[str]) -> None:
    sheets = [source for source in sources if source.endswith(".css")]
    scripts = [source for source in sources if not source.endswith(".css")]
    if not scripts or not sheets:
        raise ValueError("at least one script and one stylesheet are required")
    if not re.fullmatch(r"js/app\.[0-9a-f]{16}\.js", js_name):
        raise ValueError("js_name must be a fingerprinted js/app.*.js path")
    if not re.fullmatch(r"styles\.[0-9a-f]{16}\.css", css_name):
        raise ValueError("css_name must be a fingerprinted styles.*.css path")
    if len(scripts) != len(set(scripts)) or any(
        not re.fullmatch(r"js/[a-z0-9-]+\.js", script) for script in scripts
    ):
        raise ValueError("scripts must be unique js/*.js paths")
    if len(sheets) != len(set(sheets)) or any(
        not re.fullmatch(r"(css/)?[a-z0-9-]+\.css", sheet) for sheet in sheets
    ):
        raise ValueError("stylesheets must be unique *.css or css/*.css paths")

    html = (www / "index.html").read_text(encoding="utf-8")
    found = re.findall(r'<script src="/(js/[^"?]+\.js)"></script>', html)
    if found != scripts:
        raise ValueError("index.html script tags differ from the ordered build list")
    old_block = "".join(f'  <script src="/{script}"></script>\n' for script in scripts)
    if html.count(old_block) != 1:
        raise ValueError("index.html script tags must form one consecutive block")
    found_css = re.findall(r'<link rel="stylesheet" href="/([^"?]+\.css)" />', html)
    if found_css != sheets:
        raise ValueError("index.html stylesheet links differ from the ordered build list")
    old_css_block = "".join(f'  <link rel="stylesheet" href="/{sheet}" />\n' for sheet in sheets)
    if html.count(old_css_block) != 1:
        raise ValueError("index.html stylesheet links must form one consecutive block")

    new_block = f'  <script src="/{js_name}"></script>\n'
    built_html = html.replace(old_block, new_block).replace(
        old_css_block, f'  <link rel="stylesheet" href="/{css_name}" />\n'
    )
    parts = [b"/* Generated from modular www/js sources. Do not edit. */\n"]
    for script in scripts:
        parts.extend((f"\n;/* {script} */\n".encode("utf-8"), (www / script).read_bytes(), b"\n"))
    files = {
        "index.html": built_html.encode("utf-8"),
        js_name: b"".join(parts),
        # The same cascade as the separate links: concatenated in their order.
        css_name: b"".join((www / sheet).read_bytes() for sheet in sheets),
    }

    for name, data in files.items():
        dst = output / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists() or dst.read_bytes() != data:
            dst.write_bytes(data)


if __name__ == "__main__":
    if len(sys.argv) < 6:
        raise SystemExit("usage: bundle_assets.py <www> <output> <js/app.HASH.js> <styles.HASH.css> "
                         "<stylesheet.css>... <js/...>...")
    build(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5:])
