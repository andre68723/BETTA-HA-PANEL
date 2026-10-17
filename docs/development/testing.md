<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Checks and device tests

## Checks without a device

Firmware and the SDL2 simulator share the LVGL widget sources; the editor has its own checks. Run the local checks
first, then review on hardware.

![Sources go through tools/check.sh; its result feeds the firmware builds, CI and the device checks](../img/development/checks.svg)

- `uv run --locked --only-group checks tools/check.sh` reproduces CI with pinned FontTools, Brotli and Pillow.
- `tools/check.sh` runs editor JavaScript syntax, editor tests (`tests/webui/*.test.mjs`), HTML balance, `en`/`de`
  tables with identical keys and an ASCII English firmware table (German may use umlauts and `ß`), the source and
  documentation size checks and, with cmake and SDL2, the simulator regressions, the widget layout check and the host
  tests. `--build` also builds all five variants (`panel4`, `panel10`, `paneljc`, `paneljc-v2`, `tab5`) with the full
  define set and checks the flash size in `flash_args`. Run it before committing editor or translation changes.
  `.github/workflows/check.yml` runs the checks with `--full`; `.github/workflows/build.yml` builds and packages every
  variant in the ESP-IDF 6.1 image on a version tag, a push to `main` or by hand, and only uploads artifacts.
