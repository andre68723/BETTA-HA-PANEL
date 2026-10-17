#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Check rendered widgets for overlapping text and content outside the tile.

Renders the gallery fixture (tests/simulator/gallery_data.json), the Matte
comparison fixture (tests/simulator/matte_reference.json), and the edge
cases in tests/simulator/layout_cases.json (gallery tiles at their minimum and
large sizes and every variant) with the SDL simulator in every appearance style, reads each widget's object rectangles
from the simulator's --dump-layout output and reports:

- two visible text labels of one widget that overlap,
- a text label that overlaps a button it does not belong to,
- a text label that lies half on a panel, bar or picture of the tile,
- a label or control that reaches outside its tile.

Rectangles cannot see a ring's shape, so text on an arc or a knob still needs
a look at the gallery. Exit status 1 when a problem is found.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from ui_styles import STYLES
from check_readability import problems as readability_problems, coverage_problems

REPO = Path(__file__).resolve().parent.parent
CASES = REPO / "tests/simulator/layout_cases.json"
MATTE_REFERENCE = REPO / "tests/simulator/matte_reference.json"
# English in every style, and German, whose longer words ("nicht verfügbar") overflow first, once.
PASSES = [(style, "en") for style in STYLES] + [("flat", "de"), ("matte", "de")]
TOLERANCE = 1  # px of anti-aliasing overlap that is not a real collision

def overlap(a, b):
    return (min(a["x2"], b["x2"]) - max(a["x1"], b["x1"]) > TOLERANCE and
            min(a["y2"], b["y2"]) - max(a["y1"], b["y1"]) > TOLERANCE)


def contains(outer, inner):
    return (inner["x1"] >= outer["x1"] - TOLERANCE and inner["y1"] >= outer["y1"] - TOLERANCE and
            inner["x2"] <= outer["x2"] + TOLERANCE and inner["y2"] <= outer["y2"] + TOLERANCE)


def inside(inner, outer):
    """True when inner lies within outer (a label drawn on its own button)."""
    return contains(outer, inner)


def text_rect(rec):
    """The drawn text of a label (see the simulator's --dump-layout), else the object itself."""
    if "tx1" in rec:
        return {"x1": rec["tx1"], "y1": rec["ty1"], "x2": rec["tx2"], "y2": rec["ty2"]}
    return rec


MIN_TEXT_PX = {"panel4": 20, "tab5": 20}  # the 4-inch class has no text below 20 px (docs/appearance.md)


def is_symbol_text(text):
    """Icon glyphs (Material Design Icons, private use area) are not text and have their own sizes."""
    return all(0xE000 <= ord(c) <= 0xF8FF or c.isspace() for c in text)


