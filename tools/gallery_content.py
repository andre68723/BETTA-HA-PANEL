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


# ---- widget spec: pages of tiles, each tile one widget type/variant -------
# rect is (x, y, w, h) relative to the content box, not the raw screen.
# "caps" is the free-text "Capabilities" column, written once per widget type
# (not per variant) in the generated table.

def build_spec(entities):
    sf = entities["solar_forecast"]
    return [
        {
            "page": "gallery_a",
            "tiles": [
                dict(slot="sensor", type="sensor", title="Sensor", rect=(10, 10, 200, 140),
                     fields={"entity_id": entities["sensor"]},
                     caps={"Domain": "sensor, binary_sensor",
                           "Toggles": "show_name, sensor_show_age (both default on)",
                           "Interaction": "read-only; optional history_popup opens Recorder history on tap",
                           "Size": "min 120x80, no max"}),
                dict(slot="button", type="button", title="Button", rect=(220, 10, 160, 160),
                     fields={"entity_id": entities["button"]},
                     caps={"Domain": "switch, media_player (also scene/script/button/lock as a momentary trigger)",
                           "Display": "button_mode: auto, play_pause, stop, next, previous",
                           "Colors": "button_accent_color",
                           "Interaction": "tap triggers the action directly, no popup",
                           "Size": "min 100x100, max 480x320"}),
                dict(slot="light_tile", type="light_tile", title="Light", rect=(390, 10, 200, 220),
                     fields={"entity_id": entities["light_tile"]},
                     caps={"Domain": "light",
                           "Options": "brightness, color temperature, RGB and effects (only when Home Assistant reports "
                                      "the capability)",
                           "Interaction": "the brightness bar is a direct touch slider; tapping the lamp icon "
                                          "toggles on/off directly; the color button opens color and effect controls",
                           "Size": "min 180x180, max 480x480"}),
                # Titles here are short on purpose (the Widget column already says "Slider" /
                # "Gauge" via ROW_DISPLAY_NAME) and taller than the widget's own minimum: at
                # 100x220 the state/value labels had no room and could overlap ("Offen 34 %"
                # jumbled together), which also surfaced a real bug (see w_slider.c).
                # slot="slider" tiles must stay adjacent in this list (not necessarily on
                # screen -- rect positions them wherever) so build_rows() merges them into one
                # "Slider" row instead of two identical-looking rows; same for slot="gauge".
                dict(slot="slider", type="slider", title="Vertical", shown="Dimmer", rect=(10, 240, 120, 240),
                     fields={"entity_id": entities["slider"], "slider_direction": "bottom_to_top"},
                     caps=None),  # written once from the horizontal variant below
                dict(slot="slider", type="slider", title="Horizontal", rect=(150, 490, 260, 105),
                     fields={"entity_id": entities["slider"], "slider_direction": "left_to_right"},
                     caps={"Domain": "any (light brightness, cover position, input_number, ...)",
                           "Display": "slider_direction: auto, left_to_right, right_to_left, bottom_to_top, "
                                       "top_to_bottom",
                           "Colors": "slider_accent_color",
                           "Interaction": "drag or tap the bar directly sets the value (a real touch slider), "
                                          "no popup",
                           "Size": "min 100x100, no max"}),
                dict(slot="gauge", type="gauge", title="Smooth", rect=(150, 240, 150, 240),
                     fields={"entity_id": entities["gauge"], "style_variant": "linear", "gauge_bar_style": "smooth",
                             "gauge_min": 0, "gauge_max": 100},
                     caps=None),  # written once from the radial variant below
                dict(slot="gauge", type="gauge", title="Segmented", shown="Level", rect=(310, 240, 150, 240),
                     fields={"entity_id": entities["gauge"], "style_variant": "linear", "gauge_bar_style": "segmented",
                             "gauge_min": 0, "gauge_max": 100},
                     caps=None),
                dict(slot="gauge", type="gauge", title="Horizontal gauge", shown="Humidity", rect=(420, 490, 260, 105),
                     fields={"entity_id": entities["gauge"], "style_variant": "horizontal",
                             "gauge_bar_style": "segmented", "gauge_segment_count": 20,
                             "gauge_min": 0, "gauge_max": 100},
                     caps=None),
                dict(slot="gauge", type="gauge", title="Radial", rect=(470, 240, 190, 240),
                     fields={"entity_id": entities["gauge"], "style_variant": "radial",
                             "gauge_min": 0, "gauge_max": 100},
                     caps={"Domain": "sensor",
                           "Display": "style_variant: linear (vertical bar), horizontal (bar filling left to right, "
                                      "title and value in one row above it), radial",
                           "Options": "linear and horizontal: gauge_bar_style (smooth, segmented -- an LED-style column of "
                                      "gauge_segment_count blocks, 4-30, default 10; fewer on a column too short for them)",
                           "Range": "gauge_min, gauge_max",
                           "Extras": "radial and horizontal: up to 2 extra readouts (secondary_entity_id, "
                                     "tertiary_entity_id) as small text -- under the value inside the arc, or in one row "
                                     "under the horizontal bar when the tile has room; not separate widgets",
                           "Interaction": "read-only",
                           "Size": "min 80x160 (linear/radial), 100x70 (horizontal), no max"}),
            ],
        },
        {
            "page": "gallery_b",
            "tiles": [
                dict(slot="graph", type="graph", title="Graph (line)", rect=(10, 10, 340, 150),
                     fields={"entity_id": entities["graph"], "graph_display_mode": "line",
                             "graph_line_color": "#6fe8ff", "graph_time_window_min": 120},
                     caps=None),
                dict(slot="graph", type="graph", title="Graph (bars)", rect=(10, 170, 340, 150),
                     fields={"entity_id": entities["graph"], "graph_display_mode": "bars",
                             "graph_line_color": "#6fe8ff", "graph_time_window_min": 120, "graph_bar_bucket_min": 15},
                     caps={"Domain": "any numeric sensor",
                           "Display": "graph_display_mode: line, line_smooth, line_smooth_points, bars",
                           "Options": "graph_time_window_min, graph_bar_bucket_min (bars only), "
                                      "graph_ha_history (24 h from the Home Assistant Recorder), "
                                      "graph_point_count (render points, empty = auto)",
                           "Colors": "graph_line_color",
                           "Interaction": "read-only",
                           "Size": "min 220x140, no max"}),
                dict(slot="heating_tile", type="heating_tile", title="Heating (default)", shown="Heating", rect=(10, 330, 230, 240),
                     fields={"entity_id": entities["heating_tile"], "style_variant": "default"},
                     caps=None),
                dict(slot="heating_tile", type="heating_tile", title="Heating (semi arc)", shown="Heating", rect=(250, 330, 230, 240),
                     fields={"entity_id": entities["heating_tile"], "style_variant": "arc_semi", "arc_opening": "left"},
                     caps={"Domain": "climate",
                           "Display": "style_variant: default (full arc), arc_semi (semi arc + mode buttons)",
                           "Options": "arc_semi only: arc_opening (left, right, top, bottom)",
                           "Interaction": "tapping the card toggles the climate entity on/off directly; the +/- "
                                          "buttons step the target temperature directly -- no popup either way",
                           "Size": "min 220x200, max 480x480"}),
                dict(slot="weather", type="weather_tile", title="Weather (now)", rect=(490, 330, 220, 240),
                     fields={"entity_id": entities["weather"]},
                     caps={"Domain": "weather", "Options": "weather entity and optional Show name (off by default)",
                           "Interaction": "read-only",
                           "Extras": "Today’s low and precipitation when space permits; the same colored Meteocons as the forecast on P4",
                           "Size": "min 220x200, max 480x480"}),
                dict(slot="solar_forecast", type="solar_forecast", title="Solar forecast", rect=(360, 10, 350, 310),
                     fields={
                         "entity_id": sf["entity_id"],
                         "forecast_today_entity_id": sf["today"],
                         "forecast_tomorrow_entity_id": sf["tomorrow"],
                         "forecast_day_3_entity_id": sf["day_3"],
                         "forecast_day_4_entity_id": sf["day_4"],
                         "forecast_day_5_entity_id": sf["day_5"],
                         "solar_forecast_bar_orientation": "horizontal",
                     },
                     caps={"Domain": "sensor x5 (today, tomorrow, day 3-5, whole numbers)",
                           "Display": "solar_forecast_bar_orientation: horizontal, vertical",
                           "Interaction": "read-only",
                           "Size": "min 260x220, max 640x480"}),
                dict(slot="solar_forecast", type="solar_forecast", title="Solar forecast (vertical)",
                     rect=(360, 330, 350, 310),
                     fields={
                         "entity_id": sf["entity_id"],
                         "forecast_today_entity_id": sf["today"],
                         "forecast_tomorrow_entity_id": sf["tomorrow"],
                         "forecast_day_3_entity_id": sf["day_3"],
                         "forecast_day_4_entity_id": sf["day_4"],
                         "forecast_day_5_entity_id": sf["day_5"],
                         "solar_forecast_bar_orientation": "vertical",
                     },
                     caps={"Domain": "sensor x5 (today, tomorrow, day 3-5, whole numbers)",
                           "Display": "solar_forecast_bar_orientation: horizontal, vertical",
                           "Interaction": "read-only",
                           "Size": "min 260x220, max 640x480"}),
            ],
        },
        {
            "page": "gallery_c",
            "tiles": [
                dict(slot="weather", type="weather_3day", title="Weather forecast", rect=(10, 10, 680, 400),
                     fields={"entity_id": entities["weather"],
                             **({"secondary_entity_id": entities["weather_report"]}
                                if entities.get("weather_report") else {})},
                     caps={"Domain": "weather",
                           "Toggles": "show_precipitation (rain chance and amount, default on)",
                           "Extras": "secondary_entity_id: an optional DWD weather report sensor, shown as a "
                                     "summary line in the header if there is room (needs roughly 150x40 px free "
                                     "there, so a wider tile) and per-day in the day popup",
                           "Interaction": "tapping a day card opens a popup (pressure, wind, UV, the report text)",
                           "Size": "min 260x220, no max"}),
            ],
        },
        {
            "page": "gallery_f",
            "tiles": [
                dict(slot="energy_timeline", type="energy_timeline", title="Energy last 24 hours", rect=(10, 10, 680, 520),
                     fields={"entity_id": entities.get("energy_timeline", "sensor.demo_grid_import"),
                             "secondary_entity_id": "sensor.demo_grid_export",
                             "tertiary_entity_id": "sensor.demo_solar_production",
                             "battery_charge_entity_id": "sensor.demo_battery_charge",
                             "battery_discharge_entity_id": "sensor.demo_battery_discharge",
                             "battery_soc_entity_id": "sensor.demo_battery_soc",
                             "forecast_today_entity_id": "sensor.demo_solar_forecast_today"},
                     caps={"Domain": "sensor (cumulative kWh counters read from Recorder statistics)",
                           "Roles": "entity_id grid import (required), secondary grid export, tertiary solar "
                                    "production, optional battery charge and discharge counters, battery level "
                                    "(battery_soc_entity_id) and a Solcast forecast (forecast_today_entity_id)",
                           "Notes": "up to four rings (home use, feed-in, solar production, battery level); bars: "
                                    "96 slots of 15 minutes, home use stacked by source (solar, battery, grid) above the "
                                    "axis, feed-in and battery charging below, the forecast as a line; gaps stay gaps; "
                                    "the split is an estimate and is labelled so in the popup",
                           "Interaction": "tap opens a details popup with the measured counters and the estimated mix",
                           "Size": "min 300x200, up to the whole content area"}),
            ],
        },
        {
            "page": "gallery_h",
            "tiles": [
                dict(slot="room_tile", type="room_tile", title="Room, humidity", rect=(10, 10, 320, 200),
                     fields={"entity_id": entities["room_tile"]["entity_id"],
                             "room_entities": entities["room_tile"]["room_entities"] + [entities["cover_tile"], entities["media_player"], entities["gauge"]],
                             "room_extra_mode": "humidity", "room_extra_entity_id": entities["gauge"]},
                     caps={"Notes": "room_extra_mode humidity: the chosen humidity sensor as the prominent extra readout"}),
                dict(slot="room_tile", type="room_tile", title="Room, cover", rect=(340, 10, 320, 200),
                     fields={"entity_id": entities["room_tile"]["entity_id"],
                             "room_entities": entities["room_tile"]["room_entities"] + [entities["cover_tile"], entities["media_player"], entities["gauge"]],
                             "room_extra_mode": "cover", "room_extra_entity_id": entities["cover_tile"]},
                     caps={"Notes": "room_extra_mode cover: state and position, a 44 px button opens or closes (Stop while moving, if supported)"}),
                dict(slot="room_tile", type="room_tile", title="Room, media player", rect=(10, 220, 320, 200),
                     fields={"entity_id": entities["room_tile"]["entity_id"],
                             "room_entities": entities["room_tile"]["room_entities"] + [entities["cover_tile"], entities["media_player"], entities["gauge"]],
                             "room_extra_mode": "media_player", "room_extra_entity_id": entities["media_player"]},
                     caps={"Notes": "room_extra_mode media_player: playing/paused status and a Play/Pause button when supported"}),
                dict(slot="alarm_tile", type="alarm_tile", title="Alarm", rect=(340, 330, 280, 110),
                     fields={"entity_id": entities["alarm_tile"]},
                     caps={"Domain": "alarm_control_panel",
                           "States": "disarmed, armed home/away/night/vacation/custom bypass, arming, pending, triggered (error colour)",
                           "Options": "alarm_actions: the arm and disarm actions the tile offers (default all the entity supports)",
                           "Interaction": "tap opens the action list, a PIN pad follows when the entity needs a code; "
                                          "optional history_popup opens on long press",
                           "Size": "min 180x80, max CBWx200"}),
                dict(slot="select_tile", type="select_tile", title="Charge mode", rect=(340, 220, 360, 100),
                     fields={"entity_id": entities["select_tile"]},
                     caps={"Domain": "select, input_select",
                           "Notes": "shows the current option; the new one appears once Home Assistant reports it",
                           "Interaction": "tap opens a list of the entity's options; picking one calls select_option",
                           "Size": "min 160x80, max CBWx200"}),
            ],
        },
        {
            "page": "gallery_g",
            "tiles": [
                dict(slot="energy_timeline", type="energy_timeline", title="Nested rings", rect=(10, 10, 680, 520),
                     fields={"entity_id": entities.get("energy_timeline", "sensor.demo_grid_import"),
                             "secondary_entity_id": "sensor.demo_grid_export",
                             "tertiary_entity_id": "sensor.demo_solar_production",
                             "battery_charge_entity_id": "sensor.demo_battery_charge",
                             "battery_discharge_entity_id": "sensor.demo_battery_discharge",
                             "battery_soc_entity_id": "sensor.demo_battery_soc",
                             "forecast_today_entity_id": "sensor.demo_solar_forecast_today",
                             "style_variant": "nested"},
                     caps={"Notes": "style_variant nested: the rings inside each other (outermost the battery level, "
                                    "then feed-in, solar production, home use) with one row per value beside them; "
                                    "the timeline follows when the card is tall enough"}),
            ],
        },
        {
            # panel4 allows at most 5 pages, so everything from here on shares just 2 more
            # (a device with more pages, e.g. a 10" panel, could give each its own again).
            "page": "gallery_d",
            "tiles": [
                dict(slot="media_player", type="media_player", title="Media player", rect=(6, 6, 340, 360),
                     fields={"entity_id": entities["media_player"]},
                     caps={"Domain": "media_player", "Options": "relative volume dragging, minus/plus buttons",
                           "Interaction": "the volume bar is a direct drag slider; minus/plus/play-pause buttons "
                                          "tap directly, no popup",
                           "Size": "min 260x220, no max"}),
                dict(slot="fan_tile", type="fan_tile", title="Fan", rect=(6, 294, 300, 240),
                     fields={"entity_id": entities["fan_tile"]},
                     caps={"Domain": "fan",
                           "Display": "flat (minus/power/plus) below 220 px height, large (turning rotor) above it",
                           "Options": "one chip per speed step, oscillation, up to 3 preset modes -- all gated by "
                                      "supported_features",
                           "Interaction": "minus/power/plus and every chip (speed step, oscillation, preset) tap "
                                          "directly, no popup",
                           "Size": "min 240x80, no max"}),
                dict(slot="car_widget", type="car_widget", title="Car", rect=(316, 6, 240, 340),
                     # The extras row (precond/lock/interior temp) needs >=330 px of *height* and
                     # >=200 px of width -- backwards from what the row's own Capabilities text
                     # used to say ("from 330x200"), which is why a 340x260 tile still hid it: it
                     # had the width to spare but not the height. Found by looking for the row
                     # that never showed up, not by reading the source first.
                     fields={"entity_id": entities["car_widget"]["entity_id"],
                             "precond_entity_id": entities["car_widget"]["precond_entity_id"],
                             "interior_temp_entity_id": entities["car_widget"]["interior_temp_entity_id"]},
                     caps={"Domain": "sensor (battery / state of charge)",
                           "Extras": "from 200 px width and 330 px height: precond_entity_id (switch/climate/"
                                     "button/select), lock_entity_id (lock or door binary sensor), "
                                     "interior_temp_entity_id -- all three render as a row of read-only labels/"
                                     "icons under the rings, except precond_entity_id which is a real button",
                           "Interaction": "read-only rings; the optional precond button (if configured) is "
                                          "directly tappable, lock and interior temperature are not",
                           "Notes": "range is always shown in km",
                           "Size": "min 200x200, max 480x560"}),
                dict(slot="contact_tile", type="contact_tile", title="Contact", rect=(316, 354, 180, 70),
                     fields={"entity_id": entities["contact_tile"]},
                     caps={"Domain": "binary_sensor",
                           "Display": "state words by device class, or an icon-only / door-window pictogram look",
                           "Interaction": "read-only; optional history_popup opens Recorder history on tap",
                           "Size": "min 180x70, no max"}),
                dict(slot="cover_tile", type="cover_tile", title="Cover", shown="Blinds", rect=(316, 432, 300, 100),
                     fields={"entity_id": entities["cover_tile"]},
                     caps={"Domain": "cover", "Options": "position bar, state words, up/stop/down",
                           "Interaction": "up/stop/down buttons tap directly; the position bar is a read-only "
                                          "indicator here, not a drag target -- for that, use the slider widget "
                                          "on the same cover entity instead (see the Slider row above)",
                           "Size": "min 240x80, max CBWx200"}),
                dict(slot="camera_tile", type="camera_tile", title="Camera", rect=(0, 0, 220, 180),
                     # never a real camera feed: only an entity map that names one (the simulator's demo data) gets a
                     # preview, see the entities file's _camera_tile_note
                     fields={"entity_id": entities["camera_tile"]} if entities.get("camera_tile") else None,
                     caps={"Domain": "camera, image",
                           "Interaction": "tap refreshes the snapshot; with optional Live picture (MJPEG), tap opens full screen",
                           "Notes": "snapshots refresh every 10s while on screen; MJPEG live mode is off by default; the preview shows a stylized placeholder "
                                    "picture, not a real camera",
                           "Size": "min 160x120, no max"}),
            ],
        },
        {
            "page": "gallery_e",
            "tiles": [
                dict(slot="vacuum_tile", type="vacuum_tile", title="Vacuum", rect=(6, 6, 280, 300),
                     fields={"entity_id": entities["vacuum_tile"], "battery_entity_id": "sensor.demo_robot_battery",
                             "history_popup": True},
                     caps={"Domain": "vacuum",
                           "Options": "start/pause, stop, dock, suction level and cleaning mode pickers; optional inline map image; optional battery "
                                      "sensor (battery_entity_id, the only source of the charge since Home Assistant 2026.9; the editor suggests it for a new card)",
                           "Interaction": "action buttons tap directly; the suction and mode chips open their pickers; the picture is read-only; "
                                          "with history_popup a long press on the card opens the last 24 hours of activity",
                           "Size": "min 240x220, no max"}),
                dict(slot="vacuum_tile", type="vacuum_tile", title="Vacuum with map", rect=(6, 6, 320, 440),
                     fields={"entity_id": entities["vacuum_tile"], "battery_entity_id": "sensor.demo_robot_battery",
                             **({"secondary_entity_id": entities["vacuum_map"]} if entities.get("vacuum_map") else {})},
                     caps=None),
                dict(slot="room_tile", type="room_tile", title="Room card", rect=(6, 312, 280, 180),
                     fields={"entity_id": entities["room_tile"]["entity_id"],
                             "room_entities": entities["room_tile"]["room_entities"]},
                     caps={"Domain": "any (temperature, humidity, windows/doors, lights, presence, batteries)",
                           "Extras": "entity_id plus up to 7 more in room_entities; filled from a Home Assistant "
                                     "area in the editor",
                           "Interaction": "tap opens a popup listing every entity, with toggles for "
                                          "lights/switches/covers/fans/media players; with \"Controls on the "
                                          "card\" a large card also shows one button per device type (lights and "
                                          "switches toggle together, covers get up and down)",
                           "Size": "min 220x120, max 640x320"}),
                dict(slot="climate_tile", type="climate_tile", title="Climate", rect=(292, 6, 280, 220),
                     fields={"entity_id": entities["climate_tile"]},
                     caps={"Domain": "climate",
                           "Options": "current/target temperature (0.5 steps), mode, fan and swing chips when supported",
                           "Interaction": "tapping a chip opens the corresponding mode, fan or swing picker",
                           "Size": "min 240x180, max 480x480"}),
                dict(slot="scene_tile", type="scene_tile", title="Scene", rect=(292, 212, 180, 90),
                     fields={"entity_id": entities["scene_tile"]},
                     caps={"Domain": "scene, script, button, input_button, lock (unlock)",
                           "Notes": "momentary tile, flashes the accent color",
                           "Interaction": "tap triggers the action directly, no popup",
                           "Size": "min 160x70, max CBWx160"}),
                dict(slot="lock_tile", type="lock_tile", title="Front door", rect=(540, 112, 240, 94),
                     fields={"entity_id": entities["lock_tile"]},
                     caps={"Domain": "lock",
                           "States": "locked, unlocked, locking, unlocking, open, opening, jammed (error colour)",
                           "Options": "lock_confirm: ask before unlocking or opening (default off); an Open button "
                                      "(lock.open) from 260 px width when the lock supports it",
                           "Interaction": "tap locks or unlocks (a jammed lock is locked again); optional history_popup opens on long press",
                           "Size": "min 180x80, max CBWx200"}),
                dict(slot="binary_tile", type="binary_tile", title="Binary sensor", shown="Hallway", rect=(482, 212, 220, 90),
                     fields={"entity_id": entities["binary_tile"]},
                     caps={"Domain": "binary_sensor, event",
                           "Display": "display_mode: '' (text + since), icon, since; event.* shows "
                                      "'Triggered'/'Last 5 min'",
                           "Interaction": "read-only; optional history_popup opens Recorder history on tap",
                           "Size": "min 160x70, max CBWx160"}),
                dict(slot="sonos_favorites", type="sonos_favorites", title="Sonos favorites", rect=(292, 312, 280, 240),
                     fields={"entity_id": entities["sonos_favorites"], "secondary_entity_id": "sensor.sonos_favorites"},
                     caps={"Domain": "media_player",
                           "Extras": "secondary_entity_id: sensor.sonos_favorites, rendered as the list of "
                                     "favorite rows itself (one or two columns by name length), not a sub-label",
                           "Interaction": "tapping a favorite plays it directly (media_play_media), no popup",
                           "Size": "min 280x240, no max"}),
                dict(slot="todo_list", type="todo_list", title="Todo list", rect=(0, 0, 300, 380),
                     fields={"entity_id": entities.get("todo_list", "todo.demo_shopping"), "items": TODO_DEMO_ITEMS},
                     caps={"Domain": "todo",
                           "Interaction": "tapping a row toggles that item done, no popup",
                           "Notes": "open items first, completed ones below; the list is read from Home Assistant every few minutes and after a change",
                           "Size": "min 220x200, max 640x640"}),
                dict(slot="datetime_tile", type="datetime_tile", title="Morning routine", rect=(6, 500, 280, 90),
                     fields={"entity_id": entities.get("datetime_tile", "input_datetime.demo_morning")},
                     caps={"Domain": "input_datetime, time, date, datetime",
                           "Display": "localized date and/or time; seconds appear only when non-zero",
                           "Interaction": "tap opens date/time rollers; Save sends one HA service call",
                           "Size": "min 180x90"}),
            ],
        },
    ] + bambu_pages(entities) + wallbox_pages(entities)


