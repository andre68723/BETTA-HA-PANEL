<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Agent guide: architecture and development

BETTA HA Panel is ESP-IDF firmware for ESP32-P4 touch panels. An
embedded browser editor configures a Home Assistant dashboard at runtime. This
fork starts at upstream commit `420df764` (v0.8.2); user-facing changes are
grouped under v0.9.0 in `docs/release-notes.md`. The intended local fork version
is v0.9.0.

## Essential commands

| Task | Command |
| --- | --- |
| Fast checks: editor, host C tests and SDL layout | `tools/check.sh` |
| Layout at a larger text step | `python3 tools/check_widget_layout.py --variant tab5 --text-step 2` |
| Render one LVGL fixture locally | `tools/simulator.sh panel4 --fixture <file> --page <id> --style <style> --screenshot <file>` |
| Regenerate the widget gallery | `python3 tools/generate_simulator_gallery.py` |
| Validate the documentation, as in CI | `uv run --locked --only-group docs zensical build --clean --strict` |
| Regenerate individual widget guides | `python3 tools/generate_widget_docs.py` |
| Refresh dashboard/editor examples | `uv run --locked --group screenshots python tools/generate_docs_examples.py` (focused options in `docs/documentation.md`) |
| Build both locally exercised P4 variants | `tools/check.sh --build` with ESP-IDF 6.1 active |
| Build one board | `tools/build.sh <variant> build` with ESP-IDF 6.1 active |

## Read by task

| If changing | Read first |
| --- | --- |
| Widget rendering, fonts, layout or gallery | `docs/appearance.md`, `docs/appearance-implementation.md`, `docs/widget-anatomy.md`, `docs/simulator.md` and `docs/development/testing.md` |
| Browser editor or theme preview | `docs/editor-source.md`, `docs/appearance.md` |
| Board drivers, memory or flashing | `docs/choose-display.md`, the matching board/resources/storage guide in `docs/hardware/`, the relevant page under `docs/development/` (build, release, checks) |
| Home Assistant, MQTT or API behaviour | `docs/home-assistant.md`, `docs/notifications.md` when applicable, relevant `main/ha/` or `main/api/` code |
| User-visible behaviour | The matching topic and v0.9.0 entry in `docs/release-notes.md` |
| Documentation, screenshots or GitHub Pages | `docs/documentation.md` and the matching topic; `zensical.toml` for the current navigation |

Read only the documents relevant to the task. `docs/README.md` indexes the focused documentation.

## Documentation and examples

`docs/` Markdown and `zensical.toml` are the documentation sources. Keep selected
examples in the root README and detailed instructions in the site. Zensical uses
uv's locked `docs` group; `site/` is generated output, not a source to edit or
commit. There are no separate gallery HTML exports. Preserve direct `.html`
navigation and offline search; check changed pages over HTTP and `file://`, at
desktop and mobile widths, including image loading, zoom and horizontal overflow.

Keep user setup and behaviour in user guides, and source paths, wiring and
implementation details in Development references. The site has two audiences: Home Assistant users
(Get started, Use the panel, Examples) and developers (Development, see `docs/documentation.md`); build commands
live in `docs/development/build.md`, device pin and schematic notes in `docs/hardware/device-notes.md`. A new widget needs
an entry in `GROUPS` of `tools/generate_widget_docs.py` and a card in the feature tour. Keep a prose page under about
250 lines and split it into sub-pages instead of growing it (`tools/check_docs_size.py` fails above 300 lines). Check the navigation before
moving a page: `docs/hardware/peripherals.md` is already under Development. Device
selection uses one comparison ordered by display size; device guides start with
an illustration, share a section order and include Manufacturer references.
The flashing instruction should use the connector label from that illustration.

Documentation style: write like a person, tersely. A heading or table cell names the thing ("Widget guides", "Checks",
"Build and flash"), not a sentence about it: no leading "The", no "Look at ...", "What it does", "Where to read", "Good to
know", "At a glance", "I want to ..." or "Do you ...". No marketing tone ("The numbers"), no filler, no card grids or tables
with long prose cells where a list or tabs read better. Developer pages state only what is needed to develop. Keep release
history out of user and developer pages: describe the current state and the difference to upstream v0.8.2, not earlier
release candidates. Keep page title, navigation label and file name the same words.

