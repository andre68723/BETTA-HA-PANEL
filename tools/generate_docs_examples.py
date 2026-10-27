#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Render public dashboard examples and capture the current editor against demo APIs.

Run with uv run --locked --group screenshots python tools/generate_docs_examples.py.
Uses no device or Home Assistant connection. Browser requests stay on localhost.
"""
import argparse
import copy
import json
import math
import os
import re
import tempfile
import threading
from datetime import date
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from generate_simulator_gallery import DATA, REPO, TOP_BAR_H, build_simulator, check_run, demo_page, prepare_fixture

OUT = REPO / "docs/img/examples"
WWW = REPO / "components/webui/www"
# Widgets whose guide shows the inspector: many options, or options that depend on the entity.
# Wallbox and Bambu printer are left out: their inspector fills from device detection, which the demo API does not offer.
INSPECTOR_KINDS = ["sensor", "graph", "gauge", "energy_price_forecast", "parqet_portfolio", "stock_quotes", "room_tile", "energy_timeline"]


class DemoHTTPServer(ThreadingHTTPServer):
    # The editor loads many assets in parallel; a small listen backlog resets some connections.
    request_queue_size = 128


def fixtures():
    data = json.loads(DATA.read_text())
    _, gallery = prepare_fixture(data)
    templates = {p["title"]: p["widgets"][0] for p in gallery["pages"] if p["widgets"]}

    def tile(source, title, rect, **fields):
        widget = copy.deepcopy(templates[source])
        widget.update(id=title.lower().replace(" ", "_"), title=title,
                      rect=dict(zip(("x", "y", "w", "h"), rect)), **fields)
        return widget

    wide = [
        tile("Light", "Living room light", (16, 16, 300, 270)),
        tile("Heating (default)", "Heating", (332, 16, 300, 270)),
        tile("Media player", "Music", (648, 16, 300, 400)),
        tile("Solar forecast", "Solar forecast", (964, 16, 300, 400)),
        tile("Sensor", "Room temperature", (16, 302, 300, 114)),
        tile("Cover", "Blinds", (332, 302, 300, 114)),
        tile("Weather forecast", "Weather forecast", (16, 432, 932, 232), forecast_days=4),
        tile("Horizontal gauge", "Humidity", (964, 432, 300, 110)),
        tile("Contact", "Front door", (964, 558, 300, 106)),
    ]
    compact = [
        tile("Weather (now)", "Weather", (16, 16, 328, 250)),
        tile("Light", "Light", (360, 16, 344, 250)),
        tile("Heating (default)", "Heating", (16, 282, 328, 302)),
        tile("Media player", "Music", (360, 282, 344, 302)),
    ]
    devices = [
        tile("Energy last 24 hours", "Energy last 24 hours", (16, 16, 540, 400)),
        tile("Wallbox", "Wallbox", (572, 16, 330, 400)),
        tile("Bambu A1", "Printer", (918, 16, 346, 400)),
        tile("Climate", "Climate", (16, 432, 300, 232)),
        tile("Car", "Car", (332, 432, 300, 232)),
        tile("Alarm", "Alarm", (648, 432, 300, 110)),
        tile("Front door", "Front door", (648, 558, 300, 106)),
        tile("Fan", "Fan", (964, 432, 300, 232)),
    ]
    # Freeze age and time; forecast dates stay relative to the render day so a
    # later regeneration does not discard them as forecasts from the past.
    for state in gallery["states"]:
        state["age_seconds"] = 120
    shared = {"version": 1, "alerts": [], "states": gallery["states"],
              "frame": {"date": date.today().strftime("%d.%m.%Y"), "time": "14:30", "weather_temperature": "23 °C"}}
    house = {"id": "house", "title": "House", "type": "house_dashboard", "widgets": [], "house": data["house"]}
    energy = {"id": "energy", "title": "Energy", "type": "energy_dashboard", "widgets": [], "energy": data["energy"]}
    devices_fixture = dict(shared,
                           pages=[{"id": "devices", "title": "Devices", "widgets": devices, "ha_online": True}, house, energy])
    return (dict(shared, pages=[{"id": "home", "title": "Living room", "widgets": wide}, house, energy]),
            dict(shared, pages=[{"id": "home", "title": "Living room", "widgets": compact}, house, energy]),
            devices_fixture)


def first_light_fixture(compact):
    """One page and one real Light widget, shared by the tutorial's panel and editor images."""
    fixture = copy.deepcopy(compact)
    light = next(w for w in fixture["pages"][0]["widgets"] if w["type"] == "light_tile")
    light.update(id="first_light", title="Living room light", rect={"x": 40, "y": 40, "w": 320, "h": 300})
    fixture["pages"] = [{"id": "living_room", "title": "Living room", "widgets": [light]}]
    return fixture