# The public example list of the simulator preview (a real panel reads it from the todo entity).
TODO_DEMO_ITEMS = [
    {"summary": "Milk", "completed": False}, {"summary": "Bread", "completed": False},
    {"summary": "Coffee beans", "completed": False}, {"summary": "Light bulbs", "completed": False},
    {"summary": "Batteries", "completed": True},
]


def wallbox_pages(entities):
    bindings = entities.get("wallbox_evcc")
    if not bindings:
        return []
    return [{"page": "gallery_wallbox", "tiles": [dict(
        slot="wallbox_evcc", type="wallbox_evcc", title="Wallbox", rect=(6, 6, 360, 360), fields=dict(bindings),
        caps={"Domain": "select (evcc loadpoint mode)",
              "Display": "status, power, charged energy, remaining time and current SOC on a solid green bar",
              "Interaction": "Off requests charging stop; Smart/Fast choose modes; Vehicle changes assignment; Target SOC applies one value; Sessions lists completed charges",
              "Size": "min 320x360, no max"})]}]


def bambu_pages(entities):
    """A printing and a paused Bambu printer card; only when the entity map names a printer (the public demo data does)."""
    printer = entities.get("bambu_printer")
    if not printer:
        return []
    caps = {"Domain": "sensor (print status of the Home Assistant Bambu Lab integration)",
            "Options": "explicit entity bindings: nozzle and bed temperature, progress, remaining time, optional "
                       "step and job name, optional pause, resume and stop buttons",
            "Interaction": "Pause or Resume (one button) and Stop (after a confirmation) press the bound Home Assistant "
                           "buttons; Status opens a read-only detail popup",
            "Size": "min 320x320, no max"}
    return [{
        "page": "gallery_i",
        "tiles": [
            dict(slot="bambu_printer", type="bambu_printer", title="Bambu A1", rect=(6, 6, 354, 420),
                 fields=dict(printer["printing"]), caps=caps),
        ],
    }]


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
      "Interaction": "tap a room card for its popup (see Room card above)",
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
    ids += ["gallery_bambu_detail", "gallery_vacuum_history", "gallery_wallbox_disconnected", "gallery_wallbox_sessions"]
    images = {}
    for style in styles:
        for gallery_id in ids:
            path = img_dir / f"{gallery_id}_{style}.png"
            if path.exists():
                images[(style, gallery_id)] = path
    return images


