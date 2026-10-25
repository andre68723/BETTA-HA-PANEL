# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""What the widget gallery shows and how docs/gallery.md is written from it.

Shared by the simulator gallery (generate_simulator_gallery.py), the widget guides (generate_widget_docs.py) and the
capture from a real panel (generate_gallery.py): the tile list (build_spec), the layout built from it, the table rows
and the Markdown. Nothing here talks to a panel.
"""
import re
from pathlib import Path

from ui_styles import STYLES
from widget_cases import catalogue, widget_fields

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT_MD = REPO_ROOT / "docs" / "gallery.md"
DEFAULT_IMG_DIR = REPO_ROOT / "docs" / "img" / "gallery"

STYLE_TITLE = {"flat": "Flat", "glow": "Glow", "glass": "Glass", "matte": "Matte"}
# First table row: a short summary of each look from docs/appearance.md (keep them in step).
STYLE_SUMMARY = {
    "flat": "No shadows or gradients. Cards have a 1 px slate outline and 8 px corners, inner surfaces at most "
            "6 px, bars and sliders nearly square 4 px ends. A pressed button swaps its colours.",
    "glow": "Raised dark surfaces with a light-to-dark gradient and a soft drop shadow, heavily rounded corners and "
            "pill-shaped inset tracks. A pressed button sinks in.",
    "glass": "Frosted, translucent panels (about 12 % white tint) with a 1 px light edge on the top and left only, "
             "and faint tracks. Touch brightens the whole surface.",
    "matte": "Opaque cards with 12 px corners, no outline or shadow, a softly tinted on state, and broad, flat touch controls.",
}


GALLERY_ROLE_PATHS = {
    'temp': 'sensor',
    'switch': 'button',
    'cover': 'slider',
    'humidity': 'gauge',
    'power': 'graph',
    'light': 'light_tile',
    'climate': 'heating_tile',
    'weather': 'weather',
    'weather_report': 'weather_report',
    'media': 'media_player',
    'sonos': 'sonos_favorites',
    'gallery_cover': 'cover_tile',
    'fan': 'fan_tile',
    'door': 'contact_tile',
    'gallery_climate': 'climate_tile',
    'scene': 'scene_tile',
    'input_select': 'select_tile',
    'input_datetime': 'datetime_tile',
    'lock': 'lock_tile',
    'alarm': 'alarm_tile',
    'motion': 'binary_tile',
    'vacuum': 'vacuum_tile',
    'vacuum_map': 'vacuum_map',
    'todo': 'todo_list',
    'camera': 'camera_tile',
    'gallery_price': 'energy_price_forecast',
    'solar_remaining': 'solar_forecast.entity_id',
    'solar_today': 'solar_forecast.today',
    'solar_tomorrow': 'solar_forecast.tomorrow',
    'solar_day_3': 'solar_forecast.day_3',
    'solar_day_4': 'solar_forecast.day_4',
    'solar_day_5': 'solar_forecast.day_5',
    'solar_day_6': 'solar_forecast.day_6',
    'solar_day_7': 'solar_forecast.day_7',
    'car_battery': 'car_widget.entity_id',
    'car_precond': 'car_widget.precond_entity_id',
    'car_interior': 'car_widget.interior_temp_entity_id',
    'room_temp': 'room_tile.entity_id',
    'portfolio_value': 'parqet.entity_id',
    'portfolio_gain': 'parqet.secondary_entity_id',
    'portfolio_return': 'parqet.return_entity_id',
    'stock_1': 'stock.entity_id',
    'room_members': 'room_tile.room_entities',
}


def gallery_roles(entities):
    """Keep the device-gallery entity-map API while projecting the same roles as the test packs."""
    roles = {}
    for role, path in GALLERY_ROLE_PATHS.items():
        value = entities
        for part in path.split("."):
            value = value.get(part) if isinstance(value, dict) else None
        roles[role] = value
    roles.update(todo=entities.get("todo_list", "todo.demo_shopping"),
                 input_datetime=entities.get("datetime_tile", "input_datetime.demo_morning"),
                 grid_import=entities.get("energy_timeline", "sensor.demo_grid_import"),
                 grid_export="sensor.demo_grid_export", pv_energy="sensor.demo_solar_production",
                 battery_charge="sensor.demo_battery_charge", battery_discharge="sensor.demo_battery_discharge",
                 battery_soc="sensor.demo_battery_soc", solar_forecast_today="sensor.demo_solar_forecast_today",
                 vacuum_battery="sensor.demo_robot_battery", sonos_favorites="sensor.sonos_favorites",
                 gallery_bambu=(entities.get("bambu_printer") or {}).get("printing", {}),
                 gallery_wallbox=entities.get("wallbox_evcc", {}), gallery_parqet=entities.get("parqet", {}))
    quotes = (entities.get("stock") or {}).get("stock_entities", [])
    roles.update(gallery_stock_entities=quotes, gallery_stock_pair=quotes[:2])
    return roles


def build_spec(entities):
    """Project gallery-tagged cases; gallery_only never participates in test-pack generation."""
    data = catalogue()
    cases = [case for suite in data["suites"].values() for case in suite["cases"] if "gallery" in case]
    cases += data["gallery_only"]
    orders = sorted(case["gallery"]["order"] for case in cases)
    if orders != list(range(len(cases))):
        raise ValueError("gallery case order has a missing or duplicate tag")
    roles, pages = gallery_roles(entities), {}
    for case in sorted(cases, key=lambda case: case["gallery"]["order"]):
        meta = case["gallery"]
        if meta.get("requires") and not entities.get(meta["requires"]):
            continue
        fields = {key: value for key, value in widget_fields(case, roles).items() if key not in ("type", "title")}
        for key in meta.get("optional_fields", []):
            if not fields.get(key):
                fields.pop(key, None)
        if isinstance(fields.get("room_entities"), list):
            fields["room_entities"] = [member for value in fields["room_entities"]
                                       for member in (value if isinstance(value, list) else [value])]
        if meta.get("preview_if") and not entities.get(meta["preview_if"]):
            fields = None
        tile = {key: value for key, value in meta.items()
                if key not in ("page", "order", "requires", "preview_if", "optional_fields")}
        tile.update(type=case["type"], rect=tuple(meta["rect"]), fields=fields,
                    caps=data["gallery_caps"][meta["caps"]] if meta["caps"] is not None else None)
        pages.setdefault(meta["page"], {"page": meta["page"], "tiles": []})["tiles"].append(tile)
    return list(pages.values())


def _slug(text):
    return re.sub(r"[^a-z0-9]+", "_", str(text).lower()).strip("_")


def tile_ids(spec):
    """One stable id per tile, in spec order: the slug of its title (readable, at most 30 characters, a widget id must
    fit 32), with the slot added or a counter appended when two tiles would collide. Adding or moving a tile therefore
    no longer renames the images of the others, unlike the running number these ids used to be."""
    ids, seen = [], set()
    for page in spec:
        for tile in page["tiles"]:
            base = _slug(tile["title"])[:30] or _slug(tile["slot"])[:30]
            candidate = base
            if candidate in seen:
                candidate = f"{_slug(tile['slot'])[:14]}_{base}"[:30]
            n = 2
            while candidate in seen:
                candidate = f"{candidate[:26]}_{n}"
                n += 1
            seen.add(candidate)
            ids.append(candidate)
    return ids


def widget_json(widget_id, tile):
    x, y, w, h = tile["rect"]
    out = {
        "id": widget_id,
        "type": tile["type"],
        "title": tile.get("shown", tile["title"]),
        "rect": {"x": x, "y": y, "w": w, "h": h},
    }
    out.update(tile["fields"])
    if "shown" in tile:
        out["gallery_title"] = tile["title"]  # the tile's name in the gallery; "title" is what the widget shows
    return out


def build_layout(spec):
    pages = []
    ids = iter(tile_ids(spec))
    for page_spec in spec:
        widgets = []
        for tile in page_spec["tiles"]:
            widget_id = next(ids)
            # fields=None marks a tile with nothing safe/available to preview (a live camera
            # frame, or no entity of that domain in this Home Assistant); it still gets a row
            # in the table, just not a pushed widget or a screenshot.
            if tile["fields"] is not None:
                widgets.append(widget_json(widget_id, tile))
        pages.append({"id": page_spec["page"], "widgets": widgets})
    return {"version": 1, "pages": pages, "alerts": []}


# The two dashboard page types (main/layout/layout_validate.c's page type check) are not
# individual widgets: they auto-populate their own content (rooms by floor / energy flows)
# straight from Home Assistant, so there is nothing to place -- an empty "widgets" array is
# the whole page. house_dashboard in particular renders the real area names Home Assistant
# returns, unlike every widget tile above (which only ever show this script's own generic
# titles), so its screenshot is not anonymous the way the rest of the gallery is.
SPECIAL_PAGES = [
    ("gallery_house", "house_dashboard", "House dashboard",
     {"Domain": "n/a -- the editor can import this page's rooms from every Home Assistant "
                "area/floor in one click",
      "Options": "room cards grouped by floor, editable afterwards, up to 32 rooms",
      "Interaction": "tap a room card for its popup (see Room above)",
      "Notes": "the preview shows rooms and floors as the editor's import from Home Assistant areas "
               "lays them out, with example names -- a real import shows your own areas"}),
    ("gallery_energy", "energy_dashboard", "Energy dashboard",
     {"Source": "Home Assistant Energy dashboard or manually selected live power sensors",
      "Options": "grid, solar, home and battery flows where configured; battery state of charge, gas and water sensors are optional",
      "Interaction": "read-only",
      "Notes": "the preview uses live power sensors (grid import/export, solar, battery charge/discharge, "
               "gas and water); Home Assistant Energy fills the same page"}),
]


# ---- markdown ------------------------------------------------------------

# Where the widget images come from; the House/Energy pages and doorbell popup always need
# the real panel (render_markdown() take one of these as source_note).
PANEL_SOURCE_NOTE = ("Widgets were captured on a real panel4 against live Home Assistant entities by "
                     "`tools/generate_gallery.py`")
SIMULATOR_SOURCE_NOTE = ("Everything is rendered from the current firmware sources by the LVGL/SDL simulator "
                         "(`tools/generate_simulator_gallery.py`) with the synthetic states of "
                         "`tests/simulator/gallery_data.json`, including the House and Energy pages and the "
                         "doorbell popup with a stylized camera picture")


NAME_VISIBILITY_EXAMPLES = [
    ("sensor", "Sensor"), ("gauge", "Horizontal gauge"), ("graph", "Graph (line)"),
    ("media", "Media player"),  # the weather tiles are name-free by default
]


def relpath_for_md(path):
    return "img/gallery/" + path.name


def collect_images(img_dir, spec, styles=STYLES):
    """Map (style, gallery_id) to every gallery image already in img_dir, so a partial run
    keeps the images it did not produce."""
    ids = [tile_id for tile_id, tile in zip(tile_ids(spec), (t for page in spec for t in page["tiles"]))
           if tile["fields"] is not None]
    ids += [page_id for page_id, *_ in SPECIAL_PAGES] + ["gallery_doorbell", "gallery_notification", "gallery_notification_bold", "gallery_select_detail"]
    ids += [f"gallery_name_off_{key}" for key, _ in NAME_VISIBILITY_EXAMPLES]
    ids += ["gallery_history_sensor_en", "gallery_history_sensor_week_en", "gallery_history_sensor_week_de", "gallery_history_contact_de", "gallery_history_lock_en"]
    ids += ["gallery_bambu_detail", "gallery_vacuum_history", "gallery_wallbox_disconnected", "gallery_wallbox_sessions", "gallery_wallbox_power", "gallery_wallbox_flow"]
    images = {}
    for style in styles:
        for gallery_id in ids:
            path = img_dir / f"{gallery_id}_{style}.png"
            if gallery_id == "gallery_bambu_detail" and style != "glow":
                continue  # the current detail renderer emits Glow only
            if path.exists():
                images[(style, gallery_id)] = path
    return images


# How the library index groups the widgets: what the user wants to do. Every widget type is in exactly one group.
WIDGET_GROUPS = [
    ("Show values", "Numbers, trends and states you want to read at a glance.",
     ["sensor", "gauge", "graph", "binary_tile", "contact_tile", "parqet_portfolio", "stock_quotes"]),
    ("Control your home", "Switches, lights, heating, blinds and other things you operate by touch.",
     ["button", "slider", "light_tile", "heating_tile", "climate_tile", "cover_tile", "fan_tile", "lock_tile", "alarm_tile", "select_tile", "datetime_tile", "scene_tile"]),
    ("Rooms", "Everything about one room in a single card.", ["room_tile"]),
    ("Weather and energy", "Forecasts, solar production and the energy balance.",
     ["weather_tile", "weather_3day", "solar_forecast", "energy_price_forecast", "energy_timeline"]),
    ("Media and tasks", "Playback, favourites and shopping lists.", ["media_player", "sonos_favorites", "todo_list"]),
    ("Devices", "Vacuum, camera, car, wallbox and 3D printer.", ["vacuum_tile", "camera_tile", "car_widget", "bambu_printer", "wallbox_evcc"]),
    ("Layout", "Space for the design itself.", ["empty_tile"]),
]

# The Widget column names the widget *type*, e.g. "Slider" -- not each variant's own on-widget
# title ("Vertical", "Horizontal"), which said nothing about which widget it even was.
ROW_DISPLAY_NAME = {
    "sensor": "Sensor", "gauge": "Gauge", "car_widget": "Car", "bambu_printer": "Bambu printer", "wallbox_evcc": "Wallbox", "graph": "Graph",
    "solar_forecast": "Solar forecast", "energy_price_forecast": "Energy price forecast", "parqet_portfolio": "Parqet portfolio", "stock_quotes": "Stocks & ETFs", "weather_tile": "Weather (now)", "weather_3day": "Weather forecast",
    "button": "Button", "scene_tile": "Scene", "select_tile": "Select", "lock_tile": "Lock", "alarm_tile": "Alarm", "datetime_tile": "Date/Time", "slider": "Slider", "cover_tile": "Cover",
    "light_tile": "Light", "fan_tile": "Fan", "climate_tile": "Climate", "heating_tile": "Heating",
    "media_player": "Media player", "sonos_favorites": "Sonos favorites", "contact_tile": "Contact",
    "binary_tile": "Binary sensor", "room_tile": "Room", "vacuum_tile": "Vacuum", "energy_timeline": "Energy timeline",
    "camera_tile": "Camera", "todo_list": "Todo list",
}


def row_display_name(row):
    row_type = row["tiles"][0][1]["type"]
    return ROW_DISPLAY_NAME.get(row_type, row["tiles"][0][1]["title"])


def build_rows(spec):
    """The single source of truth for "which gallery_id is which tile, grouped and ordered
    how" -- render_markdown() calls this rather than re-deriving
    it, so the two outputs cannot drift into showing different content under the same label."""
    ids = iter(tile_ids(spec))
    # Group consecutive tiles that share a "slot" into one row (the gauge/heating/graph
    # variant sets); a tile with caps=None joins the row above it.
    rows = []
    for page_spec in spec:
        for tile in page_spec["tiles"]:
            gallery_id = next(ids)
            if rows and rows[-1]["slot"] == tile["slot"]:
                rows[-1]["tiles"].append((gallery_id, tile))
            else:
                rows.append({"slot": tile["slot"], "tiles": [(gallery_id, tile)], "caps": tile["caps"]})
            if tile["caps"] is not None:
                rows[-1]["caps"] = tile["caps"]

    order = [kind for _title, _intro, kinds in WIDGET_GROUPS for kind in kinds]

    def row_sort_key(row):
        row_type = row["tiles"][0][1]["type"]
        return order.index(row_type) if row_type in order else len(order)

    rows.sort(key=row_sort_key)
    return rows


def row_group(row):
    """The widget-library group (title, intro) of a gallery row, the same grouping as the widget guides."""
    row_type = row["tiles"][0][1]["type"]
    for title, intro, kinds in WIDGET_GROUPS:
        if row_type in kinds:
            return title, intro
    return None, None


def render_markdown(spec, images, source_note=PANEL_SOURCE_NOTE):
    """Optional visual reference: compare looks, leaving setup and option tables in user guides."""
    lines = [
        "<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->",
        "", "# Gallery", "",
        "Compare the same widgets in Flat, Glow, Glass and Matte. This is an optional visual reference; "
        "for choosing a look, use [Appearance](appearance.md). For features and setup, use the "
        "[widget library](widgets/README.md).", "",
        "Examples use synthetic data. The columns change only the style, not the available functions.", "",
        "## Widgets", "",
        "| Widget | " + " | ".join(STYLE_TITLE[style] for style in STYLES) + " |",
        "| " + " | ".join(["---"] * (len(STYLES) + 1)) + " |",
    ]
    for row in build_rows(spec):
        name = row_display_name(row)
        kind = row["tiles"][0][1]["type"]
        guide = "wallbox" if kind == "wallbox_evcc" else kind
        if (REPO_ROOT / "docs/widgets" / f"{guide}.md").is_file():
            name = f"[{name}](widgets/{guide}.md)"
        cells = []
        for style in STYLES:
            parts = []
            for gallery_id, tile in row["tiles"]:
                img_path = images.get((style, gallery_id))
                if img_path is not None:
                    caption = f"**{tile['title']}**<br>" if len(row["tiles"]) > 1 else ""
                    parts.append(f"{caption}![{tile['title']} ({style})]({relpath_for_md(img_path)})")
            cells.append("<br>".join(parts) if parts else "No preview")
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    lines += ["", "## Special pages", "", "[Pages and layouts](pages.md) explains their setup.", "",
              "| Page | " + " | ".join(STYLE_TITLE[style] for style in STYLES) + " |",
              "| " + " | ".join(["---"] * (len(STYLES) + 1)) + " |"]
    for page_id, _page_type, title, _caps in SPECIAL_PAGES:
        cells = [f"![{title} ({style})]({relpath_for_md(images[(style, page_id)])})"
                 if (style, page_id) in images else "No preview" for style in STYLES]
        lines.append(f"| {title} | " + " | ".join(cells) + " |")
    for key, title, guide in [
        ("gallery_doorbell", "Doorbell popup", "[Notifications](notifications.md)"),
        ("gallery_select_detail", "Select detail popup", "[Select](widgets/select_tile.md)"),
        ("gallery_history_sensor_en", "History popup", "[History popups](history.md)"),
    ]:
        cells = [f"![{title}, {style}]({relpath_for_md(images[(style, key)])})"
                 if (style, key) in images else "No preview" for style in STYLES]
        lines += ["", f"## {title}", "", f"Setup: {guide}." + (" Camera pictures are placeholders." if key == "gallery_doorbell" else ""), "",
                  "| Example | " + " | ".join(STYLE_TITLE[style] for style in STYLES) + " |",
                  "| " + " | ".join(["---"] * (len(STYLES) + 1)) + " |",
                  f"| {title} | " + " | ".join(cells) + " |"]
    lines += ["", "## Rendering", "", source_note + ".", "",
              "Regenerate images after visual changes with `python3 tools/generate_simulator_gallery.py`. "
              "For prose-only edits use `--markdown-only`; it preserves images and the source freshness record.", ""]
    return "\n".join(lines)
