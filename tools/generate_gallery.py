#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Regenerate docs/gallery.md and its screenshots from a live panel.

Pushes a throwaway test layout (a handful of pages, one tile per widget type/
display variant) to a real panel, cycles the appearance through flat/glow/
glass/matte, screenshots each page once per style and crops out each tile's own
rectangle, then restores the panel's original layout and style. The result
is a Markdown gallery for Zensical of widgets and special pages in all four looks.

Needs a panel4 (the default variant we build; pass --content-box for another
variant's geometry from main/app_config.h) already running the current
firmware, and a local, gitignored entity map (see
tools/gallery_entities.example.json) naming real entities from your
own Home Assistant for each widget slot -- those entity ids are personal and
never checked in; every generated screenshot, including the two special
pages (--special-pages, on by default: House and Energy dashboards) and a
doorbell popup, uses this script's own generic titles/room names, never a real Home Assistant
area name (those only appear on a room actually imported through the
editor's "import from areas", which this script does not use).

Usage:
    tools/generate_gallery.py <panel-ip> [--entities FILE] [--password PASS]

The gallery is normally rendered without a panel by
tools/generate_simulator_gallery.py; a capture on a real panel remains for
comparing the simulator with the hardware.

Safety: the panel's current layout and appearance style are saved to
--backup-dir (default: a temp dir, path printed) before anything is
changed, and restored again at the end even if the script fails partway
(a KeyboardInterrupt or an exception both hit the same restore path). If the
script is killed outright, restore by hand:
    curl -X PUT <panel>/api/layout --data-binary @<backup-dir>/layout.json
    curl -X PUT <panel>/api/settings -d '{"ui":{"style":"<original>"}}'
"""
import argparse
import base64
import json
import re
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

from gallery_content import (DEFAULT_IMG_DIR, DEFAULT_OUT_MD, PANEL_SOURCE_NOTE, REPO_ROOT, SPECIAL_PAGES, STYLES,
                             build_layout, build_spec, collect_images, render_markdown, tile_ids)
from ui_styles import FALLBACK

try:
    from PIL import Image
except ImportError:  # the panel capture needs it
    Image = None

DEFAULT_ENTITIES = Path(__file__).resolve().parent / "gallery_entities.json"
EXAMPLE_ENTITIES = Path(__file__).resolve().parent / "gallery_entities.example.json"

DEFAULT_ENTITIES = Path(__file__).resolve().parent / "gallery_entities.json"
EXAMPLE_ENTITIES = Path(__file__).resolve().parent / "gallery_entities.example.json"

# panel4's content box (main/app_config.h): screen 720x720, content box
# x=0 y=60 w=720 h=600. Pass --content-box x,y,w,h for another variant.
DEFAULT_CONTENT_BOX = (0, 60, 720, 600)


def http(method, url, payload=None, password=None, timeout=20, raw=False):
    data = None
    headers = {}
    if payload is not None:
        data = payload if raw else json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/octet-stream" if raw else "application/json"
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    if password:
        token = base64.b64encode(f"admin:{password}".encode("utf-8")).decode("ascii")
        req.add_header("Authorization", f"Basic {token}")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def get_json(url, password=None):
    return json.loads(http("GET", url, password=password))


def put_json(url, payload, password=None):
    return http("PUT", url, payload, password=password)


def post_json(url, payload, password=None):
    return http("POST", url, payload, password=password)


# ---- widget slot -> HA entity map -----------------------------------------

def load_entities(path):
    if not path.exists():
        sys.exit(
            f"Entity map not found: {path}\n"
            f"Copy {EXAMPLE_ENTITIES.relative_to(REPO_ROOT)} to "
            f"{path.relative_to(REPO_ROOT)} and fill in real entity ids from your own "
            "Home Assistant (never commit that file -- tools/* is gitignored by default)."
        )
    with open(path, encoding="utf-8") as f:
        entities = json.load(f)
    # camera_tile and todo_list are deliberately absent: see the example file's _camera_tile_note
    # and _todo_list_note.
    required = [
        "sensor", "button", "slider", "gauge", "graph", "light_tile", "heating_tile", "weather", "solar_forecast",
        "car_widget", "media_player", "sonos_favorites", "cover_tile", "fan_tile", "contact_tile", "climate_tile",
        "scene_tile", "select_tile", "lock_tile", "alarm_tile", "binary_tile", "room_tile", "vacuum_tile",
    ]
    missing = [k for k in required if k not in entities]
    if missing:
        sys.exit(f"{path} is missing entries for: {', '.join(missing)} (see the .example.json)")
    return entities


# ---- capture -----------------------------------------------------------

def screenshot(panel, password, out_path):
    data = http("GET", f"http://{panel}/api/screenshot.bmp", password=password, timeout=30)
    img = Image.open(__import__("io").BytesIO(data))
    img.save(out_path)
    return img


def capture_all(panel, password, spec, content_box, img_dir, styles):
    cb_x, cb_y, _, _ = content_box
    images = {}  # (style, gallery_id) -> Path
    for style in styles:
        print(f"-- style: {style}")
        # "reboot": false is required here -- without it the panel's default behavior on any
        # settings PUT is a full device restart (api_settings.c schedule_restart()), which the
        # 4 s sleep below is nowhere near long enough for (30+ s to reconnect Wi-Fi and
        # resubscribe ~130 HA entities), causing the next navigate/screenshot call to time out.
        # With "reboot": false the UI task re-reads the style live (EV_LAYOUT_UPDATED) instead.
        put_json(f"http://{panel}/api/settings", {"ui": {"style": style}, "reboot": False}, password)
        time.sleep(4)  # a style change restyles every tile in place; give it room to settle
        # before the first page nav below, or the first page captured after a style change
        # can catch a tile between its old and new state (seen as a spurious "unavailable").
        for page_spec in spec:
            post_json(f"http://{panel}/api/navigate", {"page": page_spec["page"]}, password)
            time.sleep(2.5)  # let LVGL restyle and redraw before the shot
            full = img_dir / f"_full_{style}_{page_spec['page']}.png"
            img = screenshot(panel, password, full)
            all_ids = tile_ids(spec)
            idx_base = sum(len(p["tiles"]) for p in spec[: spec.index(page_spec)])
            for i, tile in enumerate(page_spec["tiles"]):
                if tile["fields"] is None:
                    continue  # nothing was pushed for this tile, see build_layout()
                x, y, w, h = tile["rect"]
                box = (cb_x + x, cb_y + y, cb_x + x + w, cb_y + y + h)
                crop = img.crop(box)
                gallery_id = all_ids[idx_base + i]
                out_path = img_dir / f"{gallery_id}_{style}.png"
                crop.save(out_path)
                images[(style, gallery_id)] = out_path
            full.unlink()
    return images


def build_special_layout(entities):
    room = entities["room_tile"]
    # house_dashboard stores each room's own name (never fetched live from Home Assistant at
    # render time, only when the editor's "import from areas" is used), so this demo layout
    # gets two generic names of its own rather than any real Home Assistant area -- unlike a
    # room actually imported through the editor, nothing here identifies a real home.
    house_page = {
        "id": "gallery_house",
        "type": "house_dashboard",
        "widgets": [],
        "house": {
            # A few rooms across three floors (not just one of each) so the floor headers and
            # the grid actually show what a real house-sized dashboard looks like, not just the
            # bare minimum needed to prove the page renders.
            "rooms": [
                {"name": "Living Room", "floor": "Ground floor", "entity_id": room["entity_id"],
                 "room_entities": room["room_entities"]},
                {"name": "Kitchen", "floor": "Ground floor", "entity_id": entities["sensor"],
                 "room_entities": [entities["light_tile"], entities["contact_tile"]]},
                {"name": "Bedroom", "floor": "Upper floor", "entity_id": entities["heating_tile"],
                 "room_entities": [entities["light_tile"], entities["contact_tile"]]},
                {"name": "Office", "floor": "Upper floor", "entity_id": entities["climate_tile"],
                 "room_entities": [entities["binary_tile"]]},
                {"name": "Storage", "floor": "Basement", "entity_id": entities["sensor"],
                 "room_entities": [entities["contact_tile"]]},
            ],
        },
    }
    energy_page = {"id": "gallery_energy", "type": "energy_dashboard", "widgets": []}
    return {"version": 1, "pages": [house_page, energy_page], "alerts": []}


def capture_special_pages(panel, password, entities, content_box, img_dir, styles):
    cb_x, cb_y, cb_w, cb_h = content_box
    layout = build_special_layout(entities)
    put_json(f"http://{panel}/api/layout", layout, password)
    time.sleep(5)
    images = {}
    for style in styles:
        put_json(f"http://{panel}/api/settings", {"ui": {"style": style}, "reboot": False}, password)
        time.sleep(4)
        for page_id, _page_type, _title, _caps in SPECIAL_PAGES:
            post_json(f"http://{panel}/api/navigate", {"page": page_id}, password)
            time.sleep(2.5)
            out_path = img_dir / f"{page_id}_{style}.png"
            img = screenshot(panel, password, out_path)
            img.crop((cb_x, cb_y, cb_x + cb_w, cb_y + cb_h)).save(out_path)
            images[(style, page_id)] = out_path
    return images


def capture_doorbell_popup(panel, password, img_dir, styles):
    """Show the same notification card used by layout-configured doorbell alerts."""
    images = {}
    post_json(f"http://{panel}/api/navigate", {"page": "gallery_house"}, password)
    for style in styles:
        put_json(f"http://{panel}/api/settings", {"ui": {"style": style}, "reboot": False}, password)
        time.sleep(4)
        post_json(f"http://{panel}/api/notifications", {
            "title": "Doorbell",
            "message": "Someone is at the door.",
            "message_id": "gallery_doorbell_preview",
            "action_entity_id": "button.gallery_preview_only",
            "action_label": "Open door",
            "timeout_sec": 6,
        }, password)
        time.sleep(1)
        out_path = img_dir / f"gallery_doorbell_{style}.png"
        screenshot(panel, password, out_path)
        images[(style, "gallery_doorbell")] = out_path
        time.sleep(6)  # let the preview close before changing style
    return images


# ---- main ----------------------------------------------------------------

def main():
    if Image is None:
        sys.exit("Pillow is required: pip install pillow")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("panel", help="panel IP or hostname")
    ap.add_argument("--entities", type=Path, default=DEFAULT_ENTITIES)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT_MD)
    ap.add_argument("--img-dir", type=Path, default=DEFAULT_IMG_DIR)
    ap.add_argument("--backup-dir", type=Path, default=None)
    ap.add_argument("--password", default=None, help="BETTA_PASSWORD, if the panel has an admin password set")
    ap.add_argument("--content-box", default=None, help="x,y,w,h override, e.g. for paneljc: 0,60,1280,680")
    ap.add_argument("--styles", default=",".join(STYLES))
    ap.add_argument("--special-pages", dest="special_pages", action="store_true", default=True,
                     help="also capture the House/Energy dashboard pages (default: on)")
    ap.add_argument("--no-special-pages", dest="special_pages", action="store_false",
                    help="skip House/Energy pages and the doorbell popup")
    args = ap.parse_args()

    content_box = DEFAULT_CONTENT_BOX
    if args.content_box:
        content_box = tuple(int(v) for v in args.content_box.split(","))
    styles = args.styles.split(",")

    entities = load_entities(args.entities)
    args.img_dir.mkdir(parents=True, exist_ok=True)
    backup_dir = args.backup_dir or Path(tempfile.mkdtemp(prefix="betta_gallery_backup_"))
    backup_dir.mkdir(parents=True, exist_ok=True)

    print(f"Backing up {args.panel}'s layout and style to {backup_dir}")
    original_layout = get_json(f"http://{args.panel}/api/layout", args.password)
    original_settings = get_json(f"http://{args.panel}/api/settings", args.password)
    original_style = original_settings.get("ui", {}).get("style", FALLBACK)
    (backup_dir / "layout.json").write_text(json.dumps(original_layout, indent=2))
    (backup_dir / "style.txt").write_text(original_style)

    spec = build_spec(entities)
    gallery_layout = build_layout(spec)

    source_note = PANEL_SOURCE_NOTE

    try:
        images = collect_images(args.img_dir, spec, styles)
        print("Pushing the gallery layout...")
        put_json(f"http://{args.panel}/api/layout", gallery_layout, args.password)
        print("Waiting for Home Assistant subscriptions on the new layout to settle...")
        time.sleep(5)  # a freshly pushed layout resubscribes all its entities over the HA
        # websocket; grabbing a screenshot too soon shows a brand-new widget's "unavailable"
        # placeholder instead of its real first state.
        images.update(capture_all(args.panel, args.password, spec, content_box, args.img_dir, styles))
        if args.special_pages:
            print("Capturing the House/Energy dashboard pages...")
            images.update(capture_special_pages(args.panel, args.password, entities, content_box, args.img_dir, styles))
            print("Capturing the doorbell popup...")
            images.update(capture_doorbell_popup(args.panel, args.password, args.img_dir, styles))
        else:
            print("Skipping House/Energy dashboard pages and doorbell popup (--no-special-pages).")
        markdown = render_markdown(spec, images, source_note)
        args.out.write_text(markdown)
        print(f"Wrote {args.out} and {len(images)} images under {args.img_dir}")
    finally:
        print(f"Restoring the original layout and style ({original_style})...")
        try:
            put_json(f"http://{args.panel}/api/layout", original_layout, args.password)
            put_json(f"http://{args.panel}/api/settings", {"ui": {"style": original_style}, "reboot": False}, args.password)
        except urllib.error.URLError as exc:
            print(f"Restore failed ({exc}); restore by hand from {backup_dir}", file=sys.stderr)
            raise


if __name__ == "__main__":
    main()
