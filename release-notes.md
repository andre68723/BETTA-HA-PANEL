<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->

# Release Notes

## Unreleased

- Added source-build support for the Guition JC8012P4A1 (`paneljc`) ESP32-P4 + ESP32-C6 panel with JD9365 MIPI-DSI display, GSL3680 touch, 16 MB flash layout, and bundled ESP-Hosted C6 firmware update support.
- Added `paneljc` build/release packaging support and variant-aware web updater URLs so the editor targets `betta86-ha-panel-<version>-paneljc.ota.bin`.
- Updated supported-hardware and build-from-source documentation for the fourth hardware variant.

## v0.8.2

BETTA HA Panel v0.8.2 adds a third supported hardware variant — the Guition ESP32-S3-4848S040 — and ships factory and OTA images for all three panel variants.

### Highlights

- Added ESP32-S3 panel variant (`panels3`) for the Guition ESP32-S3-4848S040 (4.8" 480×480 ST7701S RGB display, GT911 touch, 16 MB flash, 8 MB octal PSRAM).
- MDI-based weather icons on S3 — Lottie/ThorVG animations are not used on S3 because the Meteocons vector assets exceed what the SW rasterizer can handle at embedded canvas sizes.
- Extended `make_factory_bin.ps1` — `panels3` is now a named variant; `-Variant both` packages all three variants in a single run without variants archiving each other's images.
- Three-variant release — factory and OTA binaries for `panel4`, `panel10`, and `panels3`.

### ESP32-S3 Variant (panels3)

- Target board: Guition ESP32-S3-4848S040 — ESP32-S3, 480×480 ST7701S RGB panel, GT911 capacitive touch.
- New display init (`display_init_panels3.c`) and touch init (`touch_init_panels3.c`) with a lightweight BSP shim.
- Separate partition table (`partitions.s3.csv`) with 4 MB OTA app slots suited to the 16 MB flash layout.
- S3-specific sdkconfig defaults (`sdkconfig.defaults.s3`, `sdkconfig.defaults.panels3`) disable ThorVG and Lottie; MDI icon font is used for all weather conditions.
- Build command: `idf.py -B build-panels3 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults.s3;sdkconfig.defaults.panels3" -DSDKCONFIG=sdkconfig.panels3 build`

### Packaging

- Panel 4 factory image:
  `release/betta86-ha-panel-v0.8.2-panel4.factory.bin`
- Panel 4 OTA image:
  `release/ota/betta86-ha-panel-v0.8.2-panel4.ota.bin`
- Panel 10 factory image:
  `release/betta86-ha-panel-v0.8.2-panel10.factory.bin`
- Panel 10 OTA image:
  `release/ota/betta86-ha-panel-v0.8.2-panel10.ota.bin`
- Panel S3 factory image:
  `release/betta86-ha-panel-v0.8.2-panels3.factory.bin`
- Panel S3 OTA image:
  `release/ota/betta86-ha-panel-v0.8.2-panels3.ota.bin`

## v0.8.1

BETTA HA Panel v0.8.1 adds the first Roborock map tile, refines the media player and shopping-list experience, and ships fresh dual-panel factory and OTA images for both supported display variants.

### Highlights

- Added a dedicated Roborock tile with Home Assistant `vacuum.*` and optional `image.*` map entity support.
- Added in-tile Roborock map rendering with room overlays and direct room selection on the map.
- Added Roborock segment cleaning controls with repeat count selection, start/pause, dock, and clean-selected actions.
- Added live Roborock robot-position overlay while cleaning, returning, or paused.
- Improved Roborock map loading for Home Assistant image proxy PNGs, including palette/RGBA PNG handling and PSRAM-backed decode buffers.
- Room controls on top of the map require the HACS integration `RoborockCustomMap` to provide the room metadata needed for the overlay.
- Added Roborock widget support to the web editor, widget factory, layout validation, and Home Assistant entity picker flow.
- Redesigned the Todo/Shopping List tile with larger touch targets and a card-like row layout.
- Optimized Todo scrolling with row virtualization, deferred updates while scrolling, and lower background refresh pressure.
- Refined Media Player progress handling around play/pause transitions and external state changes.
- Updated Media Player and Todo visuals to better align with the tile theme system.
- Added versioned `v0.8.1` factory and OTA binaries for `panel4` and `panel10`.

### Roborock

- The Roborock tile can use a `vacuum.*` entity for commands and status plus an `image.*` entity for the live map preview.
- Room overlays and tap-to-select room controls require the HACS integration `RoborockCustomMap`: `https://github.com/Lash-L/RoborockCustomMap`.
- Map layout adapts to tile shape: portrait tiles place the map above controls, while wide tiles place controls beside the map.
- Room controls now live directly on top of the map instead of relying on a separate room list.
- The map remains visible while periodic refreshes are in flight, avoiding the previous blank-map flicker.
- Live robot-position polling is active only while it is useful (`cleaning`, `returning`, or `paused`) and stops when the robot is docked or charging.
- The separate live-position marker is drawn below clickable room overlays so room selection remains easy.

### Todo And Media

- Todo rows use a smaller reusable object pool instead of rendering every item as a full LVGL object tree.
- Todo updates are deferred during active scrolling to avoid intermittent frame drops.
- Todo background fetch cadence was reduced to avoid unnecessary Home Assistant traffic.
- Media Player progress now keeps the last reliable timestamp through pause/play transitions instead of briefly jumping back to `0:00`.
- Media Player controls and slider styling were tuned to feel more consistent with the rest of the dashboard tiles.

### Home Assistant And Performance

- Home Assistant image attributes are compacted for map entities so Roborock image metadata can fit the device-side state cache.
- PNG cover/map fetching can decode into LVGL-compatible buffers without requiring `LV_USE_LODEPNG`.
- Large image decode and tile data allocations prefer PSRAM, reducing internal heap pressure.
- Hidden Roborock and Todo tiles avoid high-frequency work while their page is not visible.
- Entity discovery gating was adjusted so high-information pages do not starve web editor entity picker requests.

### Packaging

- Panel 4 factory image:
  `release/betta86-ha-panel-v0.8.1-panel4.factory.bin`
- Panel 4 OTA image:
  `release/ota/betta86-ha-panel-v0.8.1-panel4.ota.bin`
- Panel 10 factory image:
  `release/betta86-ha-panel-v0.8.1-panel10.factory.bin`
- Panel 10 OTA image:
  `release/ota/betta86-ha-panel-v0.8.1-panel10.ota.bin`

### Upgrade Notes

- Existing layouts remain compatible.
- Roborock tiles are additive and can be configured from the web editor using the Home Assistant entity picker.
- For live maps, configure the tile with the Roborock vacuum entity and the matching Home Assistant map image entity.
- For room overlays and direct room controls on the map, install and configure the HACS integration `RoborockCustomMap`: `https://github.com/Lash-L/RoborockCustomMap`.
- OTA images are app-only and must be installed through the panel updater, not flashed as full factory images.
- Factory images remain intended for first install, recovery, or partition-layout resets at flash offset `0x0`.

## v0.8.0

BETTA HA Panel v0.8.0 introduced the dual-panel release line, added the first Media Player and Todo List widgets, and refined the web editor and Home Assistant diagnostics around larger dashboards.

### Highlights

- Added dedicated `panel4` and `panel10` firmware variants.
- Added versioned factory and OTA binaries for both supported panel variants.
- Added the Media Player widget with title/artist display, transport controls, volume control, and progress timing.
- Added the Todo List widget for reading and completing Home Assistant todo entities directly on the panel.
- Added the on-board Theme Editor for customizing dashboard look and feel from the web UI.
- Improved web editor interaction with whole-tile row click targets and safer delete confirmation.
- Updated editor canvas sizing so layouts preview against the selected panel variant.
- Added missing-entity diagnostics so the editor can report layout entities that are not present in Home Assistant.
- Hardened the WebSocket/TLS send path and initial entity sync behavior.
- Moved larger Home Assistant/runtime buffers away from the HA client task stack to improve stability.

### Dual Panel Support

- `panel4` targets the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B 4" panel.
- `panel10` targets the Waveshare ESP32-P4 Module Nano with 10.1" DSI panel.
- Both variants share the same editor, layout format, Home Assistant integration, and widget engine.
- Release packaging can produce both variants through `tools/make_factory_bin.ps1 -Variant both`.

### Widgets

- Media Player tiles show media metadata, play/pause controls, volume, and progress position in one compact card.
- Todo List tiles bring Home Assistant shopping/todo lists onto the panel with direct completion support.
- Theme editing became available from the panel editor so visual changes no longer require firmware edits.

### Web Editor And Diagnostics

- Widget rows in the editor are easier to target on touch displays.
- Delete buttons use clearer destructive styling and confirmation behavior.
- The editor reports missing Home Assistant entities through a visible banner.
- `/api/ha/diagnostics` exposes more detail for troubleshooting entity mismatch issues.

### Packaging

- Panel 4 factory image:
  `release/betta86-ha-panel-v0.8.0-panel4.factory.bin`
- Panel 4 OTA image:
  `release/ota/betta86-ha-panel-v0.8.0-panel4.ota.bin`
- Panel 10 factory image:
  `release/betta86-ha-panel-v0.8.0-panel10.factory.bin`
- Panel 10 OTA image:
  `release/ota/betta86-ha-panel-v0.8.0-panel10.ota.bin`

### Upgrade Notes

- Existing layouts remain compatible.
- Choose the firmware image that matches the physical panel variant.
- Devices already on the OTA partition layout can update with the `.ota.bin` image for their panel variant.
- Factory images remain intended for first install, recovery, or full reflash at offset `0x0`.

## v0.7.3

BETTA HA Panel v0.7.3 finalizes the graph overhaul, improves weather forecast density, and hardens the web editor interaction model for touch and pointer-heavy layout work.

### Highlights

- Added selectable graph display modes: `line`, `line_smooth_points`, `line_smooth`, and `bars`.
- Added graph inspector options for display mode and bar interval (`5`, `10`, `15`, `30` minutes).
- Graph widgets can now be created directly from sensor entity picker flow in the editor.
- Reworked graph history sampling from fixed minute buckets to event-rate sampling with short-interval coalescing.
- Increased graph history capacity to `4096` samples and added progressive oldest-sample decimation to preserve long time ranges.
- Improved smooth graph rendering with moving-average preprocessing and Catmull-Rom interpolation.
- Added explicit validation and runtime parsing for `graph_display_mode` and `graph_bar_bucket_min` in layout JSON.
- Expanded compact Home Assistant weather forecast handling from 4 to 6 items.
- Weather forecast tiles can now show up to 5 future days plus the current summary row, depending on tile height.
- Improved forecast tile typography and row layout so taller cards use extra space instead of squeezing content.
- Fixed web editor drag/resize interruptions by switching widget manipulation to pointer capture.
- Prevented canvas re-renders during active widget interaction, avoiding lost drags while Home Assistant state updates arrive.
- Added weather forecast height hints in the editor preview.
- Finalized release packaging for `v0.7.3` factory and OTA binaries.

### Graphs

- `line` keeps the original direct sample rendering with indicator dots.
- `line_smooth_points` draws a smooth curve and overlays dots at real sample positions.
- `line_smooth` renders the smooth curve without sample dots.
- `bars` aggregates data into configurable minute buckets and renders a bar chart.
- Chatty sensors no longer exhaust the history buffer as quickly because rapid updates are merged within a short interval.
- When the history buffer fills, older data is compacted instead of being dropped immediately, so the graph better preserves long-term context.

### Weather

- Forecast tiles are no longer effectively limited to a 3-day view.
- The compact forecast payload now carries enough items for the current row plus up to 5 future days.
- Visible forecast rows scale with widget height, making larger weather cards materially more useful.
- The editor and UI wording now consistently use “Weather Forecast” instead of “Weather 3-day”.

### Web Editor

- Graph tiles now participate in entity picker workflow like other entity-bound widgets.
- Graph inspector fields dynamically show or hide point-count and bar-interval controls based on the selected graph mode.
- Dragging and resizing widgets is more stable on touch devices and in browsers where native drag behavior previously hijacked input.
- Canvas updates triggered by async state pushes are deferred until the interaction completes.

### Packaging

- Factory image:
  `release/betta86-ha-panel-v0.7.3.factory.bin`
- OTA image:
  `release/ota/betta86-ha-panel-v0.7.3.ota.bin`
- Previous `v0.7.3-beta1` factory and OTA artifacts are archived automatically by `tools/make_factory_bin.ps1`.

### Upgrade Notes

- Existing layouts remain compatible, but graph widgets can now opt into the new display modes and bar aggregation settings.
- Weather forecast tiles may render more rows on larger cards without requiring layout JSON changes.
- The OTA image remains app-only and must be installed through the panel updater, not flashed as a full factory image.

## v0.7.2

BETTA HA Panel v0.7.2 adds the first dedicated energy dashboard page with animated power-flow visualization, Home Assistant energy model support, and the required editor/runtime plumbing to configure and render it on-device.

### Highlights

- Added a dedicated Energy Dashboard page type.
- Added energy nodes for grid, solar, battery, gas, and water.
- Added animated flow visualization between energy nodes.
- Added dedicated MDI energy icon font assets for the dashboard.
- Added Home Assistant energy model parsing and runtime storage.
- Added `api_energy` endpoint support for the editor/runtime bridge.
- Extended layout schema and layout validation to understand energy dashboard configuration.
- Added editor UI to create and configure the energy page.
- Added versioned `v0.7.2` factory and OTA release artifacts.

### Energy Dashboard

- The dashboard introduces a separate UI page instead of trying to squeeze energy data into a normal tile.
- Grid, solar, battery, gas, and water are rendered as dedicated nodes with directional flow cues.
- Flow animations make energy movement legible at a glance instead of presenting only static totals.
- The runtime includes a dedicated Home Assistant energy model layer so page rendering stays decoupled from raw HA payloads.

### Editor And Runtime

- The web editor gained the necessary controls to add and configure the energy dashboard page.
- Runtime page loading and JSON parsing were extended so energy pages survive normal layout persistence.
- Layout validation was tightened to reject malformed energy dashboard definitions before they reach the device UI.

### Packaging

- Factory image:
  `release/betta86-ha-panel-v0.7.2.factory.bin`
- OTA image:
  `release/ota/betta86-ha-panel-v0.7.2.ota.bin`
- Previous `v0.7.1` factory and OTA artifacts were archived when `v0.7.2` was packaged.

### Upgrade Notes

- `v0.7.2` is the baseline that introduces the energy dashboard feature set later refined in subsequent builds.
- Existing non-energy layouts remain usable; the new energy page is additive.

## v0.7.1

BETTA HA Panel v0.7.1 focuses on easier onboarding, OTA updates, safer Home Assistant entity discovery, and display/panel reliability.

### Highlights

- Added OTA app update support from the BETTA Editor.
- OTA can flash a local `.ota.bin` uploaded through the browser.
- OTA can flash from a URL, including GitHub `blob` links that point to release binaries.
- Added an on-device OTA progress screen with the BETTA logo, status text, progress bar, and success/error states.
- Fixed OTA flash-time display flicker by enabling PSRAM XIP for code and read-only data.
- Added an OTA-capable partition table with factory, `ota_0`, and `ota_1` app slots.
- Updated the release packaging script to generate both factory and OTA images.
- Added a Quick Setup flow after Home Assistant provisioning.
- Added browser-side Home Assistant entity pickers for Light, Switch/Button, Weather, Heating/Climate, and Sensor tiles.
- Entity pickers are grouped by room where possible and reuse cached results while the editor stays open.
- Large entity lists are loaded in small pages to protect internal SRAM and TLS/WebSocket stability.
- Sensor discovery now uses search-first behavior and a capped result set for large Home Assistant installations.
- Added picker progress UI while Home Assistant discovery pages are loading.
- Reworked Settings mode so the settings menu stays in the left sidebar and selected settings open in the main content area.
- Added display auto-dimming: 100% while active, 30% after 3 minutes of no touch, and touch wakes back to 100%.
- Improved GT911 startup reliability by using an explicit reset/settle sequence before touch initialization.
- Added `espressif/esp_lcd_touch_gt911` as a direct dependency and tested the newer component version.

### OTA And Packaging

- New factory image path:
  `release/betta86-ha-panel-v0.7.1.factory.bin`
- New OTA image path:
  `release/ota/betta86-ha-panel-v0.7.1.ota.bin`
- `tools/make_factory_bin.ps1` now:
  - reads the project release version from root `CMakeLists.txt`,
  - creates a versioned factory image,
  - copies the app image as a versioned OTA image,
  - archives older factory images in `release/archive/`,
  - archives older OTA images in `release/ota/archive/`.

### Upgrade Notes

- Devices running firmware before the OTA partition layout still need a factory flash once.
- After flashing the v0.7.1 factory image, future app updates can use the browser OTA upload or OTA URL flow.
- The factory image is still intended for fresh installs and recovery flashing at offset `0x0`.
- The OTA image is only for the panel's OTA updater and must not be flashed as a full factory image.

### Web Editor

- The editor now offers entity selection while adding tiles instead of requiring users to type entity IDs manually.
- Light, switch, weather, and climate entities can be loaded directly from Home Assistant and grouped by room.
- Sensor selection is intentionally search-based because Home Assistant installations can expose hundreds or thousands of sensor entities.
- Discovery results are cached in the browser/runtime while the device remains running, with manual refresh available when Home Assistant changes.
- The Quick Setup flow helps new users create the first dashboard immediately after Home Assistant provisioning.
- The Settings tab now behaves more like a structured settings app: section names on the left, active settings on the main workspace.

### Runtime And Stability

- Home Assistant discovery uses small paged template requests instead of pulling large raw registries over the WebSocket.
- This avoids the TLS/WebSocket disconnect loop seen when larger discovery responses consumed too much internal memory.
- OTA update writes now keep the display stable with PSRAM XIP enabled.
- GT911 touch initialization now waits for the controller to settle after reset, avoiding the first-read startup error.
- Display brightness is reduced automatically during idle time to lower heat and power draw.

### Notes

- The first touch after dimming wakes the display and is still delivered to the UI.
- A future "wake-only first tap" mode can be added if accidental interactions become annoying.

## v0.7

- Added versioned factory image output.
- Added firmware version display in the web editor.
- Added advanced light tiles with capability-aware brightness, RGB, color temperature, presets, and compact Home Assistant attributes.
- Added weather forecast handling with compact refresh support.
- Added expanded widget inspector options for buttons, sliders, and graphs.
- Added Wi-Fi scanning in provisioning and settings where supported.
- Added multilingual editor/device UI support with built-in English, German, Spanish, and French strings.
- Added custom translation JSON upload/download.
- Embedded the ESP-Hosted C6 adapter firmware in the generated factory image.
