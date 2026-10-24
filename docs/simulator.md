<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Desktop simulator

Runs the firmware's widget sources with LVGL in an SDL window at a variant's pixel size, with the built-in themes and
styles, the committed fonts and weather PNGs. A visual tool, not an ESP32 emulator: no Wi-Fi, MQTT, Home Assistant,
board driver or touch, and widget actions send nothing.

## Setup

=== "macOS"

    ```bash
    brew install cmake sdl2-compat pkg-config
    ```

=== "Debian/Ubuntu"

    ```bash
    sudo apt install cmake build-essential libsdl2-dev pkg-config git
    ```

CMake 3.21+, no ESP-IDF. LVGL and cJSON come from `managed_components/`, otherwise CMake fetches LVGL v9.6.0 and
cJSON v1.7.19 on the first configure (network needed) into `build/simulator/`. LVGL's SDL driver uses the SDL2 API;
`sdl2-compat` provides it on macOS.

## Run

```bash
tools/simulator.sh panel4                                   # tests/simulator/demo.json
tools/simulator.sh panel4 --fixture <file> --page <id> --theme classic_v1 --style flat --language de
SDL_VIDEODRIVER=dummy tools/simulator.sh panel4 --fixture <file> --page <id> --screenshot out.bmp --exit-after-ms 500
```

- Geometry is the first argument: `panel4`, `panel10`, `paneljc`, `paneljc-v2` or `tab5` (1280 × 720 landscape,
  720 × 1280 portrait).
- `--theme` takes a built-in id (`dark_v2`, `classic_v1`, `light`, `ocean`, `contrast`, `black`; no custom themes),
  `--style` one of `flat`, `glow`, `glass`, `matte`, `--page` a page id (default the first).
- `--rotation` is Tab5 only: 0 or 180 portrait, 90 (default) or 270 landscape, with the firmware's projection but no
  touch rotation.
- `--text-step N` and `--text-cap N` set the text size step and tile cap
  ([conventions](development/conventions.md#text-and-fonts)).
- `--screenshot f.bmp` writes a BMP and exits after 250 ms, or after `--exit-after-ms`.
- `--dump-layout f.jsonl` writes one line per visible object (rectangle, label text and its drawn rectangle); it is
  the input of `tools/check_widget_layout.py`.
- `BETTA_SIM_NOW=<Unix seconds>` in the environment fixes the clock: the firmware's `time()` returns it, and LVGL's
  tick and `esp_timer_get_time()` advance only by the simulator's own delays, so dates, ages, forecast days and
  animations render the same on every run. Set `TZ` for the local time.

## Test pages

```bash
tools/test_pages.sh                      # all sizes, four styles, text step 0, German
tools/test_pages.sh tab5 --text-steps 0,1,2 --languages de,en
```

Renders every widget and variant on topic pages for the 10-inch, Tab5 and 4-inch sizes, checks layout and content and
opens an overview (`/private/tmp/betta-testenv/view/index.html`, rewritten by each run). See
[Test layouts](development/testing.md#test-layouts).

## Fixture

A fixture is the layout's `pages` plus `states`. Get the pages from an authenticated `GET /api/layout`:
`jq '. + {states: []}' layout.json > preview.json`. Keep exported layouts and state snapshots out of the repository
(private names, locations). Public examples: `tests/simulator/demo.json`.

```json
{"entity_id": "sensor.example_power", "state": "7.1911",
 "attributes": {"unit_of_measurement": "kW"}, "display_precision": 2, "age_seconds": 240}
```

- `display_precision` (0-6, optional) feeds the firmware's precision cache; without it the raw state shows.
- `age_seconds` (optional) drives the freshness text.
- `frame` freezes the top bar: `date` (dd.mm.yyyy), `time` (HH:MM), `weather_temperature`, `weather_condition` (for
  example `partlycloudy`), `stocks` (`symbol`, `value`, optional `euro`), `left_items` (two slots, for example
  `["date", "stock"]`), `mqtt` (boolean) and `crash_warning` (boolean). Without it the host clock is used.
- `notification` (title, message, action, camera entity) opens the real notification card; a camera state with
  `entity_picture` `file:<path>` shows that PNG.
- `tap_widget` opens a widget's read-only popup by id. `setup_screen` (`ssid`, `password`, `ip`) renders the Wi-Fi
  setup splash; use public demo values.
- A page of type `house_dashboard` (with `house.rooms`) or `energy_dashboard` (with `energy`) renders like on the panel.

Regression fixtures: `matte_reference.json` (light, cover, climate, dimmer, German long titles, unavailable and unit
edge cases; `check_widget_layout.py --fixture` checks it) and `dutch-characters.json` (long `25.4926 kWh`, `ë`, `95.4 €`,
`°C`, update-age text below the value).

## Gallery

`python3 tools/generate_simulator_gallery.py` renders the [gallery](gallery.md) from `tests/simulator/gallery_data.json`
in all four styles; `--variant paneljc|tab5` renders the house and energy pages at that size
([10-inch](gallery-sim-paneljc.md), [5-inch](gallery-sim-tab5.md)). It needs Pillow and no panel; see
[Gallery](development/testing.md#gallery).

## Limits

- Every widget type in the gallery is linked from the real sources, plus graph, media player and vacuum. Camera and
  todo have no gallery image. Unsupported types are reported and skipped.
- The top bar and navigation are the firmware's `ui_pages.c` fed by `tests/simulator/pages_stubs.c` (Wi-Fi and Home
  Assistant show as connected). The navigation lists at most `APP_MAX_PAGES` pages, ending at the selected one.
- States are a snapshot and can differ from a device screenshot (`tools/screenshot.sh <ip>`). Check on the panel before
  claiming hardware behaviour.