# How the library index groups the widgets: what the user wants to do. Every widget type is in exactly one group.
WIDGET_GROUPS = [
    ("Show values", "Numbers, trends and states you want to read at a glance.",
     ["sensor", "gauge", "graph", "binary_tile", "contact_tile"]),
    ("Control your home", "Switches, lights, heating, blinds and other things you operate by touch.",
     ["button", "slider", "light_tile", "heating_tile", "climate_tile", "cover_tile", "fan_tile", "lock_tile", "alarm_tile", "select_tile", "datetime_tile", "scene_tile"]),
    ("Rooms", "Everything about one room in a single card.", ["room_tile"]),
    ("Weather and energy", "Forecasts, solar production and the energy balance.",
     ["weather_tile", "weather_3day", "solar_forecast", "energy_timeline"]),
    ("Media and tasks", "Playback, favourites and shopping lists.", ["media_player", "sonos_favorites", "todo_list"]),
    ("Devices", "Vacuum, camera, car, wallbox and 3D printer.", ["vacuum_tile", "camera_tile", "car_widget", "bambu_printer", "wallbox_evcc"]),
    ("Layout", "Space for the design itself.", ["empty_tile"]),
]

# The Widget column names the widget *type*, e.g. "Slider" -- not each variant's own on-widget
# title ("Vertical", "Horizontal"), which said nothing about which widget it even was.
ROW_DISPLAY_NAME = {
    "sensor": "Sensor", "gauge": "Gauge", "car_widget": "Car", "bambu_printer": "Bambu printer", "wallbox_evcc": "Wallbox", "graph": "Graph",
    "solar_forecast": "Solar forecast", "weather_tile": "Weather (now)", "weather_3day": "Weather forecast",
    "button": "Button", "scene_tile": "Scene", "select_tile": "Select", "lock_tile": "Lock", "alarm_tile": "Alarm", "datetime_tile": "Date/Time", "slider": "Slider", "cover_tile": "Cover",
    "light_tile": "Light", "fan_tile": "Fan", "climate_tile": "Climate", "heating_tile": "Heating",
    "media_player": "Media player", "sonos_favorites": "Sonos favorites", "contact_tile": "Contact",
    "binary_tile": "Binary sensor", "room_tile": "Room card", "vacuum_tile": "Vacuum", "energy_timeline": "Energy timeline",
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
