#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Render the whole gallery locally and regenerate docs/gallery.md for Zensical.

--variant <board> renders only the whole-page views (the house and energy pages, which the firmware lays out
for the board's screen) at that board's geometry; the single widgets are shown at 4-inch size only.

No device or Home Assistant connection is made. The synthetic fixture lives in
tests/simulator/gallery_data.json: the widget tiles, the House page (rooms and
floors as an import from Home Assistant areas lays them out), the Energy page on
live sensors and the doorbell popup with a placeholder camera picture.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from datetime import date, timedelta

from gallery_freshness import REPO, SOURCES_JSON, write_stamp
from gallery_content import (DEFAULT_IMG_DIR, DEFAULT_OUT_MD, SIMULATOR_SOURCE_NOTE,
                             STYLES, NAME_VISIBILITY_EXAMPLES, build_layout, build_spec, collect_images, render_markdown)


REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "tests/simulator/gallery_data.json"
DEMO_PAGES = REPO / "tests/simulator/gallery_pages.json"
DEMO_KEY = {"panel10": "wide", "paneljc": "wide", "paneljc-v2": "wide", "tab5": "tab5"}


def check_run(argv, **kwargs):
    result = subprocess.run(argv, cwd=REPO, text=True, capture_output=True, check=False, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{' '.join(map(str, argv))} failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout + result.stderr


TILE_X = 10
TILE_Y = 10
TOP_BAR_H = 60  # APP_CONTENT_BOX_Y in main/app_config.h, the same on every board

# Screen width, screen height and content box height per simulator variant (main/app_config.h). The tiles keep
# their gallery sizes (all fit the smallest content box); the house and energy pages and the full-screen
# popups follow the board.
GEOMETRY = {
    "panel4": (720, 720, 600),
    "panel10": (1280, 800, 680),
    "paneljc": (1280, 800, 680),
    "paneljc-v2": (1280, 800, 680),
    "tab5": (1280, 720, 600),
}


def demo_page(fixture, variant):
    """The whole-page demo layout of a board (gallery_pages.json), built from the gallery tiles it names;
    None for a board without one (panel4 dashboard examples live in docs/pages.md)."""
    key = DEMO_KEY.get(variant)
    if key is None:
        return None
    by_title = {page["title"]: page["widgets"][0] for page in fixture["pages"] if page["widgets"]}
    widgets = []
    for index, item in enumerate(json.loads(DEMO_PAGES.read_text(encoding="utf-8"))[key], start=1):
        widgets.append(dict(by_title[item["ref"]], id=f"gallery_demo_{index}", rect=dict(item["rect"])))
    return {"id": "gallery_demo", "title": "Demo", "widgets": widgets, "whole_page": True}


def prepare_fixture(data, variant="panel4"):
    """The simulator fixture: every gallery tile alone on its own page at (TILE_X, TILE_Y), so a
    tile can have any size up to the content area; the page packing of build_spec() only matters
    for a capture on a real panel. The packed first page is kept as the doorbell's background."""
    spec = build_spec(data["entities"])
    packed = build_layout(spec)
    fixture = {"version": 1, "alerts": [], "pages": []}
    for page in packed["pages"]:
        for widget in page["widgets"]:
            tile = dict(widget, rect=dict(widget["rect"], x=TILE_X, y=TILE_Y))
            fixture["pages"].append({"id": widget["id"], "title": widget.get("gallery_title", widget["title"]),
                                     "widgets": [tile]})
    for page in fixture["pages"]:
        if any(widget["type"] in ("bambu_printer", "wallbox_evcc") for widget in page["widgets"]):
            page["ha_online"] = True  # a connected, synchronised panel: the command buttons are enabled
    for key, title in NAME_VISIBILITY_EXAMPLES:
        original = next(page["widgets"][0] for page in fixture["pages"] if page["title"] == title)
        widget = dict(original, id=f"gallery_name_off_{key}", show_name=False)
        fixture["pages"].append({"id": widget["id"], "title": f"{title} without name", "widgets": [widget]})
    fixture["doorbell_background"] = packed["pages"][0]
    # The special pages: a house page laid out from rooms as the editor's import from Home Assistant
    # areas creates them, and an energy page on live sensors.
    fixture["pages"].append({"id": "gallery_house", "type": "house_dashboard", "title": "House",
                             "widgets": [], "house": data["house"]})
    fixture["pages"].append({"id": "gallery_energy", "type": "energy_dashboard", "title": "Energy",
                             "widgets": [], "energy": data["energy"]})
    demo = demo_page(fixture, variant)
    if demo is not None:
        fixture["pages"].append(demo)
    fixture["states"] = data["states"]
    for state in fixture["states"]:
        picture = state.get("attributes", {}).get("entity_picture", "")
        if picture.startswith("file:"):
            # Local placeholder pictures live next to the fixture; the simulator loads them.
            state["attributes"]["entity_picture"] = "file:" + str(DATA.parent / picture[len("file:"):])
    today = date.today()
    for state in fixture["states"]:
        forecast = state.get("attributes", {}).get("forecast", [])
        for day in forecast:
            offset = day.pop("date_offset_days", None)
            if offset is not None:
                day["date"] = (today + timedelta(days=offset)).isoformat()
    fixture["doorbell_background"]["title"] = "HOME"
    for page in fixture["pages"] + [fixture["doorbell_background"]]:
        for widget in page["widgets"]:
            if widget["type"] == "graph":
                widget["history"] = data["graph_history"]
    return spec, fixture


def build_simulator(variant="panel4"):
    build_dir = REPO / "build/simulator" / variant
    check_run(["cmake", "-S", "tests/simulator", "-B", str(build_dir), f"-DSIM_VARIANT={variant}",
               "-DCMAKE_BUILD_TYPE=Debug"])
    check_run(["cmake", "--build", str(build_dir), "--parallel"])
    return build_dir / "betta-widget-sim"


def render_images(spec, fixture, output_dir, variant="panel4"):
    """Renders every widget image; returns (count, layout problems from check_widget_layout.py)."""
    from PIL import Image
    from check_widget_layout import MIN_TEXT_PX, check_dumps

    binary = build_simulator(variant)
    output_dir.mkdir(parents=True, exist_ok=True)
    count = 0
    dumps = []
    with tempfile.TemporaryDirectory(prefix="betta_sim_gallery_") as temp:
        fixture_path = Path(temp) / "gallery.json"
        fixture_path.write_text(json.dumps(fixture, ensure_ascii=False), encoding="utf-8")
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "dummy"
        for style in STYLES if variant == "panel4" else ():  # the single widgets are shown at 4-inch size only
            for page in fixture["pages"]:
                if page.get("type") or page.get("whole_page"):
                    continue  # whole pages: render_special_pages()
                widget = page["widgets"][0]
                bmp = Path(temp) / f"{page['id']}_{style}.bmp"
                layout = Path(temp) / f"{page['id']}_{style}.jsonl"
                output = check_run([str(binary), "--fixture", str(fixture_path), "--page", page["id"],
                                    "--theme", "dark_v2", "--style", style, "--language", "en",
                                    "--screenshot", str(bmp), "--dump-layout", str(layout),
                                    "--exit-after-ms", "300"], env=env)
                dumps.append((f"{page['id']}/{style}: ", layout))
                if "1 widget(s) rendered, 0 skipped" not in output:
                    raise RuntimeError(f"Incomplete render of {page['id']} {style}: {output}")
                w, h = widget["rect"]["w"], widget["rect"]["h"]
                with Image.open(bmp) as full:
                    full.crop((TILE_X, 60 + TILE_Y, TILE_X + w, 60 + TILE_Y + h)).save(
                        output_dir / f"{widget['id']}_{style}.png")
                count += 1
        if variant == "panel4":
            count += render_doorbell(binary, fixture, output_dir, Path(temp), env)
            count += render_history_popups(binary, fixture, output_dir, Path(temp), env)
            count += render_select_popup(binary, fixture, output_dir, Path(temp), env, dumps)
            count += render_glow_popups(binary, fixture, output_dir, Path(temp), env, dumps)
        count += render_special_pages(binary, fixture_path, fixture, output_dir, Path(temp), env, dumps, GEOMETRY[variant])
        problems = check_dumps(dumps, MIN_TEXT_PX.get(variant, 0))
    return count, problems


def render_special_pages(binary, fixture_path, fixture, output_dir, temp, env, dumps, geometry=GEOMETRY["panel4"]):
    """The house and energy pages: the whole content area below the top bar."""
    from PIL import Image

    screen_w, _, content_h = geometry
    count = 0
    for page in (p for p in fixture["pages"] if p.get("type") or p.get("whole_page")):
        for style in STYLES:
            bmp = temp / f"{page['id']}_{style}.bmp"
            layout = temp / f"{page['id']}_{style}.jsonl"
            check_run([str(binary), "--fixture", str(fixture_path), "--page", page["id"],
                       "--theme", "dark_v2", "--style", style, "--language", "en",
                       "--screenshot", str(bmp), "--dump-layout", str(layout), "--exit-after-ms", "400"], env=env)
            dumps.append((f"{page['id']}/{style}: ", layout))
            with Image.open(bmp) as full:
                full.convert("RGB").crop((0, TOP_BAR_H, screen_w, TOP_BAR_H + content_h)).save(output_dir / f"{page['id']}_{style}.png")
            count += 1
    return count


def render_doorbell(binary, fixture, output_dir, temp, env):
    """The doorbell notification card with the fake camera over the first page, full screen."""
    from PIL import Image

    doorbell = json.loads(DATA.read_text(encoding="utf-8")).get("doorbell")
    if not doorbell:
        return 0
    examples = {
        "doorbell": doorbell,
        "notification_bold": json.loads((REPO / "tests/simulator/notification_bold.json").read_text())["notification"],
        "notification": {"title": "Laundry", "message": "The washing machine is done.",
                         "message_id": "laundry_done", "timeout_sec": 30},
    }
    for key, notification in examples.items():
        fixture_path = temp / f"{key}.json"
        background = dict(fixture, pages=[fixture["doorbell_background"]], notification=notification)
        fixture_path.write_text(json.dumps(background, ensure_ascii=False), encoding="utf-8")
        for style in STYLES:
            bmp = temp / f"{key}_{style}.bmp"
            check_run([str(binary), "--fixture", str(fixture_path), "--page", fixture["doorbell_background"]["id"],
                       "--theme", "dark_v2", "--style", style, "--language", "en",
                       "--screenshot", str(bmp), "--exit-after-ms", "400"], env=env)
            with Image.open(bmp) as full:
                full.convert("RGB").save(output_dir / f"gallery_{key}_{style}.png")
    return len(examples) * len(STYLES)


def render_select_popup(binary, fixture, output_dir, temp, env, dumps):
    """Open the real option picker with a synthetic tap; no option is chosen."""
    from PIL import Image

    page = next(page for page in fixture["pages"] if any(w["type"] == "select_tile" for w in page["widgets"]))
    widget = page["widgets"][0]
    fixture_path = temp / "select_detail.json"
    fixture_path.write_text(json.dumps(dict(fixture, pages=[page], tap_widget=widget["id"]),
                                       ensure_ascii=False), encoding="utf-8")
    for style in STYLES:
        bmp = temp / f"select_detail_{style}.bmp"
        layout = temp / f"select_detail_{style}.jsonl"
        check_run([str(binary), "--fixture", str(fixture_path), "--page", page["id"],
                   "--theme", "dark_v2", "--style", style, "--language", "en",
                   "--screenshot", str(bmp), "--dump-layout", str(layout), "--exit-after-ms", "400"], env=env)
        dumps.append((f"select_detail/{style}: ", layout))
        with Image.open(bmp) as full:
            full.convert("RGB").save(output_dir / f"gallery_select_detail_{style}.png")
    return len(STYLES)


def render_glow_popups(binary, fixture, output_dir, temp, env, dumps):
    """Popups that only their widget guide shows, once, in the default Glow style: the Bambu card's read-only status popup
    (opened by a synthetic tap on the real button, --tap; nothing is sent or confirmed) and the vacuum's recent-activity popup."""
    from PIL import Image

    count = 0
    bambu = next((p for p in fixture["pages"] if any(w["type"] == "bambu_printer" for w in p["widgets"])), None)
    if bambu is not None:
        widget = bambu["widgets"][0]
        w, h = widget["rect"]["w"], widget["rect"]["h"]
        button_y = 60 + TILE_Y + h - 12 - 25  # centre of the footer buttons: 12 px padding, 50 px high at the 4-inch size
        path = temp / "bambu_popup.json"
        path.write_text(json.dumps(dict(fixture, pages=[bambu]), ensure_ascii=False), encoding="utf-8")
        bmp, layout = temp / "bambu_detail.bmp", temp / "bambu_detail.jsonl"
        check_run([str(binary), "--fixture", str(path), "--page", bambu["id"], "--theme", "dark_v2", "--style", "glow",
                   "--language", "en", "--tap", f"{TILE_X + w // 2},{button_y}", "--screenshot", str(bmp),
                   "--dump-layout", str(layout), "--exit-after-ms", "400"], env=env)
        dumps.append(("bambu_detail/glow: ", layout))
        with Image.open(bmp) as full:
            full.convert("RGB").save(output_dir / "gallery_bambu_detail_glow.png")
        count += 1
    vacuum = next((p for p in fixture["pages"] if any(w["type"] == "vacuum_tile" for w in p["widgets"])), None)
    if vacuum is not None:
        popup = dict(fixture, pages=[vacuum], history_popup={
            "entity_id": "vacuum.demo_robot", "title": "Robot vacuum", "current": "Docked", "numeric": False,
            "device_class": ""})
        path = temp / "vacuum_history.json"
        path.write_text(json.dumps(popup, ensure_ascii=False), encoding="utf-8")
        bmp = temp / "vacuum_history.bmp"
        check_run([str(binary), "--fixture", str(path), "--page", vacuum["id"], "--theme", "dark_v2", "--style", "glow",
                   "--language", "en", "--screenshot", str(bmp), "--exit-after-ms", "400"], env=env)
        with Image.open(bmp) as full:
            full.convert("RGB").save(output_dir / "gallery_vacuum_history_glow.png")
        count += 1
    for name in ("disconnected", "sessions"):
        path = REPO / "tests/simulator" / f"wallbox_{name}.json"
        bmp, layout = temp / f"wallbox_{name}.bmp", temp / f"wallbox_{name}.jsonl"
        check_run([str(binary), "--fixture", str(path), "--page", "wallbox", "--theme", "dark_v2", "--style", "glow",
                   "--language", "en", "--screenshot", str(bmp), "--dump-layout", str(layout), "--exit-after-ms", "400"], env=env)
        dumps.append((f"wallbox_{name}/glow: ", layout))
        with Image.open(bmp) as full:
            image = full.convert("RGB")
            if name == "disconnected":
                widget = json.loads(path.read_text())["pages"][0]["widgets"][0]
                rect = widget["rect"]
                image = image.crop((rect["x"], TOP_BAR_H+rect["y"], rect["x"]+rect["w"], TOP_BAR_H+rect["y"]+rect["h"]))
            image.save(output_dir / f"gallery_wallbox_{name}_glow.png")
        count += 1
    return count


def render_history_popups(binary, fixture, output_dir, temp, env):
    """Render shared numeric, contact and lock Recorder popups in both UI languages."""
    from PIL import Image

    examples = [
        ("sensor_en", "Sensor", "sensor.demo_room_temperature", "Room temperature", "21.8 °C", True, "", "en"),
        ("contact_de", "Contact", "binary_sensor.demo_front_door", "Haustür", "Geschlossen", False, "door", "de"),
        ("lock_en", "Front door", "lock.demo_front_door", "Front door", "Locked", False, "lock", "en"),
        # the 7-day view: the same popup after a tap on its "7 days" button (panel4 geometry, reference text size)
        ("sensor_week_en", "Sensor", "sensor.demo_room_temperature", "Room temperature", "21.8 °C", True, "", "en"),
        ("sensor_week_de", "Sensor", "sensor.demo_room_temperature", "Raumtemperatur", "21,8 °C", True, "", "de"),
    ]
    taps = {"sensor_week_en": "195,144", "sensor_week_de": "195,144"}
    count = 0
    for key, tile_title, entity_id, title, current, numeric, device_class, language in examples:
        page = next(page for page in fixture["pages"] if any(w["title"] == tile_title for w in page["widgets"]))
        popup_fixture = dict(fixture, pages=[page], history_popup={
            "entity_id": entity_id, "title": title, "current": current, "numeric": numeric,
            "device_class": device_class,
        })
        fixture_path = temp / f"history_{key}.json"
        fixture_path.write_text(json.dumps(popup_fixture, ensure_ascii=False), encoding="utf-8")
        for style in STYLES:
            bmp = temp / f"history_{key}_{style}.bmp"
            check_run([str(binary), "--fixture", str(fixture_path), "--page", page["id"],
                       "--theme", "dark_v2", "--style", style, "--language", language,
                       *(["--tap", taps[key]] if key in taps else []),
                       "--screenshot", str(bmp), "--exit-after-ms", "400"], env=env)
            with Image.open(bmp) as full:
                full.convert("RGB").save(output_dir / f"gallery_history_{key}_{style}.png")
            count += 1
    return count


PAGE_VIEWS = (("gallery_demo", "Demo page"), ("gallery_house", "House page"), ("gallery_energy", "Energy page"))


def write_pages_markdown(variant, img_dir, out):
    """A small Markdown page with the whole-page views of one board, one row per page, one column per style."""
    screen_w, screen_h, content_h = GEOMETRY[variant]
    rel = os.path.relpath(img_dir, out.parent)
    label = {"tab5": '5" M5Stack Tab5', "paneljc": '10.1" Guition', "paneljc-v2": '10.1" Guition (v2)', "panel10": '10.1" Waveshare'}.get(variant, variant)
    lines = [
        "<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->", "",
        f"# {label}: simulator gallery", "",
             f"Rendered from the current firmware sources by the SDL simulator at {screen_w}x{screen_h} "
             f"(content box {screen_w}x{content_h}) from the synthetic fixture `tests/simulator/gallery_data.json`. "
             "The single widgets are shown at 4-inch size in [gallery.md](gallery.md). Regenerate with "
             f"`python3 tools/generate_simulator_gallery.py --variant {variant}`.", ""]
    for page_id, title in PAGE_VIEWS:
        if not (img_dir / f"{page_id}_{STYLES[0]}.png").is_file():
            continue
        lines += [f"## {title}", "", "| " + " | ".join(style.title() for style in STYLES) + " |",
                  "|" + " --- |" * len(STYLES),
                  "| " + " | ".join(f"![{title}, {style}]({rel}/{page_id}_{style}.png)" for style in STYLES) + " |", ""]
    out.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", default="panel4", choices=sorted(GEOMETRY),
                        help="board geometry to render (default panel4); another variant renders only the house and energy "
                             "pages into docs/img/gallery-<variant>/ and docs/gallery-sim-<variant>.md and leaves the "
                             "freshness record of the panel4 gallery alone")
    parser.add_argument("--img-dir", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--markdown-only", action="store_true",
                        help="rewrite Markdown from existing images without rendering or changing the freshness stamp")
    parser.add_argument("--stamp-only", action="store_true",
                        help="record the current sources as the gallery's state without rendering (the change is not visible)")
    args = parser.parse_args()
    if args.stamp_only and args.markdown_only:
        parser.error("--stamp-only and --markdown-only cannot be combined")
    default_variant = args.variant == "panel4"
    docs = DEFAULT_OUT_MD.parent
    if args.img_dir is None:
        args.img_dir = DEFAULT_IMG_DIR if default_variant else DEFAULT_IMG_DIR.parent / f"gallery-{args.variant}"
    if args.out is None:
        args.out = DEFAULT_OUT_MD if default_variant else docs / f"gallery-sim-{args.variant}.md"
    if args.stamp_only:
        write_stamp()
        print(f"Recorded the current gallery sources in {SOURCES_JSON.relative_to(REPO)}")
        return 0
    start = time.perf_counter()
    data = json.loads(DATA.read_text(encoding="utf-8"))
    spec, fixture = prepare_fixture(data, args.variant)
    if args.markdown_only:
        if default_variant:
            images = collect_images(args.img_dir, spec)
            args.out.write_text(render_markdown(spec, images, SIMULATOR_SOURCE_NOTE), encoding="utf-8")
        else:
            write_pages_markdown(args.variant, args.img_dir, args.out)
        print(f"Rewrote Markdown from existing images: {args.out}")
        return 0
    count, problems = render_images(spec, fixture, args.img_dir, args.variant)
    if not default_variant:
        write_pages_markdown(args.variant, args.img_dir, args.out)
        print(f"Rendered {count} page images in {time.perf_counter() - start:.1f}s: {args.out}")
        for problem in problems:
            print(f"layout: {problem}")
        return 1 if problems else 0
    images = collect_images(args.img_dir, spec)
    # Images of tiles that no longer exist (or were renamed) would otherwise stay in the repository for good.
    keep = {path.resolve() for path in images.values()}
    stale = sorted(p for p in args.img_dir.glob("*.png") if p.resolve() not in keep)
    for path in stale:
        path.unlink()
    if stale:
        print(f"Removed {len(stale)} image(s) of tiles that are gone or renamed")
    if default_variant:
        write_stamp()
    args.out.write_text(render_markdown(spec, images, SIMULATOR_SOURCE_NOTE), encoding="utf-8")
    print(f"Rendered {count} widget images in {time.perf_counter() - start:.1f}s: {args.out}")
    for problem in problems:
        print(f"layout: {problem}")
    if problems:
        print(f"{len(problems)} widget layout problem(s); fix them before committing the gallery "
              "(tools/check_widget_layout.py)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