`docs/widgets/*.md` are generated: change `tools/generate_widget_docs.py` and the
shared gallery metadata, then regenerate. Capture UI examples with the real
LVGL/SDL renderer or the current editor against public synthetic data. Match the
fixture, device geometry and enabled extras to the feature being demonstrated;
compact layouts can hide an extra even when its sensor is configured. Preserve
aspect ratios and relative pixel scale, and label simulator examples separately
from dated device captures. Hardware illustrations have their sources and
generation prompts in `docs/img/hardware/README.md`.

Compare release claims with upstream `420df764`. JC and Tab5 support was added
in this fork: there is no upstream hardware baseline for either. Label measured
values with the device, date and test scope; comparisons between fork builds must
say so. Verify toolchain claims against the manifests, lock files and build setup.

## Architecture

| Area | Main paths | Responsibility |
| --- | --- | --- |
| Boot and coordination | `main/app_main.c`, `main/app_events.*` | Initialize storage, display, network, editor and integrations; pass state changes and commands to the UI task. |
| Persistent configuration | `main/settings/`, `main/layout/`, `main/ui/theme/` | Runtime settings and authentication in NVS; layout, themes and translations in LittleFS. `layout_validate.c` validates layouts; `widget_types.c` defines shared widget limits and domains. |
| Network and integrations | `main/net/`, `main/ha/` | Wi-Fi (ESP-Hosted C6), time, MQTT, Home Assistant WebSocket/REST, entity state and service calls. |
| Device HTTP API | `main/api/` | Serve the editor and `/api/*`; `http_guard.c` enforces authentication and request limits. API handlers publish UI events where needed. |
| Display | `main/ui/`, `main/drivers/` | Shared `esp_lvgl_adapter` 0.7.3 runtime in `drivers/display_port.c`; LVGL pages and widgets, rendering styles, board-specific display/touch drivers and the optional JC camera sensor. Brightness, dim timer and wake behaviour are shared by all boards in `drivers/display_brightness.c` (decisions in host-tested `display_policy.c`); a board file only writes the backlight. |
| Browser editor | `components/webui/www/`, `components/webui/CMakeLists.txt` | Plain HTML/CSS and ordered classic JavaScript source modules. The build generates one fingerprinted JS bundle and CSS asset (not checked in), then gzips and embeds three assets; `docs/editor-source.md` maps the modules. |
| Build and checks | `config/`, `tools/`, `tests/host/`, `.github/workflows/` | Variant configs and partitions, dependency patches, local builds, browser checks and pure-C host tests. |

