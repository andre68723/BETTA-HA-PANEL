<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Web editor source

User guide: [Use the web editor](editor.md). This page covers the source.
Field-by-field domain and eligibility contracts: [Entity search rules](development/entity-search.md).

Plain HTML, CSS and JavaScript in `components/webui/www/`, no framework. For development serve that directory:
`index.html` loads the modules in order. The firmware build generates an `index.html` that loads one ordered bundle
(`app.<hash>.js`) and one fingerprinted stylesheet (`styles.<hash>.css`, the `WEBUI_CSS_SOURCES` concatenated in
`index.html` order, so the cascade is the same) and embeds those three files gzipped. They live only under
`build/<variant>/`. The HTML is never cached; the fingerprinted files are cached until the next firmware build.
`www/img/doorbell-example.png` (the Alerts example, offline) is a copy of `docs/img/gallery/gallery_doorbell_glow.png`:
refresh it when that image changes (asset declaration and copy are in the WebUI CMake file).

```
www/index.html        the page (loads the scripts below, in this order)
www/styles.css        base editor styles, the canvas and the layout preview shared by every style
www/css/editor-refinements.css  the later layer of the base: sentence-case UI pass, settings forms, light theme, add-widget menu, preview refinements
www/css/style-*.css   rules for one appearance style (depth: shared by Glow and Glass), after the base
www/css/readability.css  the readability controls' styles, after the style files
www/css/preview-fit.css  tile preview sizes that follow the tile (container units) and the looks matched to the panel
www/css/preview-roles.css  preview texts in the panel's text roles (`--r-meta` … `--r-key` from `/api/version`), loaded last
www/css/settings.css compact settings forms and the offline alert example
www/css/layout-toolbar.css compact layout header, icon actions and JSON menu
www/css/layout-actions.css widget context menu and page-transfer dialog
www/css/inspector.css compact Inspector fields, tabs and independently scrolling desktop lists
www/js/config.js      constants, the entity picker configurations
www/js/i18n.js        built-in English translations
www/js/i18n-de.js     built-in German translations
www/js/state.js       size limits, the `editor` state object, the DOM element map `el`
www/js/util.js        normalizers, layout normalization, status line, `t()`, storage
www/js/ui-basics.js   style cards, firmware info, HA diagnostics, canvas geometry
www/js/inspector-hints.js Inspector info icons and help popups; settings sentence summaries
www/js/translations.js loading and applying translations, language files
www/js/settings.js    settings pages: Wi-Fi, OTA, device status, alerts, backup, security, saving
www/js/brightness-test.js  temporary brightness tests: shared controls, serialized requests and device countdown
www/js/update-check.js the panel's daily firmware update check: banner, OTA URL, its two settings
www/js/entity-search-rules.js search eligibility by field and the rules shown in each picker
www/js/layout-model.js layout model, entity domains and suggestions, loading layout and entities
www/js/pickers.js     entity pickers, the "+ Widget" dialog (widget types and entity search)
www/js/readability.js panel4 role sizes, day-count and gauge-width hints for the previews (after pickers, before canvas)
www/js/bambu-printer.js Bambu printer widget: Inspector fields for its entity bindings and the canvas preview (before canvas)
www/js/evcc-wallbox.js wallbox bindings, loadpoint discovery, optional power/flow header and its preview
www/js/stock-quotes.js Stocks & ETFs: quote ordering and Inspector options
www/js/energy-timeline-colors.js energy flow palette, compact color controls and HEX editing
www/js/tile-previews.js  the schematic tile previews (solar, car, Sonos, `tilePreviewMarkup`) and the house page grid (before canvas)
www/js/layout-history.js bounded layout/selection snapshots, edit grouping and Undo/Redo
www/js/widget-actions.js validated page transfers, widget menu and target dialog
www/js/energy-canvas-preview.js  the Energy page's schematic preview on the canvas
www/js/canvas.js      pages list, canvas, inspector, page and widget operations
www/js/inspector-layout.js Inspector presentation: tabs, optional groups, wrapping entity fields and automatic Appearance fit
www/js/room-import.js room import: area pick, role assignment dialog with preview, zone layout
www/js/bind.js        event binding, editor start, bootstrap
www/js/theme.js       theme editor
www/js/menus.js       dropdown menus
www/js/main.js        calls `bootstrap()` once everything has loaded
```

