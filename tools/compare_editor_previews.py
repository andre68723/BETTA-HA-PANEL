#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Put every editor tile preview next to the LVGL gallery image of the same widget.

The editor draws schematic previews (components/webui/www/js/canvas.js, tilePreviewMarkup); this tool renders each
gallery widget in the real editor against the public demo API (the same one generate_docs_examples.py uses) and writes
one image per widget with the editor on the left and the simulator on the right, plus contact sheets. It is a review
aid, not a gate: look for elements that are missing, extra, clipped, overlapping or in another language.

    uv run --locked --group screenshots python tools/compare_editor_previews.py
    uv run --locked --group screenshots python tools/compare_editor_previews.py --style matte media_player vacuum
    uv run --locked --group screenshots python tools/compare_editor_previews.py --cases Vacuum "Media player"

The default compares the gallery sizes against docs/img/gallery (run tools/generate_simulator_gallery.py first if it is
stale). --cases renders the edge cases of tests/simulator/layout_cases.json instead, the smallest and the largest
size of every tile (all of them with --all-sizes), with the LVGL simulator; naming tiles limits it to those. This finds
the sizes where the panel hides or rearranges something and the preview does not. --pages compares the whole house and
energy pages (the canvas against docs/img/gallery/gallery_house_<style>.png and gallery_energy_<style>.png).
No device and no external request.
"""
import argparse
import json
import os
import subprocess
import tempfile
import threading
from functools import partial
from http.server import SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from check_widget_layout import CASES, case_fixture
from generate_docs_examples import DemoHTTPServer, DATA, REPO, TOP_BAR_H, WWW, build_simulator, demo_api, prepare_fixture

SHEET_HEIGHT = 1500


def serve(responses):
    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            if not url.path.startswith("/api/"):
                return super().do_GET()
            if url.path not in responses:
                self.send_error(404)
                return
            payload = responses[url.path]
            query = parse_qs(url.query)
            if url.path == "/api/state" and "entity_id" in query:
                payload = {"items": [s for s in payload["items"] if s["entity_id"] == query["entity_id"][0]]}
            body = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = DemoHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(WWW)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def side_by_side(editor_png, reference_png, label):
    from PIL import Image, ImageDraw

    left, right = Image.open(editor_png).convert("RGB"), Image.open(reference_png).convert("RGB")
    image = Image.new("RGB", (left.width + right.width + 30, max(left.height, right.height) + 22), (40, 40, 40))
    ImageDraw.Draw(image).text((4, 4), f"editor | LVGL  {label}", fill=(255, 255, 255))
    image.paste(left, (10, 22))
    image.paste(right, (left.width + 20, 22))
    return image


def contact_sheets(images, output):
    from PIL import Image

    sheets, current, height = [], [], 0
    for image in images:
        if current and height + image.height > SHEET_HEIGHT:
            sheets.append(current)
            current, height = [], 0
        current.append(image)
        height += image.height + 6
    if current:
        sheets.append(current)
    for number, group in enumerate(sheets):
        sheet = Image.new("RGB", (max(i.width for i in group), sum(i.height + 6 for i in group)), (20, 20, 20))
        y = 0
        for image in group:
            sheet.paste(image, (0, y))
            y += image.height + 6
        sheet.save(output / f"sheet{number}.png")
    return len(sheets)


def pick_cases(fixture, tiles, all_sizes):
    """(page id, label) of the edge cases to compare: by default the smallest and the largest size of each tile
    that changes no field (variants are the gallery's job)."""
    cases = json.loads(CASES.read_text(encoding="utf-8"))["cases"]
    chosen = []
    for tile in sorted({c["tile"] for c in cases}):
        if tiles and tile not in tiles:
            continue
        group = [(i, c) for i, c in enumerate(cases) if c["tile"] == tile and not c.get("fields")]
        if not group:
            continue
        group.sort(key=lambda ic: ic[1]["w"] * ic[1]["h"])
        picks = group if all_sizes else sorted({group[0][0], group[-1][0]})
        chosen += [(f"case_{i}", f"{tile} {cases[i]['w']}x{cases[i]['h']}") for i in (picks if not all_sizes else [i for i, _ in picks])]
    return chosen


def render_cases(fixture, chosen, style, output):
    """LVGL images of the edge cases, cropped to the tile, keyed by page id."""
    from PIL import Image

    binary = build_simulator("panel4")
    result = {}
    with tempfile.TemporaryDirectory(prefix="betta_cases_") as temp:
        path = Path(temp) / "cases.json"
        path.write_text(json.dumps(fixture))
        for page_id, _ in chosen:
            bmp = Path(temp) / f"{page_id}.bmp"
            subprocess.run([str(binary), "--fixture", str(path), "--page", page_id, "--theme", "dark_v2", "--style", style,
                            "--language", "en", "--screenshot", str(bmp), "--exit-after-ms", "300"],
                           env=dict(os.environ, SDL_VIDEODRIVER="dummy"), check=True, capture_output=True)
            page = next(p for p in fixture["pages"] if p["id"] == page_id)
            rect = page["widgets"][0]["rect"]
            with Image.open(bmp) as image:
                cropped = image.convert("RGB").crop((rect["x"], rect["y"] + TOP_BAR_H, rect["x"] + rect["w"],
                                                      rect["y"] + TOP_BAR_H + rect["h"]))
            target = output / f"{page_id}_lvgl.png"
            cropped.save(target)
            result[page_id] = target
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("widgets", nargs="*", help="gallery widget ids (default: all)")
    parser.add_argument("--style", default="glow", choices=("flat", "glow", "glass", "matte"))
    parser.add_argument("--output", type=Path,
                        help="output directory (default: a new /private/tmp/betta-editor-preview-* directory)")
    parser.add_argument("--reference", type=Path, default=REPO / "docs/img/gallery",
                        help="directory with <id>_<style>.png from generate_simulator_gallery.py")
    parser.add_argument("--cases", action="store_true",
                        help="compare the edge cases of layout_cases.json (the widget arguments name tiles, e.g. Vacuum)")
    parser.add_argument("--pages", action="store_true", help="compare the house and energy pages as a whole")
    parser.add_argument("--all-sizes", action="store_true", help="with --cases: every size without field overrides")
    parser.add_argument("--browser-executable", help="Use an installed Chromium instead of Playwright's")
    args = parser.parse_args()
    if args.output is None:
        args.output = Path(tempfile.mkdtemp(prefix="betta-editor-preview-", dir="/private/tmp"))
    args.output.mkdir(parents=True, exist_ok=True)

    from playwright.sync_api import sync_playwright

    _, fixture = prepare_fixture(json.loads(DATA.read_text()))
    for state in fixture["states"]:
        state["age_seconds"] = 120
    fixture["frame"] = {"date": "06.10.2026", "time": "14:30"}
    chosen, lvgl = [], {}
    if args.cases:
        fixture = case_fixture(fixture)
        chosen = pick_cases(fixture, set(args.widgets), args.all_sizes)
        lvgl = render_cases(fixture, chosen, args.style, args.output)
    responses = demo_api(fixture)
    responses["/api/settings"]["ui"]["style"] = args.style
    # The gallery fixture holds one page per widget; the panel's page limit would reject the layout.
    responses["/api/version"]["max_pages"] = max(responses["/api/version"]["max_pages"], len(fixture["pages"]))
    server = serve(responses)
    problems, images = [], []
    try:
        with sync_playwright() as p:
            options = {"headless": True}
            if args.browser_executable:
                options["executable_path"] = args.browser_executable
            browser = p.chromium.launch(**options)
            page = browser.new_page(viewport={"width": 1536, "height": 864}, locale="en-GB")
            page.on("pageerror", lambda error: problems.append(f"page error: {error}"))
            page.goto(f"http://127.0.0.1:{server.server_port}", wait_until="networkidle")
            page.wait_for_function("editor.editorStarted && editor.entities.length > 0")
            targets = []
            if args.pages:
                for page_id in ("gallery_house", "gallery_energy"):
                    reference = args.reference / f"{page_id}_{args.style}.png"
                    if reference.exists():
                        targets.append((page_id, None, reference, page_id))
                    else:
                        problems.append(f"{page_id}: no gallery image {reference.name}")
            for fixture_page in ([] if args.pages else fixture["pages"]):
                for widget in fixture_page["widgets"]:
                    if args.cases:
                        label = dict(chosen).get(fixture_page["id"])
                        if label:
                            targets.append((fixture_page["id"], widget, lvgl[fixture_page["id"]], label))
                    elif not args.widgets or widget["id"] in args.widgets:
                        reference = args.reference / f"{widget['id']}_{args.style}.png"
                        if not reference.exists():
                            problems.append(f"{widget['id']}: no gallery image {reference.name}")
                            continue
                        targets.append((fixture_page["id"], widget, reference, None))
            for page_id, widget, reference, label in targets:
                page.evaluate("id => { editor.selectedPageId = id; editor.selectedWidgetId = null; renderAll(); }", page_id)
                key = widget["id"] if widget else page_id
                shot = args.output / f"{key}_editor.png"
                try:
                    tile = page.locator("#canvas") if widget is None else page.locator(f'#canvas .widget-box[data-widget-id="{key}"]')
                    tile.wait_for(timeout=3000)
                    tile.screenshot(path=str(shot))
                except Exception as error:  # noqa: BLE001 - report and go on with the next widget
                    problems.append(f"{key}: not rendered ({str(error)[:60]})")
                    continue
                caption = label or f"{key} ({widget['type']}) {widget['rect']['w']}x{widget['rect']['h']}"
                image = side_by_side(shot, reference, caption)
                image.save(args.output / f"{key}.png")
                images.append(image)
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
    sheets = contact_sheets(images, args.output) if images else 0
    print(f"{len(images)} widgets compared ({args.style}), {sheets} contact sheets in {args.output}")
    for problem in problems:
        print("  " + problem)
    raise SystemExit(1 if problems else 0)


if __name__ == "__main__":
    main()
