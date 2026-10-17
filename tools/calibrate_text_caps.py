#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Calibrate how far the text of a tile may grow with its size (text_scale_tile_cap in main/ui/text_scale.c).

Larger text (the Tab5 density step and the user's Larger/Largest steps) needs room. A tile that is too small keeps the smaller text
of its cap instead of overlapping. This tool renders every layout fixture (gallery tiles, edge cases, room cards ...) on the Tab5
geometry at the largest text step with the cap forced to 0, 1, 2 and 3, checks each rendered tile for overlaps with the same rules as
tools/check_widget_layout.py, and for each widget type and growth step k picks the thresholds (w >= a, h >= b) so that no tile
inside them failed at cap k while as many tiles as possible passed. Thresholds never decrease with k.

  python3 tools/calibrate_text_caps.py            # print the table and the tiles that fail even at cap 0
  python3 tools/calibrate_text_caps.py --write    # also update the table in main/ui/text_scale.c

The result depends on the fixtures: add a tile to tests/simulator/layout_cases.json when a size needs protection, then run it again.
"""
import argparse
import json
from pathlib import Path
import sys
import tempfile

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from check_widget_layout import (MATTE_REFERENCE, case_fixture, find_problems, render_dumps)  # noqa: E402
from check_readability import problems as readability_problems  # noqa: E402
from generate_simulator_gallery import DATA, build_simulator, prepare_fixture  # noqa: E402

MIN_TEXT_PX = 20
MARKERS = {"adaptive": ("    /* TILE_CAPS_ADAPTIVE_BEGIN */", "    /* TILE_CAPS_ADAPTIVE_END */"),
           "wide": ("    /* TILE_CAPS_WIDE_BEGIN */", "    /* TILE_CAPS_WIDE_END */")}
FAR = 9999
# Components that are not widget tiles but lay out text on fixed shapes (type, widths, heights, base growth), kept by every run. The energy page
# grows by one step on a screen at least 1000 px wide on the boards with adaptive layouts; the 10-inch boards use the same energy page, so the same row.
MANUAL_ROWS = {"adaptive": [("energy_page", [1000, FAR, FAR], [1, FAR, FAR], 0)],
               "wide": [("energy_page", [1000, FAR, FAR], [1, FAR, FAR], 0)]}


def fixtures():
    _, gallery = prepare_fixture(json.loads(DATA.read_text(encoding="utf-8")))
    return {"gallery": gallery, "cases": case_fixture(gallery),
            "matte_reference": json.loads(MATTE_REFERENCE.read_text(encoding="utf-8")),
            "energy_timeline": json.loads((REPO / "tests/simulator/energy_timeline.json").read_text()),
            "room_extra": json.loads((REPO / "tests/simulator/room_extra.json").read_text()),
            "wallbox_evcc": json.loads((REPO / "tests/simulator/wallbox_evcc.json").read_text()),
            "wallbox_disconnected": json.loads((REPO / "tests/simulator/wallbox_disconnected.json").read_text()),
            "wallbox_sessions": json.loads((REPO / "tests/simulator/wallbox_sessions.json").read_text()),
            "bambu_printer": json.loads((REPO / "tests/simulator/bambu_printer.json").read_text()),
            "readability": json.loads((REPO / "tests/simulator/readability.json").read_text()),
            "readability_history_numeric": json.loads((REPO / "tests/simulator/readability_history_numeric.json").read_text()),
            "readability_history_state": json.loads((REPO / "tests/simulator/readability_history_state.json").read_text())}


def run_cap(binary, name, fixture, cap, step, temp):
    """The widget ids that fail at a forced cap: overlaps and escapes (check_widget_layout) and clipped essential text."""
    path = Path(temp) / f"{name}_{cap}.json"
    path.write_text(json.dumps(fixture, ensure_ascii=False), encoding="utf-8")
    ids = [page["id"] for page in fixture["pages"]]
    extra = ["--text-step", str(step), "--text-cap", str(cap)]
    failed = set()
    for context, dump in render_dumps(binary, path, ids, temp, extra):
        lines = [json.loads(line) for line in Path(dump).read_text(encoding="utf-8").splitlines() if line]
        for problem in find_problems(lines, min_px=MIN_TEXT_PX):
            failed.add(problem.split(" (")[0])
        if name.startswith("readability"):
            for problem in readability_problems(lines):
                failed.add(problem.split(":")[0])
    return failed


def thresholds(samples, step):
    """samples: [(w, h, passed_at_cap_step)]. The (a, b) with no failing sample inside and the most passing samples inside."""
    widths = sorted({w for w, _, _ in samples}) + [FAR]
    heights = sorted({h for _, h, _ in samples}) + [FAR]
    best = (-1, FAR, FAR)
    for a in widths:
        for b in heights:
            inside = [ok for w, h, ok in samples if w >= a and h >= b]
            if inside and not all(inside):
                continue
            score = len(inside)
            if score > best[0] or (score == best[0] and a * b > best[1] * best[2]):
                best = (score, a, b)
    return best[1], best[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true", help="update the table in main/ui/text_scale.c")
    parser.add_argument("--variant", default="tab5", choices=("tab5", "panel4", "paneljc"),
                        help="tab5 or panel4 calibrate the adaptive table, paneljc the table of the 10-inch boards (default: tab5)")
    parser.add_argument("--step", type=int, default=2, choices=(0, 1, 2), help="user step to render (default 2: the most growth)")
    args = parser.parse_args()
    binary = build_simulator(args.variant)
    samples = {}   # type -> {(w, h): [pass at cap 0..3]}
    with tempfile.TemporaryDirectory(prefix="betta_caps_") as temp:
        for name, fixture in fixtures().items():
            tiles = {}
            for page in fixture["pages"]:
                for widget in page.get("widgets", []):
                    if widget.get("type") and widget.get("rect") and widget.get("id"):
                        tiles[widget["id"]] = (widget["type"], widget["rect"]["w"], widget["rect"]["h"])
            results = {cap: run_cap(binary, name, fixture, cap, args.step, temp) for cap in range(4)}
            for page, (wtype, w, h) in tiles.items():
                row = samples.setdefault(wtype, {}).setdefault((w, h), [True] * 4)
                for cap in range(4):
                    if page in results[cap]:
                        row[cap] = False
            print(f"{name}: {len(tiles)} tiles, failing at cap 0/1/2/3: " + "/".join(str(len({p for p in results[c] if p in tiles})) for c in range(4)))
    table = []
    for wtype in sorted(samples):
        by_cap = [[(w, h, ok[cap]) for (w, h), ok in samples[wtype].items()] for cap in range(4)]
        base_failures = [(w, h) for w, h, ok in by_cap[0] if not ok]
        if base_failures:
            print(f"WARNING {wtype}: fails even at cap 0 for {sorted(base_failures)[:6]}")
        a_prev = b_prev = 0
        a_list, b_list = [], []
        for step in (1, 2, 3):
            a, b = thresholds(by_cap[step], step)
            a, b = max(a, a_prev), max(b, b_prev)
            a_list.append(a); b_list.append(b); a_prev, b_prev = a, b
        table.append((wtype, a_list, b_list))
    table = [(t, a, b, 0) for t, a, b in table] + MANUAL_ROWS["wide" if args.variant == "paneljc" else "adaptive"]
    rows = "\n".join(f'    {{"{t}", {{{", ".join(map(str, a))}}}, {{{", ".join(map(str, b))}}}, {base}}},' for t, a, b, base in table)
    for t, a, b, base in table:
        print(f"{t:18s} w {a}  h {b}")
    if args.write:
        path = REPO / "main/ui/text_scale.c"
        text = path.read_text(encoding="utf-8")
        begin, end = MARKERS["wide" if args.variant == "paneljc" else "adaptive"]
        start = text.index(begin) + len(begin)
        stop = text.index(end)
        path.write_text(text[:start] + "\n" + rows + "\n" + text[stop:], encoding="utf-8")
        print(f"{path} updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