The scripts are classic scripts sharing one global scope, run in `<script>` order: top-level code may only use what an
earlier file defines (calls inside functions run later). `bundle_assets.py` keeps that order and inserts semicolon
boundaries; `tools/check_web.py` checks order, syntax, fingerprinted names and gzip round-trips.

## Adding a file

1. Create it under `www/js/` with the licence header of the others.
2. Add it to `WEBUI_JS_SOURCES` in `components/webui/CMakeLists.txt` and to the `<script>` list in `www/index.html` in
   the same position. A stylesheet goes into `WEBUI_CSS_SOURCES` and the `<link>` block the same way (order is cascade
   order; style files come after `styles.css` and `css/editor-refinements.css`).
3. `scripts/check.sh` fails when the lists, tags and `www/js/` disagree or the bundle breaks, and runs the editor tests
   (`node --test tests/webui/*.test.mjs`). `load_editor.mjs` loads the scripts into a Node `vm` context with a small DOM
   stub and derives production order from `index.html`, excluding only the browser bootstrap `main.js`.
   Handwritten sources warn above 800 lines and fail above 2,999.

## Local test server

Run the current editor against synthetic data, without a panel, Home Assistant or ESP-IDF:

```bash
scripts/editor_local.sh
```

The shell entry point starts `tools/editor_local.py` and forwards all arguments and signals.
Once the server starts, press Enter in the terminal to open the default browser, or open the printed URL manually
(`http://127.0.0.1:8766/` by default). No `ui` parameter is needed; `/` uses the view chosen at startup.
Sources are served directly from `components/webui/www/`; refresh after edits.
Only Python's standard library is needed. The default wide-board demo contains every widget type, optional
bindings and house/energy pages on eight pages. Save writes `.local/editor-local/paneljc-all-widgets-state.json`; Export downloads
JSON. The local toolbar identifies the test environment. Ctrl+C stops the server.

- `--variant panel4|panel10|paneljc|paneljc-v2|tab5` selects geometry, page limits and text-role metadata.
- `--fixture path/to/layout.json` replaces the demo layout; an optional `states` array supplies synthetic readings.
  Layout-only files use the demo states, so unknown bindings show missing readings.
- `--demo all` explicitly selects the default all-widgets layout for wide variants.
  `--demo compact` selects the smaller example with its own save file. Panel4 defaults to compact;
  its small geometry does not support the all-widgets layout.
- `--state-file path` isolates saves; an existing save takes precedence over the fixture.
  `--reset` replaces that save with the supplied/default fixture. The default save file is separate per variant.
- `--port 0` selects a free port, printed on startup; the default is 8766.
- Non-interactive starts print the URL without waiting for keyboard input or opening a browser.
- `--ui spike` opens the bundled local prototype and enables the **View** selector for comparison with Classic.
  `--spike path/to/adapter` overrides its sources (`spike.css`, `widget-icons.js`, `spike.js`).

Both views share the editor sources and API. The local view selector transfers the in-memory layout and selection
through session storage, including unsaved edits, without saving to the API. Both views use the shared layout history, which resets
on a view change. View preferences and transfer data never enter layout JSON.

The demo reuses widget templates and synthetic states from the Gallery and documentation examples, with its own
page arrangement and extra bindings for Inspector testing. Documentation captures are generated separately;
existing local saves retain their edits until an explicit `--reset`.

The API serves local fixtures and accepts local layout/UI-setting saves only. Device commands, firmware uploads,
network setup and HA service calls are unavailable. Brightness tests are explicitly labelled as local simulations; their transient state is not saved. There is no device proxy; external connections are blocked by
CSP. Its validation checks page/widget/JSON limits, not the firmware's complete contract. Geometry/text metadata
follow current firmware profiles; the browser preview still approximates LVGL and does not establish hardware behavior.

## Parallel development