- Output is one line per step; the full output is in `${TMPDIR:-/tmp}/betta-check/<step>.log` (`BETTA_CHECK_LOG_DIR` overrides). A slow step whose command and
  input files are unchanged since its last pass is skipped (`build/check-stamps/`, `--force` runs it anyway). The widget
  layout runs at the standard text step on every check and at the Larger and Largest steps with `--full` and in CI.
  [Run levels](tools.md#run-levels) say which tool runs when.
- `tests/host/test_i18n_builtin.c` loads both built-in translation tables as strict JSON (trailing data fails), requires
  `lvgl` as the only root key and equal keys in `de` and `en`, and checks real `ui_i18n_get()` lookups in both languages.
- `tests/host/test_history_stats.c` covers the Recorder statistics parser with reply shapes from a real Home Assistant:
  metadata and eligibility (measurement versus counter, unit class, state class), the 168-hour window, gaps, duplicates,
  off-hour and out-of-window rows, and hourly changes summed into local days across both daylight-saving changes.
- The statistics parser test also covers a cumulative counter classified from its metadata alone, summing several
  sources and marking an hour or a whole source as missing. `tools/check_web.py` fails when a `ui_i18n_get` key in
  `main/` has no entry in the built-in tables.
- `tests/host/test_admin_auth.c` includes `main/settings/admin_auth.c` with a fake NVS (`tests/host/auth_stubs/`) and injects faults:
  open and read errors, damaged or foreign-version records, half a legacy salt/hash pair, failing writes. It checks that "never set"
  stays open, "unreadable" answers closed and heals when the storage does, and that a legacy pair migrates.
- `tests/host/test_history_week.c` runs the real 7-day worker (`main/ha/ha_history_week.c`) on the host with pthread-backed
  FreeRTOS stubs (`tests/host/week_stubs/`), a counting heap and a scriptable WebSocket: a busy heavy-request gate with bounded
  retries, a reply that never comes (and the cancel), a reply that arrives after the timeout, failing and wrongly shaped replies,
  a failing or partial source, other units and unit classes, queue overflow, and no allocation left behind after a thousand
  requests. The waits are compile-time overrides (`WEEK_*_MS`) so the test needs seconds, not minutes.
- The history and energy regressions end with a memory loop: 300 popup or page cycles are judged against a baseline cycle (a lock popup,
  a page built and deleted without taps), because LVGL keeps some bytes per cycle on its own; the range switching and the node
  taps may add at most 512 bytes per cycle.
- `tests/host/test_alarm_state.c` covers the alarm state names, the arm and disarm actions an entity offers from its
  `supported_features`, the `code_format` and `code_arm_required` rules and the per-tile `alarm_actions` mask.
- Host tests need cJSON (`libcjson-dev`, `brew install cjson`, or fetched sources); `tools/test_host.sh` exits with 2
  when neither exists.
- `tests/host/test_coredump_store.c` checks two-report retention, failed imports, duplicate imports,
  raw downloads, metadata/diagnostic round-trips, corrupted payloads and stable-ID deletion.
  `tests/webui/coredump_reports.test.mjs` exercises individual erase clicks, confirmation,
  status refresh, errors, banner identity and compatibility with single-report firmware.
- `tests/host/test_ha_controls.c` covers parent-area inheritance, missing/cyclic parents,
  empty/custom names, bounded effect and suction lists, overflow preservation and escaped service values.
  The SDL `--regression controls` test taps the real Light/Vacuum controls, checks service payloads,
  failed sends, unavailable devices and popup cleanup during state changes, hiding and deletion.
  Control choices retain at most 24 complete values shorter than 96 bytes; compaction drops
  trailing choices to fit the existing attribute buffer without losing brightness or vacuum capabilities.
- `--regression fonts` (font manager failure acceptance, `tests/simulator/font_budget_regression.c`) requests every integer size and every icon font
  with `UI_FONTS_TEST_HOOKS` compiled in: a failed creation and the 64-instance budget never give a caller a NULL font, instances stay bounded and a
  key created before keeps its instance. `GET /api/version` reports the same counters under `fonts` on a panel.
- With SDL2, `check.sh` runs the regressions `controls`, `wallbox`, `bambu`, `history`, `energy`, `fonts` and
  `notification` through `tools/simulator.sh <variant> --regression <name>` for panel4, paneljc and tab5.
  `--regression bambu` exercises the real widget buttons, mocked service targets, pending commands, timeout/disconnect and popup
  ownership during hide, resize and deletion. It sends no Home Assistant commands. `--regression history` runs
  alongside it and drives the history popup's real 24 h / 7 days buttons, quick switching, close and reopen, and
  the entities that get no selector. `--regression energy` taps the real Gas, Water
  and Solar circles of the Energy page: popup versus right-hand panel (only where it fits), nodes without a source,
  a new selection replacing the old one and the page being deleted with a popup or selection open.
- `check.sh` lists checks it could not run (no Node, no cmake/SDL2) under "not run". With `CI` set, a missing
  prerequisite fails the run. Pass the editor tests as files: Node 22 (CI) treats a directory as a module.

Storage regressions use the actual settings/layout stores and automatic-update worker with injected NVS, file, lock and allocation failures. They cover credential generations and migration, rollback-version retention, queue saturation, atomic history replacement, graph quotas and orphan cleanup. Transfer deadlines cover silence and continuous slow progress.

## Widget layout check

`python3 tools/check_widget_layout.py` renders every gallery widget in the simulator in all four styles and reads each
object's rectangle (`--dump-layout`). It fails when two texts overlap, a text overlaps a button or lies half on a panel,
bar or picture, or anything reaches outside its tile. `check.sh`, CI and `tools/generate_simulator_gallery.py` run it
(panel4; `--variant paneljc|tab5` for the 1280 px geometry; the panel4-only readability fixtures are skipped there). It
also renders the Matte comparison (`tests/simulator/matte_reference.json`) and edge cases
(`tests/simulator/layout_cases.json`: tiles at minimum and large sizes and in every variant).

- Rectangles cannot see a ring's shape, and a widget that leaves content out passes: look at the images for text on
  arcs and knobs and for whether a tile still shows what it is for.
- A new or changed widget layout needs a tile in the gallery fixture (`tests/simulator/gallery_data.json`,
  `build_spec()` in `tools/gallery_content.py`) and its minimum size and variants in `layout_cases.json`.
- Two LVGL traps behind most findings: an object placed with `lv_obj_align()` keeps that anchor, so a later
  `lv_obj_set_pos()` offsets from it (use `lv_obj_align(obj, LV_ALIGN_TOP_LEFT, x, y)`); sizes read right after creation
  are 0 until `lv_obj_update_layout()` runs.
- The [simulator](../simulator.md) renders a local layout and state fixture at the panel's pixel size. Compare with a
  device screenshot before calling a result hardware-verified.

## Semantic readability checks

The simulator JSONL dump reports role, font identity (including Poppins fallback wrappers), nominal size, line height,
H/x/0 ink boxes and H angles at 30/40 cm (71.93 mm active panel4 width). `tools/check_readability.py --strict
<dump.jsonl>` checks reviewed essential text for clipping and capital height below the 16 arcminute reference; compact
20 px fits are reported and unassigned labels stay diagnostics. Font height alone does not establish comfort. The layout
check includes `tests/simulator/readability.json` (saved geometry, lower-density alternatives, negative and large
values, German titles, unknown and unavailable states, missing dates, rain) and two `readability_history_*.json`
fixtures for numeric and discrete details.

## Screenshots

`tools/screenshot.sh <panel-ip> [out.png]` saves what the panel shows (popups and the update screen included) via
`GET /api/screenshot.bmp`. Do not give `curl` a short timeout: the image is about 3 MB on the 10-inch panel and a timeout
truncates it silently.

## Device page captures

`tools/capture_device_pages.py <panel-ip> [--reference-dir DIR]` visits every page of the saved layout, saves each
framebuffer as PNG and writes `manifest.json` (firmware commit, layout hash, with `--reference-dir` the SHA-256 of the
compared images). It never overwrites an existing set and returns the panel to its first page. Then
`tools/import_device_captures.py [--board panel4|paneljc] --from <set>` replaces `docs/img/<board>/` and rewrites
`docs/gallery-<board>.md` (one current set per board). Keep household captures and their metadata under ignored
local storage, such as `.local/`, rather than committing them. Only import captures made with public synthetic
data and owned or licensed artwork; inspect every image and the manifest before publication. The maintained
documentation examples use the simulator's public fixtures.

## Gallery

`python3 tools/generate_simulator_gallery.py` regenerates the widget images of `docs/gallery.md` with the
[simulator](../simulator.md) from `tests/simulator/gallery_data.json`, in each style. It needs no panel and no Home
Assistant; re-run it after every change to a widget's look. Each tile is rendered alone on its own page (free size up to
720 × 600); the packing in `build_spec()` only matters for a capture on a real panel. `--variant paneljc|panel10|
paneljc-v2|tab5` renders only the house and energy pages at that geometry into `docs/img/gallery-<variant>/` and
`docs/gallery-sim-<variant>.md`. The special pages come from the simulator too: House from the fixture's `house`, Energy
from its `energy` sensors, the doorbell popup from its `doorbell` notification with a placeholder camera picture
(`tests/simulator/doorbell_camera.png`, via a `file:` entity_picture).

