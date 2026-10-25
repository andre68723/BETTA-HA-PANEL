#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Draw docs/img/development/checks.svg, the overview in docs/development/testing.md.

Update the boxes here when scripts/check.sh gains or loses a check, then run the script.
"""
from pathlib import Path
from xml.sax.saxutils import escape

OUT = Path(__file__).resolve().parents[1] / "docs/img/development/checks.svg"
INK, LINE, FILL, SOFT = "#1d3557", "#1976d2", "#e3f2fd", "#fff8e1"
parts = []


def box(x, y, w, h, title, lines=(), fill=FILL, dash=False, stroke=LINE):
    d = ' stroke-dasharray="14 9"' if dash else ""
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="16" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="4"{d}/>')
    top = y + 34 if lines else y + h / 2 + 7
    parts.append(f'<text x="{x + w / 2}" y="{top}" text-anchor="middle" font-size="21" font-weight="700" '
                 f'fill="{INK}">{escape(title)}</text>')
    for i, line in enumerate(lines):
        parts.append(f'<text x="{x + w / 2}" y="{top + 28 + i * 24}" text-anchor="middle" font-size="17" '
                     f'fill="{INK}">{escape(line)}</text>')


def arrow(x1, y1, x2, y2, dash=False):
    d = ' stroke-dasharray="14 9"' if dash else ""
    parts.append(f'<line x1="{x1}" y1="{y1}" x2="{x2 - 4}" y2="{y2}" stroke="{INK}" stroke-width="5"{d}/>')
    parts.append(f'<polygon points="{x2},{y2} {x2 - 18},{y2 - 11} {x2 - 18},{y2 + 11}" fill="{INK}"/>')


W, H = 1240, 600
parts.append(f'<rect width="{W}" height="{H}" rx="20" fill="#ffffff"/>')

# sources
box(30, 70, 260, 120, "Web editor", ["HTML, CSS, ordered JS"])
box(30, 240, 260, 120, "Firmware sources", ["widgets, layout, themes"])
box(30, 410, 260, 120, "Fixtures", ["layouts and HA states", "in tests/simulator"])
for y in (130, 300, 470):
    arrow(290, y, 380, 300)

# scripts/check.sh
parts.append(f'<rect x="380" y="20" width="440" height="560" rx="20" fill="{SOFT}" stroke="#f9a825" stroke-width="5"/>')
parts.append(f'<text x="600" y="58" text-anchor="middle" font-size="23" font-weight="700" fill="{INK}">scripts/check.sh</text>')
inner = [("Editor and tooling", ["JS/Python tests, syntax, check_web.py"]),
         ("Host C tests", ["pure logic, panel4 and paneljc"]),
         ("Widget layout in the simulator", ["3 variants; 3 text sizes with --full"]),
         ("Fonts, source and docs sizes", ["file size ceilings, font subsets"]),
         ("Gallery freshness", ["a hint, never a failure"]),
         ("Passing steps", ["cached by inputs and tool versions"])]
for i, (title, lines) in enumerate(inner):
    box(410, 78 + i * 83, 380, 72, title, lines, fill="#ffffff")

# results
box(900, 70, 310, 130, "Firmware builds", ["check.sh --build:", "all five variants", "(explicit, not in CI checks)"])
box(900, 250, 310, 110, "CI on GitHub", ["check.yml runs check.sh --full", "without the builds"], fill="#ffffff")
box(900, 410, 310, 130, "Real device", ["screenshots, page captures,", "stress test: only when asked"],
    fill="#ffffff", dash=True)
arrow(820, 135, 900, 135)
arrow(820, 305, 900, 305)
arrow(820, 475, 900, 475, dash=True)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
       f'font-family="Inter, Helvetica, Arial, sans-serif" role="img" aria-label="Checks that run without a device">'
       + "".join(parts) + "</svg>\n")
OUT.write_text(svg, encoding="utf-8")
print(f"wrote {OUT}")