The normal data path is Home Assistant/MQTT/Wi-Fi -> model and event queue ->
`ui_runtime.c` -> LVGL widgets. The editor reads and writes settings and layout
through `/api/*`; a layout update triggers a UI rebuild. Only the UI task, or
code holding the display lock, may call LVGL. ESP-Hosted Wi-Fi RPCs must use
the serialization in `wifi_mgr.c`. Keep large buffers out of internal RAM and
large structs off the main task stack; see `docs/development/learnings.md` for the measured limits.
The HA transport/task state stays in `main/ha/ha_client.c`; domain attribute
compaction and forecast extraction live in `main/ha/ha_attr_serialize.c`, with
host tests. Layout-driven HA subscription selection lives in
`main/ha/ha_layout_entities.c`, the HA area/floor registry state in
`main/ha/ha_room_registry.c`, the entities' display precision in `main/ha/ha_display_precision.c`,
the editor's entity-discovery domains (a new domain is a table entry) in `main/ha/ha_discovery_domains.c`;
registry fallback names and parent-device areas use host-tested `main/ha/ha_registry_discovery.c`,
and bounded effect/fan-speed choices and escaped service values use `main/ha/ha_control_options.c`; `ha_client.c` still loads layout/settings and
logs overflows. Weather-widget attribute parsing lives in
`main/ui/widgets/weather_value_parse.c`, separate from LVGL rendering.
Recorder statistics (the 7-day history popup and the Energy page's Gas, Water and Solar details) are parsed by the pure, host-tested `main/ha/ha_history_stats.c`
(time in UTC, day boundaries in the local time zone) and fetched one request at a time by the worker `main/ha/ha_history_week.c` over the generic WebSocket helper;
`main/ui/ui_days_view.c` draws the daily bars for the popup and for the right-hand panel (`energy.details` of an Energy page). Measured reply shapes are in
`docs/development/learnings.md`, "Recorder statistics".
The weather tile is split by responsibility into `w_weather_tile.c` (entry points) and `weather_tile_{lottie,text,icon,values,forecast,layout,popup,render}.c`
with shared types in `weather_tile_internal.h`; after a refactor of rendering code, prove parity by hashing SDL renders (`SDL_VIDEODRIVER=dummy`
and the `--screenshot` option of `tools/simulator.sh`) before and after, as was done for that split.
House pages are laid out by `main/ui/ui_house_page.c` and energy pages configured by
`ui_energy_page_config_from_json()`, both shared by `ui_runtime.c` and the desktop simulator.
Keep handwritten modules focused: aim for a few hundred lines where a cohesive
responsibility allows it; a file over 800 lines deserves review. The 3,000-line
check is a hard ceiling, not a target. A few larger legacy files have frozen
ceilings in `tools/check_source_sizes.py` until they can be split by responsibility.
Do not treat generated build-only WebUI bundles as source modules.
Every font is an `lv_tiny_ttf` instance made at run time from four committed subsets (Poppins Medium text, Poppins Bold notification emphasis, Material Design Icons, Noto Emoji
monochrome) by the font manager `main/ui/text_font.c`; no bitmap font is generated and `lv_font_conv` is not used. Text asks for a semantic role
(`UI_TEXT_META/LABEL/VALUE/KEY/HERO`, `ui/theme/ui_typography.h`), never for a pixel size: the size follows the role, the board profile and the user's text
size step (`ui/text_scale.c`, pure and host-tested). Widgets lay out from `lv_font_get_line_height()` of the font in use, not from fixed offsets; a tile that is too small
for larger text keeps a smaller text through `text_scale_tile_cap()` (calibrated by `tools/calibrate_text_caps.py`). Symbols are `APP_SYMBOL_*` strings drawn with
`APP_FONT_SYMBOL_*`, not `LV_SYMBOL_*`. Sizes and metrics, the regeneration of the subsets and the emoji policy are in `docs/appearance-implementation.md`, `docs/appearance.md` ("Text size")
and `main/ui/fonts/README.md`.

Memory and stability rules (measured, details in `docs/development/learnings.md`, "Memory and heap"). Internal RAM is the scarce
resource: Wi-Fi/SDIO buffers, TLS and task stacks need it, so LVGL allocates from PSRAM first on every variant
(`ui/lvgl_alloc.c`, set in `config/sdkconfig.defaults`; measured on `paneljc` and `panel4`, `panel10` not yet). Size a task
stack from `GET /api/version?tasks=1` after exercising the panel, move stacks of tasks that never touch flash to PSRAM
(`xTaskCreateWithCaps`, ending with `vTaskDeleteWithCaps(NULL)`) and keep the HTTP task at 14 KB (the screenshot endpoint needs 11).
Never wait forever on hardware or a lock while holding the display lock: the paneljc UI froze (2026-10-05) in the PPA rotation's `portMAX_DELAY`;
the shared `drivers/display_port.c` adapter callback runs PPA non-blocking with a one-second deadline and retains the crash hook.
The older `esp_lvgl_port` dependency patch remains for BSP compatibility, not the running graphics task. A UI hang leaves a core dump (`util/ui_watchdog.c`); how to read it is in
`docs/coredump.md` and `docs/development/learnings.md`, "UI stall watchdog". A heartbeat timeout aborts in `ui_watchdog`
(inspect the other tasks' stacks); a PPA timeout aborts in `lvgl` (`taskLVGL` in older images). `Illegal instruction` at `panic_abort` can be a deliberate
abort, so check the panic reason, diagnostic cause and matching backtrace before assuming pointer corruption.
Crash retention is two reports in LittleFS (`util/coredump_store.c`, boot/API glue in `util/coredump_archive.c`);
the raw partition is erased only after a verified archive import. Collect `/api/version?tasks=1`, `/api/log`,
`/api/reboot-log` and each `/api/coredump?id=<id>` before resetting a responsive panel. IDs are raw-dump SHA256,
not list positions; download/delete by ID. Match each report's `elf_sha256` to `.local/elf/<variant>/`, not the current
running build. RTC diagnostic evidence (`util/coredump_diagnostics*`) records PPA geometry/buffers and the heartbeat
stall cause before abort. Keep the store host tests, per-report editor actions and display warning count in step.
Every deliberate `esp_restart()` calls `reboot_log_mark(<cause>)` first (`util/reboot_log.h`, append-only causes); read
`GET /api/reboot-log` before assuming a hang is unexplained. The task watchdog is not enough for a dead link behind a live
screen: `util/net_watchdog.c` restarts on a dead link, a silent gateway or an unresponsive web server, bounded to three restarts in a row.
Layouts: `APP_LAYOUT_MAX_JSON_LEN` is 32 KB on every board (the 224-widget total with real titles); build time no longer limits it (224 widgets take 3.6 s on the
paneljc since the state pass is one pass per widget; the per-entity pass was quadratic and took 25 s). The page limit comes from the bottom bar, not memory: `ui/nav_layout.c` decides full tabs
(icon and title) or compact tabs (shown page with title, others icon or number), 8 pages on the 4-inch bar, 12 on the 10-inch.

## Variants and build

Use `tools/build.sh <variant> <idf.py arguments>` with ESP-IDF 6.1 activated.
Variants: `panel4`, `panel10`, `paneljc`, `paneljc-v2`, `tab5` (one M5Stack Tab5 ST7121/v1.3 unit began device trials on 2026-10-05; see `docs/choose-display.md`). This wrapper
selects the target, config overlays and `build/<variant>/`; a bare `idf.py`
can select the wrong flash configuration. The root `CMakeLists.txt` selects a
variant manifest and applies `tools/apply_component_patches.py` to fetched
components during configure. `main/idf_component.yml`, `managed_components/`
and `build/` are generated or fetched, not primary sources. The root
`dependencies.lock` is shared by `panel4` and `panel10`; building a
different one can rewrite it. JC variants use `dependencies.lock.paneljc`;
Tab5 uses `dependencies.lock.tab5`.

Panel4 selects the official BSP 3.0.1 source under `third_party/`; the override changes only its
adapter constraint. Preserve GPIO23 reset and the GT911 address probe. Read both BSP and panel
constructor when migrating: ST7703 2.0.0 already enables IDF 6 DMA2D, and a second enable causes
INVALID_STATE and OTA rollback. The shared adapter profile keeps rotation zero; the bitmap callback
uses LVGL's current rotation so Tab5 boot/runtime orientation and native touch mapping stay paired.

Run `tools/check.sh` for web checks and host tests, and `tools/check.sh --build`
for the local `panel4` and `paneljc` builds. Host tests run without a prior
ESP-IDF build, using a system `libcjson` package or fetched cJSON sources.
Firmware builds and host tests do not establish hardware behaviour. Check the
specific board on device before claiming it is verified. `panel10` and
`paneljc-v2` have only been compile-checked in this fork. The ESP32-S3 board (`panels3`) was removed: no hardware, and its 480 px screen
cannot hold the newer widgets. See `docs/choose-display.md` for variant
limits and `docs/release-notes.md` for release gaps.

For an unresponsive network, identify the board before touching USB. Opening
the serial port may itself reset it and erase volatile evidence. If HTTP works,
collect `/api/version` and `/api/log` first. A documented recovery for a
nonresponsive 16 MB `paneljc` is `esptool --port <port> flash-id`: it reads the
flash ID and then hardware-resets the panel without writing firmware. A
paneljc network freeze (2026-09) was resolved by the memory work and has not recurred; the history is in `docs/development/learnings.md`.

## Change map

- Widget layout: `tools/check_widget_layout.py` (part of `tools/check.sh`, run for the panel4 and the paneljc
  geometry, covering the gallery and the edge cases in `tests/simulator/layout_cases.json`) must pass, and
  the gallery regenerates with `tools/generate_simulator_gallery.py`; see
  `docs/development/testing.md` for what it checks and the LVGL positioning traps. Text size: render other steps with
  `--text-step 1|2`; after a layout or widget change that affects text growth run `tools/calibrate_text_caps.py --write`
  (tab5 for Panel4/Tab5, `--variant paneljc` for the 10-inch boards) and commit the table in `main/ui/text_scale.c`.
- New widget: update `main/layout/widget_types.c`, type-specific validation,
  `main/ui/ui_widget_factory.c`, the widget source and `main/CMakeLists.txt`;
  account for its Home Assistant entity subscriptions in `main/ha/ha_layout_entities.c`.
  In the editor, update the picker configuration, Add menu, sizes and preview.
- Widget or editor-preview change: keep `tilePreviewMarkup` and the panel widget in step (rule in `docs/editor-source.md`);
  `.local/skills/preview-parity/SKILL.md` audits the drift on demand and is not part of `tools/check.sh`.
- Editor text: a hint longer than 100 characters in the Inspector is shortened to its first sentence plus an info button by
  `js/inspector-hints.js`; keep hints short anyway. A gauge hides its name when it is too narrow (`autoGaugeName` in `layout-model.js`).
- New editor file: add it to `WEBUI_JS_SOURCES` (or a stylesheet to
  `WEBUI_CSS_SOURCES`) in `components/webui/CMakeLists.txt` and the ordered
  script (or stylesheet link) list in `components/webui/www/index.html`. New text needs
  English and German keys in `components/webui/www/js/i18n.js`.
- Device tests without touch: `POST /api/ui/tap` opens a read-only popup, `/api/navigate` switches a page, `/api/screenshot.bmp`
  and `tools/capture_device_pages.py` capture it; before a risky layout test back up `GET /api/layout` to `.claude/layout-backups/`
  (ignored) and keep the USB recovery in mind (`esptool flash-id` resets a hung panel, then `PUT` the old layout).
- Navigation or page limits: `ui/nav_layout.c` (pure, host-tested), `APP_MAX_PAGES` in `app_config.h`, the editor's `maxPages()`
  and `navIconIssue()` and `/api/version` (`max_pages`, `full_nav_pages`) stay in step.
- User-visible behaviour: update the matching topic in the v0.9.0 section of
  `docs/release-notes.md`. Use `docs/README.md` to find the
  focused hardware, integration, appearance and widget documents.

## Efficient checks, builds and device updates

Standing owner authorization (2026-10-12): read-only device diagnostics, including version/task status, logs,
reboot history and crash-dump downloads, matching-ELF decoding, local checks and firmware builds, and the cache/network
access these checks need are pre-approved. Proceed without another confirmation. This does not authorize flashing,
device resets, pushing, deleting retained crash reports or changing Home Assistant.

For interactive browser checks, prefer Safari MCP. Use text extraction and batched DOM interactions for speed and
token efficiency; capture screenshots when visual appearance needs inspection. Set the visible browser window to
90% of the available screen width and height and verify its actual size. A separately controlled MCP test view does
not prove that the visible Safari window was resized; check both when they differ.
For settings grids, check each visible control against its own group's bounds at wide and narrow widths,
with optional controls enabled. A page without horizontal overflow can still have overlapping groups.

Batch related edits before building or flashing. Start with the smallest meaningful
check for the changed responsibility: Inspector-only JavaScript changes use
`node --test tests/webui/*.test.mjs` and `python3 tools/check_web.py`. Run required
host, SDL, documentation and release gates when relevant, but retain successful
results and repeat only checks affected by new changes, failures or unresolved
concerns. Do not repeat firmware builds for documentation-only edits; clearly
record the source commit actually embedded in each image.

If the system Python version changes, activate the existing ESP-IDF Python environment before `export.sh`;
verify `idf.py --version` before building instead of installing a second environment unnecessarily.

Use incremental `tools/build.sh <variant> ...` builds only for the requested
device(s); reconfigure when configuration, dependencies or release source metadata
require it. Finish the relevant checks and source changes, then build and flash
once. A requested single-device build does not authorize building other variants.
Package finished builds without rebuilding them. Preserve task logs under
`/private/tmp/betta-<task>/`, and report exit codes, pass/fail totals and relevant failure
context. Search focused sections rather than reread whole files or successful logs.
Batch independent reads/checks; serialize builds sharing managed dependencies.

After an authorized flash, verify the image's source stamp, OTA validation,
connection and changed behavior. A successful upload and reachable HTTP can still be the old rollback
image; compare the actual running build. `/api/screenshot.bmp` is an LVGL snapshot, not a readback
of the physical DPI framebuffer, so it does not prove hardware rotation, colors or touch calibration.
The bounded log can lose startup lines; a missing line alone is not an initialization failure.
Do not routinely add hardware stress tests to
every flash. Use them when the change/risk requires them or the owner requests
them; respect an explicit refusal. The owner declined the 10-inch stress test
on 2026-10-05.
For backlight checks, distinguish requested percentages, PWM duty/frequency readback and actual light output;
`/api/version` brightness is the last successful software write, not an optical measurement.

UI control tests must exercise the event -> model -> saved JSON path, including
enable/disable and reload, rather than only normalize pre-populated values.
`tests/webui/inspector_events.test.mjs` uses the real `bindUi()` and Inspector
handlers and checks every Inspector input has an editing event; it is included
in the existing Node test glob. Add relevant cases when introducing controls.
Keep firmware defaults and the editor's Reset to defaults behavior in step.

For settings UI changes, group controls by the user's situation (use, inactivity, night), not by internal API fields.
Keep one reset per settings page, scoped to every control on that page; use headings instead of nested group boxes.
Hide dependent controls when their feature is off, preserving their saved values across disable/re-enable and reload.
An automatic brightness policy needs an explicit preview to judge levels without waiting for its timer or schedule.
Temporary device tests must expire on the device even if the browser disappears; slider edits must not restart that deadline.
Test start/update/stop and actual settings saves separately, and make local simulations visibly distinct from hardware tests.


## Repository rules

Inspect `git status` before editing and preserve unrelated tracked and untracked work.
Keep build artifacts and local plans out of commits. Documentation PNGs and
generated widget guides are maintained assets; include affected outputs when refreshing them.
Keep only Markdown plans for open future work in `.local/plans/`, named
`plan-<topic>-MM-DD.md` (month and day, without a year). Remove completed
plans; put logs, captures, dumps, helper scripts and temporary reports under
`/private/tmp/betta-<task>/`, then clean them up when their task is finished.
Keep reusable project workflows as skills in `.local/skills/` (including
`peer-project-review`, `publication-review`, `hardware-diagrams` and `plan-maintenance`),
rather than as plan prompts. `.local/` is ignored as a whole.
For peer comparisons, read `.local/skills/peer-project-review/SKILL.md`; for a
publication review, read `.local/skills/publication-review/SKILL.md`; to check,
update, merge or rename the plans, read `.local/skills/plan-maintenance/SKILL.md`. Hardware
illustration work uses `.local/skills/hardware-diagrams/SKILL.md`.

Parallel agents in one checkout share the Git index: another agent can commit
files you have staged. Prefer separate worktrees for independent commits; when
sharing a checkout, coordinate staging and commits, including ownership of shared
files. Use explicit paths with normal `git add`, inspect the staged diff and scope
your commit to owned paths with `git commit --only ... -- <paths>`. That scopes
your commit but cannot stop another committer taking staged files. Do not unstage
others' work or rewrite their commits; verify the resulting commit's file list.

Read the relevant page under `docs/development/` (overview: `docs/development.md`) for detailed build, code,
memory and device guidance; its documentation table maps changes to topic files.
Keep source headers and third-party attribution intact (see `LICENSE` section 12).
Code, commits, comments and documentation are English; the maintainer talks to
assistants in German. Use Conventional Commit subjects with an English body that
explains the reason and architectural change, one logical change per commit.
Use `andre68723` as the author name in commits and fork file headers.

Do not push or flash a device unless the owner asks. The `origin` remote is
upstream; `fork` is the owner's remote. Never modify the `backup*` branches.
Read Home Assistant through the configured `ha-mcp` server only; keep access
read-only unless the owner asks for a change, and never print or store its token.