def find_problems(lines, context="", min_px=0):
    """lines: parsed --dump-layout records. Returns human-readable problem strings."""
    widgets = {}
    for rec in lines:
        widgets.setdefault(rec["widget"], []).append(rec)
    problems = []
    for widget_id, recs in widgets.items():
        card = next((r for r in recs if r["kind"] == "card"), None)
        if card is None:
            continue
        wtype = card.get("type", "")
        where = f"{context}{widget_id} ({wtype})"
        labels = [dict(r, **text_rect(r)) for r in recs if r["kind"] == "label" and r.get("text", "").strip()]
        for label in labels:
            if min_px and label.get("font_px", min_px) < min_px and not is_symbol_text(label["text"]):
                problems.append(f"{where}: text '{label['text']}' is {label['font_px']} px, below the {min_px} px minimum")
            if "hidden name sentinel" in label["text"].lower():
                problems.append(f"{where}: disabled widget name is visible")
        buttons = [r for r in recs if r["kind"] == "button"]
        # Other surfaces directly in the tile (panels, forecast cards, pictures): a text either lies
        # within one or stays clear of it; half on it means one covers the other.
        surfaces = [r for r in recs if r["kind"] in ("obj", "bar", "slider") and r["depth"] == 1]
        if wtype == "price_forecast" and any(r.get("text_role") == "key" for r in labels):
            # A valid price tile must retain its plot and the complete interval, even at a forced growth cap.
            if not any(r["kind"] == "obj" and r["y2"] - r["y1"] + 1 >= 14 for r in surfaces):
                problems.append(f"{where}: price plot is missing or too short")
            for label in labels:
                if ":" in label["text"] and label.get("text_clipped"):
                    problems.append(f"{where}: price interval '{label['text']}' is clipped")
        for rec in labels + [r for r in recs if r["kind"] not in ("card", "obj", "label")]:
            if not contains(card, rec):
                text = rec.get("text", rec["kind"])
                problems.append(f"{where}: '{text}' reaches outside the tile "
                                f"({rec['x1']},{rec['y1']})-({rec['x2']},{rec['y2']})")
        for i, a in enumerate(labels):
            for b in labels[i + 1:]:
                if overlap(a, b):
                    problems.append(f"{where}: text '{a['text']}' overlaps '{b['text']}'")
            for btn in buttons:
                if overlap(a, btn) and not inside(a, btn):
                    problems.append(f"{where}: text '{a['text']}' overlaps a button")
            for surface in surfaces:
                if overlap(a, surface) and not inside(a, surface):
                    problems.append(f"{where}: text '{a['text']}' partly covered by a {surface['kind']} "
                                    f"({surface['x1']},{surface['y1']})-({surface['x2']},{surface['y2']})")
    return problems


def case_fixture(gallery_fixture):
    """One page per edge case: a copy of the named gallery tile with the case's size and fields."""
    tiles = {page["widgets"][0].get("gallery_title", page["widgets"][0]["title"]): page["widgets"][0]
             for page in gallery_fixture["pages"] if page["widgets"]}
    for title, wtype, entity in [("Camera", "camera_tile", "camera.demo_preview"),
                                 ("Todo list", "todo_list", "todo.demo_list"), ("Empty card", "empty_tile", "")]:
        tiles[title] = {"type": wtype, "title": title, "entity_id": entity, "rect": {"x": 10, "y": 10, "w": 300, "h": 240}}
    pages = []
    for index, case in enumerate(json.loads(CASES.read_text(encoding="utf-8"))["cases"]):
        widget = json.loads(json.dumps(tiles[case["tile"]]))
        widget.update(case.get("fields", {}))
        widget["id"] = f"case_{index}"
        widget["rect"].update(w=case["w"], h=case["h"])
        label = f"{case['tile']} {case['w']}x{case['h']}"
        if case.get("fields"):
            label += " " + ",".join(f"{k}={v}" for k, v in case["fields"].items())
        widget["title"] = case.get("name", case["tile"])
        pages.append({"id": widget["id"], "title": label, "widgets": [widget]})
    return dict(gallery_fixture, pages=pages)


def render_dumps(binary, fixture_path, page_ids, out_dir, extra_args=()):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    jobs = [(page, style, language, Path(out_dir) / f"{page}_{style}_{language}.jsonl")
            for style, language in PASSES for page in page_ids]

    def run(job):
        page, style, language, path = job
        subprocess.run([str(binary), "--fixture", str(fixture_path), "--page", page, "--style", style,
                        "--theme", "dark_v2", "--language", language, "--exit-after-ms", "300",
                        "--dump-layout", str(path), *extra_args], env=env, check=True, capture_output=True)
        label = style if language == "en" else f"{style} {language}"
        return (f"{page}/{label}: ", path)

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        return list(pool.map(run, jobs))


