<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Development

BETTA HA Panel is ESP-IDF firmware for ESP32-P4 touch panels with an embedded browser editor. Paths and commands
below are relative to the repository root; the working rules for contributors and assistants are in
[AGENTS.md](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/AGENTS.md).

```bash
source ~/esp/esp-idf-v6.1/export.sh      # ESP-IDF 6.1 with the ESP32-P4 toolchain
scripts/build.sh panel4 build              # one variant, always through this wrapper
uv run --locked --only-group checks scripts/check.sh --changed  # daily checks
```

[Build and flash](development/build.md) has the full setup. Updates and rollback are in
[Releases, OTA and rollback](development/release.md), what runs without a device in
[Checks and device tests](development/testing.md), vendored and patched dependencies in
[Components and patches](development/components.md); [Scripts and tools](development/tools.md) says which script does what. [Code conventions](development/conventions.md) and
[Working practices](development/learnings.md) collect the rules and what went wrong on real devices.

Subsystems have their own references: the [web editor source](editor-source.md), [appearance](appearance-implementation.md),
[notifications](notifications-implementation.md), [widget anatomy](widget-anatomy.md), the
[desktop simulator](simulator.md), [crash dumps](coredump.md) and
[CPU, memory and rendering](hardware/resources.md).

## Architecture

Home Assistant, MQTT and Wi-Fi feed a model and an event queue, `ui_runtime.c` turns that into LVGL widgets, and
only the UI task (or code holding the display lock) may call LVGL. The editor reads and writes settings and layout
through `/api/*`; saving a layout rebuilds the UI.

- **Boot and events:** `main/app_main.c` starts storage, display, network, editor and integrations;
  `main/app_events.*` passes state changes and commands to the UI task.
- **Configuration:** settings and the admin login live in NVS (`main/settings/`); layout, themes and translations in
  LittleFS (`main/layout/`, `main/ui/theme/`). `layout_validate.c` validates layouts, `widget_types.c` holds the
  shared widget limits and entity domains.
- **Network and Home Assistant:** `main/net/` (Wi-Fi through the ESP-Hosted C6, time, MQTT) and `main/ha/`
  (WebSocket and REST, entity state, service calls).
- **HTTP API:** `main/api/` serves the editor and `/api/*`; `http_guard.c` enforces authentication and request limits.
- **Display:** `main/ui/` (pages, widgets, styles) and `main/drivers/` (board display and touch drivers, the optional
  JC camera). Brightness, dim timer and wake behaviour are shared in `drivers/display_brightness.c`; a board file only
  writes the backlight.
- **Editor:** `components/webui/www/`, see [Web editor source](editor-source.md).
- **Build and checks:** `config/` (variant configs, partitions), `tools/`, `tests/host/`, `.github/workflows/`.

## Variants

`panel4` (Waveshare 4-inch LCD-4B) and `paneljc` (Guition JC8012P4A1, 10.1-inch) are exercised on hardware. A first
M5Stack Tab5 unit began device trials on 2026-10-05. `panel10` and `paneljc-v2` (Waveshare and Guition V2 10.1-inch)
are only compile-checked. A build or simulator render does not establish hardware behaviour; see
[Display devices](choose-display.md) for the status of each.