The experiment lives under `tools/editor_local/spike/`, outside the firmware's CMake asset lists. Its workspace,
labelled Save/Export, widget icons and prototype history are injected only by the local server. Work on these
files can proceed alongside firmware changes. Local fixtures/saves, plans and browser captures stay under `.local/`.
Changes in the shared `components/webui/www/` sources affect both the local editor and the next firmware build.
Keep prototype work in the adapter until explicitly integrating it. Production coexistence needs a reversible
workspace adapter over one model, validation/save path and history; the local reload switch is an experiment.

## Device-backed preview

For live readings, serve `www/` on localhost and proxy `/api` to a panel; saves then go to that panel.
Back up its layout or make the proxy read-only. The local test server above does not provide this mode.
Browser smoke test on a flashed panel: check board, assets and canvas, then Settings → Device, Network,
Home Assistant and back without saving. Check for console errors and the generated
`styles.<hash>.css` and `js/app.<hash>.js`. Device workloads and evidence collection are in
[Checks and device tests](development/testing.md).

## Rules for the editor

Entity searches expose unit, state class and source flags through both `/api/entities` and the on-demand discovery API.
Energy timeline fields distinguish cumulative counters, battery SOC and Solcast data; price sources require a supported
time/price list. Keep the popup, autocomplete and local demo metadata aligned. Empty filtered results must never fall
back to all sensors. The popup explains its domains, hard restrictions and soft ranking separately from loading status;
saved incompatible bindings remain visible with a warning.

Device-class metadata is shared by the Inspector and previews through `entityDisplayInfo()`, which merges the entity
list with live state attributes. A missing field in either response must not mask a class or unit from the other.
The class badge supports HA's class-capable platforms and preserves unknown reported class names. Locks and scenes
without a class do not get a misleading "none" badge. Domain-specific labels distinguish binary Power from sensor
Power, and cover classes such as Blind from numeric measurements.

| Use | Device-class effect |
| --- | --- |
| Sensor widget | Timestamp/date/duration format; monetary, temperature and power/energy unit choices; enum labels |
| Binary/contact widgets | State words, contact drawing and alert category; events use their trigger timestamp |
| Gauge and room readings | Numeric values retain their units; distance extras scale metres to kilometres; room summaries classify temperature, humidity, battery and contacts |
| Controls | Class is descriptive; actions and optional controls follow domain, supported features and state |

Do not copy Sensor format controls to actor widgets: a cover's position and a player's volume are percentages,
not the entity's class value. Unsupported domains do not gain widget controls just because they report a class.
The shared Gallery fixture supplies explicit classes; documentation captures must not patch them independently.

- **Little scrolling.** At about 80-90 % of a desktop screen, finding a widget, editing its main fields and saving must
  not need trips between distant regions. Review Device and Layout at that size when adding controls; group or fold long,
  rare options.