# Screen points tapped before the screenshot (the Gas node of the Energy page, below the 60 px top bar).
TAPS = {"energy-details": ["560,160"], "energy-details-popup": ["560,160"]}


def render_panels(wide, compact, devices, output, appearance_only=False, setup_only=False, topbar_only=False,
                  weather_report_only=False, history_only=False, energy_details_only=False, first_light_only=False):
    from PIL import Image
    from check_widget_layout import check_dumps

    captures = [("paneljc", wide, "home", "glow", "dashboard-wide"),
                ("paneljc", devices, "devices", "glow", "dashboard-devices"),
                ("panel4", compact, "home", "glass", "dashboard-compact"),
                ("paneljc", wide, "house", "matte", "house"),
                ("paneljc", wide, "energy", "glow", "energy"),
                ("tab5", wide, "house", "matte", "house-tab5")]
    comparison = copy.deepcopy(wide)
    _, gallery = prepare_fixture(json.loads(DATA.read_text()))
    comparison["pages"] = [demo_page(gallery, "paneljc")]
    comparisons = [("paneljc", comparison, "gallery_demo", style, f"appearance-{style}")
                   for style in ("flat", "glow", "glass", "matte")]
    setup = dict(compact, pages=[{"id": "setup", "title": "Setup", "widgets": []}],
                 setup_screen={"ssid": "BETTA-Setup-1A2B", "password": "maple-river-42", "ip": "192.168.4.1"})
    setup_capture = [("panel4", setup, "setup", "flat", "setup-device")]
    topbar = {"version": 1, "pages": [{"id": "topbar", "title": "Demo", "widgets": []}],
              "frame": {"date": "04.10.2026", "time": "14:30", "left_items": ["date", "stock"],
                        "stocks": [{"symbol": "VWCE.DE", "value": "123.45", "euro": True}]}}
    topbars = [(variant, topbar, "topbar", "glow", name) for variant, name in
               [("panel4", "topbar-4-inch"), ("tab5", "topbar-5-inch"), ("paneljc", "topbar-10-inch")]]
    weather_topbar = copy.deepcopy(topbar)
    weather_topbar["frame"].update(left_items=["date", "weather", "stock"], weather_temperature="23",
                                   weather_condition="partlycloudy")
    topbars += [(variant, weather_topbar, "topbar", "glow", name) for variant, name in
                [("tab5", "topbar-5-inch-weather"), ("paneljc", "topbar-10-inch-weather")]]
    # The MQTT chip appears while MQTT runs; the connectivity popups open from the right-hand chips.
    mqtt_topbar = copy.deepcopy(weather_topbar)
    mqtt_topbar["frame"]["mqtt"] = True
    topbars += [("paneljc", mqtt_topbar, "topbar", "glow", "topbar-10-inch-mqtt")]
    popups = []
    for kind in ("wifi", "ha", "mqtt"):
        popup = copy.deepcopy(mqtt_topbar)
        popup["open_popup"] = kind
        popups.append(("paneljc", popup, "topbar", "glow", f"popup-{kind}"))
    report_fixture = copy.deepcopy(wide)
    forecast = next(widget for widget in report_fixture["pages"][0]["widgets"] if widget["type"] == "weather_3day")
    forecast.update(id="weather_report", rect={"x": 16, "y": 16, "w": 680, "h": 520})
    report_fixture["pages"] = [{"id": "weather_report", "title": "Weather report", "widgets": [forecast]}]
    report = next(state for state in report_fixture["states"] if state["entity_id"] == forecast["secondary_entity_id"])
    # Real DWD reports are German; the documentation example uses synthetic English text, not a real forecast.
    report["state"] = "Sunny and mild"
    report["attributes"] = {"s": "Sunny and mild today. A few showers overnight, changeable tomorrow.",
                            "d": {str(day): "Sunny, partly cloudy with a light breeze." for day in range(7)}}
    reports = [("paneljc", report_fixture, "weather_report", style, f"weather-forecast-report_{style}")
               for style in ("flat", "glow", "glass", "matte")]
    temperature = "sensor.demo_room_temperature"
    graph = {"id": "temperature_history", "type": "graph", "title": "Bedroom temperature",
             "entity_id": temperature, "rect": {"x": 20, "y": 20, "w": 680, "h": 360},
             "graph_ha_history": True, "graph_time_window_min": 1440, "graph_point_count": 48,
             "graph_variant": "line", "graph_line_color": "#6fe8ff", "history_step_minutes": 30}
    # 49 samples, 30 minutes apart: the complete 24-hour window, including both endpoints.
    graph["history"] = [round(21.0 + 1.8 * math.sin(i * math.tau / 48)
                              + 0.3 * math.sin(i * math.tau / 24), 1) for i in range(49)]
    graph_fixture = {"version": 1, "pages": [{"id": "history", "title": "History", "widgets": [graph]}],
                     "states": [{"entity_id": temperature, "state": "21.0",
                                 "attributes": {"friendly_name": "Bedroom temperature",
                                                "unit_of_measurement": "°C", "device_class": "temperature"}}]}
    motion = copy.deepcopy(next(p["widgets"][0] for p in gallery["pages"]
                                if p["widgets"] and p["widgets"][0]["type"] == "binary_tile"))
    motion.update(id="motion_history", title="Hallway motion", entity_id="binary_sensor.demo_hallway_motion",
                  history_popup=True, rect={"x": 20, "y": 20, "w": 300, "h": 160})
    motion_fixture = {"version": 1, "pages": [{"id": "history", "title": "History", "widgets": [motion]}],
                      "states": [{"entity_id": motion["entity_id"], "state": "off",
                                  "attributes": {"friendly_name": "Hallway motion", "device_class": "motion"}}],
                      "history_popup": {"entity_id": motion["entity_id"], "title": "Hallway motion",
                                        "current": "Clear", "numeric": False, "device_class": "motion"}}
    histories = [("panel4", graph_fixture, "history", "flat", "graph-ha-history-24h"),
                 ("panel4", motion_fixture, "history", "flat", "binary-motion-history-24h")]
    # Seven-day details of Gas, Water and Solar: the right-hand panel on the 10-inch page after a tap on Gas, and the
    # popup the 4-inch page opens instead (its page is too narrow for the panel, whatever the page setting says).
    side = copy.deepcopy(wide)
    next(page for page in side["pages"] if page["id"] == "energy")["energy"]["details"] = "side"
    narrow = copy.deepcopy(compact)
    next(page for page in narrow["pages"] if page["id"] == "energy")["energy"]["details"] = "side"
    energy_details = [("paneljc", side, "energy", "glow", "energy-details"),
                      ("panel4", narrow, "energy", "glow", "energy-details-popup")]
    first_light = [("panel4", first_light_fixture(compact), "living_room", "glow", "first-light-panel")]
    captures = (first_light if first_light_only else histories if history_only else reports if weather_report_only else topbars + popups if topbar_only
                else setup_capture if setup_only else comparisons if appearance_only
                else energy_details if energy_details_only
                else captures + comparisons + setup_capture + topbars + popups + reports + histories + energy_details + first_light)
    binaries = {}
    with tempfile.TemporaryDirectory(prefix="betta_docs_") as temp:
        for variant, fixture, page, style, name in captures:
            if variant not in binaries:
                binaries[variant] = build_simulator(variant)
            path = Path(temp) / f"{name}.json"
            path.write_text(json.dumps(fixture))
            bmp, dump = Path(temp) / f"{name}.bmp", Path(temp) / f"{name}.jsonl"
            result = check_run([str(binaries[variant]), "--fixture", str(path), "--page", page,
                               "--theme", "dark_v2", "--style", style, "--language", "en",
                               *[arg for tap in TAPS.get(name, []) for arg in ("--tap", tap)],
                               "--screenshot", str(bmp), "--dump-layout", str(dump), "--exit-after-ms", "400"],
                               env=dict(os.environ, SDL_VIDEODRIVER="dummy"))
            if "skipped" in result and "0 skipped" not in result:
                raise RuntimeError(result)
            problems = check_dumps([(f"{name}/{style}: ", dump)])
            if problems:
                raise RuntimeError("\n".join(problems))
            with Image.open(bmp) as image:
                if name.startswith("topbar-"):
                    image = image.crop((0, 0, image.width, TOP_BAR_H))
                elif name.startswith("popup-"):
                    # The popup is centred; its height follows the rows (Home Assistant also lists the firmware).
                    if name == "popup-ha":
                        card = next(row for line in dump.read_text().splitlines()
                                    if (row := json.loads(line)).get("widget") == "popup"
                                    and row.get("depth") == 2 and row.get("kind") == "obj")
                        image = image.crop((max(0, card["x1"] - 12), max(0, card["y1"] - 12),
                                            min(image.width, card["x2"] + 13), min(image.height, card["y2"] + 13)))
                    else:
                        image = image.crop((400, 275, 880, 525))
                elif name.startswith("weather-forecast-report_") or name == "graph-ha-history-24h":
                    rect = fixture["pages"][0]["widgets"][0]["rect"]
                    x, y = rect["x"], rect["y"] + TOP_BAR_H
                    image = image.crop((x, y, x + rect["w"], y + rect["h"]))
                image.convert("RGB").save(output / f"{name}.png", optimize=True)
            print(f"Rendered {name} ({variant}, {style})")