def check_dumps(dumps, min_px=0):
    """Problems from all dumps, each reported once with the styles it appears in."""
    found = {}
    for context, path in dumps:
        page, style = context.rstrip(": ").split("/")
        lines = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line]
        for problem in find_problems(lines, min_px=min_px):
            found.setdefault(f"{page}: {problem}", []).append(style)
    return [f"{problem} [{', '.join(styles)}]" for problem, styles in found.items()]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fixture", type=Path, help="simulator fixture (default: the gallery fixture)")
    parser.add_argument("--variant", default="panel4", choices=("panel4", "panel10", "paneljc", "paneljc-v2", "tab5"),
                        help="panel geometry and font mapping to check (default: panel4)")
    parser.add_argument("--text-step", type=int, choices=(0, 1, 2), default=0,
                        help="user text size step to render (Standard, Larger, Largest; default 0)")
    parser.add_argument("--text-cap", type=int, choices=(0, 1, 2, 3),
                        help="force the tile growth cap of every tile (calibration; default: the calibrated cap per tile)")
    parser.add_argument("--rotation", type=int, choices=(0, 90, 180, 270),
                        help="display rotation (90/270 require the tab5 variant)")
    args = parser.parse_args()
    extra_args = ["--text-step", str(args.text_step)] + (["--text-cap", str(args.text_cap)] if args.text_cap is not None else [])
    if args.rotation is not None:
        extra_args += ["--rotation", str(args.rotation)]

    sys.path.insert(0, str(REPO / "tools"))
    from generate_simulator_gallery import DATA, build_simulator, prepare_fixture

    binary = build_simulator(args.variant)
    with tempfile.TemporaryDirectory(prefix="betta_layout_") as temp:
        if args.fixture:
            fixtures = {"custom": json.loads(args.fixture.read_text(encoding="utf-8"))}
        else:
            _, gallery = prepare_fixture(json.loads(DATA.read_text(encoding="utf-8")))
            fixtures = {"gallery": gallery, "cases": case_fixture(gallery),
                        "matte_reference": json.loads(MATTE_REFERENCE.read_text(encoding="utf-8")),
                        "energy_timeline": json.loads((REPO / "tests/simulator/energy_timeline.json").read_text()),
                        "room_extra": json.loads((REPO / "tests/simulator/room_extra.json").read_text()),
                        "bambu_printer": json.loads((REPO / "tests/simulator/bambu_printer.json").read_text()),
                        "connectivity_ha": json.loads((REPO / "tests/simulator/connectivity_ha.json").read_text()),
                        "connectivity_wifi": json.loads((REPO / "tests/simulator/connectivity_wifi.json").read_text()),
                        }
            if args.variant == "panel4":  # the readability fixtures use panel4's 720x600 pages and its role sizes
                fixtures["readability"] = json.loads((REPO / "tests/simulator/readability.json").read_text())
                for kind in ("numeric", "state"):
                    fixtures[f"readability_history_{kind}"] = json.loads(
                        (REPO / f"tests/simulator/readability_history_{kind}.json").read_text())
        problems = []
        for name, fixture in fixtures.items():
            fixture_path = Path(temp) / f"{name}.json"
            fixture_path.write_text(json.dumps(fixture, ensure_ascii=False), encoding="utf-8")
            titles = {page["id"]: page.get("title", page["id"]) for page in fixture["pages"]}
            dumps = render_dumps(binary, fixture_path, list(titles), temp, extra_args)
            if name.startswith("readability"):
                for context, path in dumps:
                    records = [json.loads(line) for line in path.read_text().splitlines() if line]
                    problems += [context + issue for issue in readability_problems(records)]
                    problems += [context + issue for issue in coverage_problems(records, context.split("/", 1)[0])]
            if name.startswith("connectivity_"):
                for context, path in dumps:
                    records = [json.loads(line) for line in path.read_text().splitlines() if line]
                    problems += [f"{context}: connection detail '{record.get('text', '')}' is clipped"
                                 for record in records if record.get("text_clipped")]
            # Name an edge case by its tile, size and fields rather than its page id.
            problems += [problem.replace(f"{page}: ", f"{titles[page]}: ", 1) if page in titles else problem
                         for problem in check_dumps(dumps, MIN_TEXT_PX.get(args.variant, 0))
                         for page in [problem.split(":", 1)[0]]]
    for problem in problems:
        print(problem)
    print(f"widget layout: {'ok' if not problems else f'{len(problems)} problem(s)'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