- Both generators share `build_rows()` in `tools/gallery_content.py`. The fan rotor lands differently on each run:
  discard those three images when nothing else changed.
- `tools/generate_gallery.py <panel-ip>` compares against hardware: it screenshots every widget and page on a real
  panel4 against real entities (`tools/gallery_entities.json`, gitignored, copy the `.example.json`), backs up and
  restores the panel's layout and style, and replaces the simulator images until the next simulator run. Optional extras
  (car precondition row, segmented gauge) render only with compatible entities configured; a missing extra is silently
  left out.
- Never hand-edit the generated files or `docs/img/gallery/*.png`. Image names are stable
  (`<slug of the title>_<style>.png`), so a commit touching one widget changes only its images. A run prunes unreferenced
  images and writes `docs/img/gallery/sources.json`, a hash stamp of every rendering input. `check.sh` runs
  `tools/check_gallery_fresh.py`, which warns (never fails; `--strict` fails) when sources changed since the stamp.
  Regenerate only on that warning and commit the images separately as `docs(gallery): ...`. After a refactor that did
  not change rendering, `generate_simulator_gallery.py --stamp-only` refreshes the stamp.
- The published gallery contains demo-data widgets and special pages from the simulator. Real household pages captured
  with `tools/capture_device_pages.py` remain local test evidence.

## Memory and stability stress test

`tools/stress_test.py <panel-ip>` combines rapid page navigation, repeated entity discovery (Room/House import) and
concurrent loads of every editor asset in `index.html` (six connections, `--webui-concurrency`). It reports memory from
`GET /api/version` before, during and after a cool-down, counts failed requests and exits nonzero on any failure or
memory probe. `--no-navigate --no-discovery` isolates the editor load, `--no-webui-load` reproduces the older test.
A one-time cache allocation drops once and stays flat; a leak keeps dropping after the load stops. It only does GETs and
page switches and never touches USB. Errors under combined load count as failures even if the panel recovers.

## Bottom navigation: full and compact tabs

`ui/nav_layout.c` (pure, host-tested) places the tabs, `ui_pages.c` draws them. Tabs are **full** (icon and title) while
each is at least `NAV_FULL_TAB_MIN_W` (112 px) wide: 5 pages on the 4-inch bar, 10 on the 10-inch bar
(`nav_layout_full_pages()`, `full_nav_pages` in `/api/version`). Beyond that the bar is **compact**: the shown page keeps
icon and title (at most a quarter of the bar), the others are 64-96 px icon tabs (the page number without an icon).
`nav_layout_max_pages()` is the limit at 64 px: 8 pages at 720 px, 12 at 1280 px (`APP_MAX_PAGES`, `max_pages`). 64 px is
6.4 mm on the 4-inch panel (254 ppi) and 10.8 mm on the 10-inch panel (150 ppi). The editor asks for a distinct icon per
page once the bar is compact (`navIconIssue()`); the simulator draws every icon as the list glyph, so judge icons in the
editor or on a panel. More tabs would need a "More" list or lazy page building (owner decision 2026-10-01: not wanted).
The widget total stays 224 (`APP_MAX_WIDGETS_TOTAL`).