def demo_api(fixture):
    """Only public synthetic responses; no credentials, proxy or external calls."""
    layout = {k: v for k, v in fixture.items() if k in ("version", "alerts", "pages")}
    states = []
    for state in fixture["states"]:
        attrs = copy.deepcopy(state.get("attributes", {}))
        # The simulator's compact fixture recipe is expanded by its C adapter.
        # Give the editor the same real HA list shape rather than a test-only key.
        recipe = attrs.pop("price_series", None)
        if isinstance(recipe, dict):
            from datetime import datetime, timedelta
            start = datetime.now().astimezone().replace(hour=0, minute=0, second=0, microsecond=0)
            interval = timedelta(minutes=recipe.get("interval_min", 60))
            attrs["data"] = [{"start_time": (start + timedelta(days=day) + index * interval).isoformat(), "price_per_kwh": value}
                             for day, key in enumerate(("today", "tomorrow")) for index, value in enumerate(recipe.get(key, []))]
        states.append({**state, "attributes": attrs, "attributes_json": json.dumps(attrs)})
    # Explicit integration metadata for synthetic examples; never inferred in the production editor.
    integrations = {}
    for page in fixture.get("pages", []):
        for widget in page.get("widgets", []):
            kind = widget.get("type")
            if kind in ("stock_quotes", "sonos_favorites", "bambu_printer"):
                for key, value in widget.items():
                    if key.endswith("entity_id") and isinstance(value, str):
                        integrations[value] = kind
                if kind == "stock_quotes":
                    for value in widget.get("stock_entities", []):
                        integrations[value] = kind

    def search_flags(state):
        attrs = state["attributes"]
        try:
            import math
            numeric = int(math.isfinite(float(state["state"])))
        except (ValueError, TypeError):
            numeric = 0
        kind = integrations.get(state["entity_id"])
        return (numeric | (2 if isinstance(attrs.get("data"), str) and len(attrs["data"]) > 400 and "Wetter" in attrs["data"] else 0)
                | (4 if isinstance(attrs.get("items"), dict) else 0)
                | ({"stock_quotes": 8, "sonos_favorites": 16, "bambu_printer": 32}.get(kind, 0))
                | (64 if isinstance(attrs.get("current_temperature"), (int, float)) else 0)
                | (128 if kind == "bambu_printer" and {"running", "pause"} <= set(attrs.get("options", [])) else 0))
    entities = [{"id": s["entity_id"], "name": s["attributes"].get("friendly_name", s["entity_id"]),
                 "search_flags": search_flags(s), "supported_features": s["attributes"].get("supported_features"),
                 "capabilities": {"dimming": any(mode not in ("onoff", "unknown") for mode in s["attributes"].get("supported_color_modes", []))},
                 "domain": s["entity_id"].split(".")[0], "unit": s["attributes"].get("unit_of_measurement", ""),
                 "device_class": s["attributes"].get("device_class", ""),
                 "state_class": s["attributes"].get("state_class", ""),
                 "has_price_series": bool(s["attributes"].get("pf") or s["attributes"].get("raw_today") or
                                          any("price_per_kwh" in row or "da_cons" in row for row in s["attributes"].get("data", []) if isinstance(row, dict))),
                 "has_solar_forecast": isinstance(s["attributes"].get("detailedForecast", s["attributes"].get("fc_kw")), list), "area_id": "living_room",
                 "area_name": "Living room"} for s in states]
    # Read the firmware's Dark palette, so the editor preview and LVGL use the same colors.
    source = (REPO / "main/ui/theme/theme_palette.c").read_text().split("k_preset_classic_v1")[0]
    palette = {key: "#" + value for key, value in re.findall(r"\.(\w+)\s*=\s*0x([0-9A-Fa-f]{6})", source)}
    theme = {"id": "dark_v2", "name": "Dark", "builtin": True, "colors": palette}
    return {
        "/api/display/test": {"active": False},
        "/api/version": {"version": "0.9.0", "project": "BETTA HA Panel", "board": "panel4",
                         "screen_w": 720, "screen_h": 720, "canvas_w": 720, "canvas_h": 600,
                         "max_pages": 8, "full_nav_pages": 5, "layout_max_bytes": 32768,
                         "text": {"board": "panel4", "floor": 20,
                                  "roles": ["meta", "label", "value", "key", "hero"],
                                  "sizes": [[20, 24, 28, 36, 42], [24, 28, 32, 42, 48], [28, 32, 36, 48, 56]]}},
        "/api/settings": {"wifi": {"configured": True, "ssid": "Demo home"},
                          "ha": {"configured": True, "ws_url": "ws://homeassistant.local:8123/api/websocket"},
                          "ui": {"language": "en", "style": "glow", "brightness": 80,
                                 "dim_brightness": 20, "dim_timeout_sec": 120}},
        "/api/layout": layout,
        "/api/entities": {"items": entities},
        "/api/state": {"items": states},
        "/api/ha/diagnostics": {"total": 0, "items": []},
        "/api/i18n/languages": {"languages": [{"code": "en"}, {"code": "de"}]},
        "/api/i18n/effective": {},
        "/api/themes": {"themes": [theme], "active_id": "dark_v2"},
        "/api/themes/active": theme,
        "/api/themes/get": theme,
        "/api/ota/status": {"running": False, "update": {"enabled": False}},
        "/api/reboot-log": {"items": []},
        "/api/log": {"text": "Documentation demo; no device connected."},
    }