- **Texts:** keys in the `en` and `de` tables of `WEB_I18N_BUILTIN` (`js/i18n.js`, `js/i18n-de.js`). Keys and interpolation variables must match; an empty string is allowed only
  for the hidden disabled-update state. Dynamic colour-editor, time-zone and entity-picker labels use the same tables.
  Widget registration: [New widget types](development/conventions.md#new-widget-types).
- **Text size** (`settingsUiTextStep`, beside the save controls) applies live and refreshes the preview and role-size
  hint; resetting Appearance keeps it.
- **Widget names:** the **Show name** checkbox sits beside Name for every type; `widgetShowsName()` is shared by the
  inspector, normalization and the previews (legacy sensor flag as fallback). Previews omit the name element; the
  widget list keeps it.
- **Pages:** `maxPages()` comes from `/api/version` (`max_pages` 8 or 12). From `full_nav_pages` (5, 10) on, the page
  inspector warns about a missing or duplicate icon (`navIconIssue()`). The size limit is `layout_max_bytes` (32 KB);
  see [bottom navigation](development/testing.md#bottom-navigation-full-and-compact-tabs).
- **Entity search** covers editable widget entity bindings. Wallbox bindings are read-only and come from discovery
  of the selected evcc instance. `inspectorEntityPickerTarget()` supplies
  domain and Room candidate filters; picking a result dispatches the same change event as manual entry.
- **Inspector presentation** retains the original fields and event handlers. Wrapping entity editors forward
  editing events to those fields; picker assignments mirror back into the visible editor. Position and size
  live in Placement, folded initially; Entities, Display and Controls precede it. Empty sections are hidden.
  The right column stays reserved and has no collapse button. Widget and special-page titles have
  an info icon with a short description and a documentation link. Extended help uses a label and an info icon opening the full explanation in a popup. Modal keyboard
  events do not reach editor shortcuts. Help uses `**Section heading**`, a blank line and its explanation; standalone
  bold headings render as headings, with separate body paragraphs. Do not join a heading and body with a colon.
  Hidden optional groups contribute no help; warnings/status remain visible beside their controls (`warn`/`no-shorten`).
  Settings retain sentence summaries. `autoGaugeName` in `js/layout-model.js` hides a gauge name when too narrow.
  The local spike shares help popups and keeps its own presentation adapter.
- **Energy timeline and room extra field** live in `js/canvas.js`, validated in `js/layout-model.js` like
  `layout_validate.c` (an extra entity must be one of the card's own and fit the mode's domain).
- **Previews follow the panel.** A tile preview matches the panel render in size, proportions, visible elements and
  their order at the same geometry and text step; it is not a pixel copy. The thresholds it copies from a `w_*.c`
  (a row appears above some height, a cover below some width) change together with the widget.
  Panel4 and Tab5 lay their widgets out from the text roles; the 10-inch boards keep fixed rows (`UI_TEXT_ADAPTIVE`).
  `/api/version` reports this as `text.adaptive`, `previewTextAdaptive()` reads it and `#canvas.text-fixed` carries
  the 10-inch rules in `css/preview-roles.css`. Line heights come from `previewLineHeight()` (the panel's metrics table).
- **Readability** (`js/readability.js`, `css/readability.css`) mirrors the panel4 role sizes in 720 × 600 previews.
  Weather and aligned solar offer auto or 1-7 days; Save does not resize tiles or enable rain. Gauge width
  hints measure state, range and unit at 38 px and are suggestions.
- **Sound** has its own settings page; the test plays at the selected volume and bypasses enable and night mute. Boards
  without audio hide it.

## Layout preview

Canvas tiles follow the panel: colour theme (`themeApplyCanvasPalette()` in `js/theme.js` sets `--pv-*` on `#canvas` from
`/api/themes/active`), the style's card and inner shapes (the "Layout preview" part of `styles.css`,
`css/style-<style>.css`) and Poppins Medium (the panel's subset as a WOFF2 data URI in the `betta-poppins` block of
`styles.css`, written by `tools/generate_widget_fonts.py --web`; the CSP allows `font-src data:`). The editor around the
canvas stays neutral. Sizes follow the tile through container units of `.widget-box` (`cqw`, `cqh`, `cqmin`), like the
panel's text scaling, and an element the panel drops at a size (media cover, vacuum robot, solar rows) is dropped at the
same height. Pixel-exact comparison is the simulator's job.

`tools/compare_editor_previews.py` puts each gallery widget's editor tile next to its LVGL image (one image per widget
plus contact sheets under `/private/tmp/`):

```bash
uv run --locked --group screenshots python tools/compare_editor_previews.py [--style matte] [widget ids]
... --cases [tile names] [--all-sizes]   # smallest and largest size of each tile in layout_cases.json, fresh renders
... --pages                              # whole house and energy page
```

Look for missing, extra, clipped or overlapping elements, German text in the English editor and sizes where the panel
hides something.

## Tab5 orientation and layout storage

Tab5 layout responses carry `orientation`, `canvas_w` and `canvas_h`. Portrait projects each widget's `portrait_rect`
into `rect` and adds the canonical `landscape_rect`; PUT restores landscape `rect` and stores edited portrait positions
separately. Stored JSON keeps the 32 KiB limit; Tab5 accepts up to 64 KiB on the wire for these aliases. A mismatched
orientation is rejected with HTTP 409. Version refreshes do not resize a loaded layout: reload after rotating.