def capture_editor(fixture, output, executable, setup_only=False, hero=False, first_light=False):
    from playwright.sync_api import sync_playwright

    responses = demo_api(fixture)
    if hero:
        # The wide 10-inch dashboard of the home page, shown as it looks in the editor.
        responses["/api/version"].update(board="paneljc", screen_w=1280, screen_h=800, canvas_w=1280, canvas_h=680,
                                         max_pages=12, full_nav_pages=10)
    failures = []

    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            if not url.path.startswith("/api/"):
                return super().do_GET()
            if url.path not in responses:
                failures.append(f"Unmocked API: {url.path}")
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

        def do_POST(self):
            self.send_error(405, "Documentation demo is read-only")

        do_PUT = do_POST
        do_DELETE = do_POST

    server = DemoHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(WWW)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with sync_playwright() as p:
            options = {"headless": True}
            if executable:
                options["executable_path"] = executable
            browser = p.chromium.launch(**options)
            size = {"width": 1440, "height": 900} if hero else {"width": 1536, "height": 864}
            page = browser.new_page(viewport=size, device_scale_factor=1, locale="en-GB")
            page.on("pageerror", lambda error: failures.append(str(error)))
            page.on("requestfailed", lambda request: failures.append(f"{request.failure}: {request.url}"))
            page.on("console", lambda msg: failures.append(msg.text) if msg.type == "error" else None)

            def local_only(route):
                if urlsplit(route.request.url).hostname != "127.0.0.1":
                    failures.append(f"External request: {route.request.url}")
                    route.abort()
                else:
                    route.continue_()

            page.route("**/*", local_only)
            url = f"http://127.0.0.1:{server.server_port}"
            if first_light:
                page.goto(url, wait_until="networkidle")
                page.wait_for_function("editor.editorStarted && editor.entities.length > 0")
                page.evaluate("editor.selectedWidgetId = 'first_light'; renderAll()")
                page.locator("#canvas").wait_for()
                page.screenshot(path=str(output / "first-light-editor.png"))
                browser.close()
                if failures:
                    raise RuntimeError("\n".join(failures))
                print("Captured the first-light tutorial (public demo API)")
                return
            if hero:
                page.goto(url, wait_until="networkidle")
                page.wait_for_function("editor.editorStarted && editor.entities.length > 0")
                page.evaluate("editor.selectedWidgetId = 'living_room_light'; renderAll()")
                page.locator("#canvas").wait_for()
                page.screenshot(path=str(output / "editor-dashboard.png"))
                browser.close()
                if failures:
                    raise RuntimeError("\n".join(failures))
                print("Captured the editor with the home page dashboard")
                return
            if not setup_only:
                page.goto(url, wait_until="networkidle")
                page.wait_for_function("editor.editorStarted && editor.entities.length > 0")
                page.evaluate("editor.selectedWidgetId = 'light'; renderAll()")
                page.locator("#canvas").wait_for()
                page.screenshot(path=str(output / "editor-layout.png"))
                # the Energy page's own options: data source and the seven-day details (popup or right-hand panel)
                page.evaluate("editor.selectedPageId = 'energy'; editor.selectedWidgetId = null; renderAll()")
                # The page inspector in the widget inspector's form (js/inspector-layout.js): header and sections.
                page.locator("#pageInspectorSection .inspector-sec").first.wait_for(state="visible")
                page.locator("#pageInspectorSection").screenshot(path=str(output / "editor-energy.png"))
                page.evaluate("setActivePane('settings'); setActiveSettingsSection('settingsThemeSection')")
                page.screenshot(path=str(output / "editor-appearance.png"))
                page.evaluate("setActiveSettingsSection('settingsHardwareSection')")
                page.locator("#settingsHardwareSection").screenshot(path=str(output / "editor-display.png"))
            page.set_viewport_size({"width": 800, "height": 720})
            responses["/api/settings"]["wifi"] = {"configured": False, "setup_ap_active": True,
                                                   "setup_ap_ssid": "BETTA-Setup-1A2B", "country_code": "DE"}
            responses["/api/settings"]["ha"] = {"configured": False}
            page.goto(url, wait_until="networkidle")
            page.locator("#provisioningWifiPage:not(.hidden)").wait_for()
            page.screenshot(path=str(output / "setup-webui-wifi.png"))
            responses["/api/settings"]["wifi"] = {"configured": True, "ssid": "Demo home", "country_code": "DE"}
            page.goto(url, wait_until="networkidle")
            page.locator("#provisioningHaPage:not(.hidden)").wait_for()
            page.screenshot(path=str(output / "setup-webui-ha.png"))
            browser.close()
        if failures:
            raise RuntimeError("\n".join(failures))
        print("Captured WebUI setup" + ("" if setup_only else ", editor layout and appearance") + " (public demo API)")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def capture_inspectors(output, executable):
    """The inspector (Placement collapsed) for INSPECTOR_KINDS, each with the widget of its guide's main preview (the
    first gallery tile of that type) selected, at twice the pixel density for legible text."""
    from playwright.sync_api import sync_playwright

    _spec, fixture = prepare_fixture(json.loads(DATA.read_text()))
    responses = demo_api(fixture)
    failures = []

    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            if not url.path.startswith("/api/"):
                return super().do_GET()
            payload = responses.get(url.path)
            if payload is None:
                self.send_error(404)
                return
            body = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            self.send_error(405, "Documentation demo is read-only")

    server = DemoHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(WWW)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with sync_playwright() as p:
            options = {"headless": True}
            if executable:
                options["executable_path"] = executable
            browser = p.chromium.launch(**options)
            page = browser.new_page(viewport={"width": 1536, "height": 2400}, device_scale_factor=2, locale="en-GB")
            page.on("pageerror", lambda error: failures.append(str(error)))
            page.on("requestfailed", lambda request: failures.append(f"{request.failure}: {request.url}"))
            page.route("**/*", lambda route: route.continue_() if urlsplit(route.request.url).hostname == "127.0.0.1"
                       else (failures.append(f"External request: {route.request.url}"), route.abort()))
            for kind in INSPECTOR_KINDS:
                widget = next((pg["widgets"][0] for pg in fixture["pages"]
                               if pg["widgets"] and pg["widgets"][0]["type"] == kind), None)
                if widget is None:
                    failures.append(f"No gallery tile of type {kind}")
                    continue
                # The gallery has more tiles than a layout has pages: each capture loads a layout of this tile alone.
                responses["/api/layout"] = {"version": 1, "alerts": [], "pages": [{"id": "inspector", "title": "Demo",
                                                                                    "widgets": [widget]}]}
                # The demo's energy counters and similar have no state; list them so the rows do not read "not found".
                listed = {item["id"] for item in responses["/api/entities"]["items"]}
                for value in widget.values():
                    if isinstance(value, str) and re.fullmatch(r"[a-z_]+\.[a-z0-9_]+", value) and value not in listed:
                        responses["/api/entities"]["items"].append({"id": value, "name": value, "domain": value.split(".")[0],
                                                                    "unit": "", "device_class": ""})
                        listed.add(value)
                page.goto(f"http://127.0.0.1:{server.server_port}", wait_until="networkidle")
                try:
                    page.wait_for_function("editor.editorStarted && editor.entities.length > 0", timeout=15000)
                except Exception as error:
                    state = page.evaluate("({url: location.href, started: typeof editor !== 'undefined' && editor.editorStarted, entities: typeof editor !== 'undefined' ? editor.entities.length : 0, status: document.getElementById('status')?.textContent, body: document.body.innerText.slice(0, 500)})")
                    raise RuntimeError(("\n".join(failures) or str(error)) + f"\nEditor state: {state}") from error
                page.evaluate("(id) => { editor.selectedPageId = 'inspector'; editor.selectedWidgetId = id; renderAll(); }",
                              widget["id"])
                page.locator("#inspectorSection").wait_for(state="visible")
                page.wait_for_timeout(200)
                page.locator("#inspectorSection").screenshot(path=str(output / f"inspector-{kind}.png"))
                print(f"Captured the inspector of {kind}")
            browser.close()
        if failures:
            raise RuntimeError("\n".join(failures))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUT)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--panels-only", action="store_true")
    selection.add_argument("--editor-only", action="store_true")
    selection.add_argument("--first-light-only", action="store_true", help="Render the first-light tutorial panel and editor")
    selection.add_argument("--appearance-only", action="store_true", help="Render the four matched style comparisons")
    selection.add_argument("--setup-only", action="store_true", help="Capture device and WebUI first-run setup with demo credentials")
    selection.add_argument("--topbar-only", action="store_true", help="Render cropped 4-, 5- and 10-inch top bars and the connectivity popups")
    selection.add_argument("--weather-report-only", action="store_true", help="Render weather forecasts with synthetic DWD report text")
    selection.add_argument("--history-only", action="store_true", help="Render a 24-hour temperature graph and motion history popup")
    selection.add_argument("--energy-details-only", action="store_true", help="Render the Energy page's seven-day details (side panel and popup)")
    selection.add_argument("--inspector-only", action="store_true", help="Capture the inspector of the widgets whose guide shows it")
    parser.add_argument("--browser-executable", help="Use an installed Chromium browser instead of Playwright's")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.inspector_only:
        capture_inspectors(args.output, args.browser_executable)
        return
    wide, compact, devices = fixtures()
    if not args.editor_only:
        render_panels(wide, compact, devices, args.output, args.appearance_only, args.setup_only, args.topbar_only,
                      args.weather_report_only, args.history_only, args.energy_details_only, args.first_light_only)
    if args.first_light_only:
        capture_editor(first_light_fixture(compact), args.output, args.browser_executable, first_light=True)
        return
    if not any((args.panels_only, args.appearance_only, args.topbar_only, args.weather_report_only, args.history_only,
                args.energy_details_only)):
        capture_editor(compact, args.output, args.browser_executable, args.setup_only)
        if not args.setup_only:
            capture_editor(wide, args.output, args.browser_executable, hero=True)
            capture_editor(first_light_fixture(compact), args.output, args.browser_executable, first_light=True)


if __name__ == "__main__":
    main()
