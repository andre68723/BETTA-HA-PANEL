<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Code conventions

## Text and fonts

Use semantic roles through `ui_font_role()` or `ui_text_set()` (`ui/theme/ui_typography.h`), never pixel sizes.
`ui/text_scale.c` resolves the role, board profile and user step; a new board gets a profile entry.
Lay out from `lv_font_get_line_height()`. Keep one-line labels in dot mode with a one-line height and fit them with
`ui_text_fit()`. Panel4/Tab5 use adaptive rows; 10-inch boards use fixed rows. Protect small tiles with fixtures and
calibrated `text_scale_tile_cap()` thresholds, then check all three geometries at text steps 0/1/2.
All four font subsets are runtime `lv_tiny_ttf` fonts, managed by `main/ui/text_font.c`; bitmap fonts and
`lv_font_conv` are not used. The font reference in `main/ui/fonts/README.md` owns subset regeneration, metrics,
font fallbacks and emoji policy. Every new size/icon font needs a verified metrics row; do not embed brand logos.
Use `APP_SYMBOL_*` and `APP_FONT_SYMBOL_*`, not `LV_SYMBOL_*`; tile icons use `mdi_font_registry.h`.
New strings use `ui_i18n_get()` with English/German keys in `main/settings/i18n_store.c`. German uses real umlauts
and ß; English stays ASCII apart from °. The checker enforces this; dropped languages fall back to English.

## Look and feel

- Buttons and bars use `main/ui/theme/ui_style.h` (`ui_style_button()`/`_auto()`, inset tracks, gauge arcs), not flat
  fills. The style (flat, glow, glass, matte; `ui_style_get()`) is independent of the theme colours. Keep a depth-free
  fallback behind `ui_style_has_depth()` (glow and glass) and use `ui_style_is_glass()`/`ui_style_is_glow()` where they
  differ. Prefer `switch (ui_style_get())` over if/else chains, so `-Wswitch` catches a forgotten style. IDs, default and
  the Flat fallback are in `ui_style_registry.c`
  ([appearance reference](../appearance-implementation.md#style-ids-and-compatibility)).
- Cards inside tiles (weather days) are rounded surfaces slightly lighter than the tile, label muted, value in the
  primary colour.

## New widget types

`tools/check.sh --changed` checks every type from `main/layout/widget_types.c` in these places:

- `factory.create`, `factory.apply_state`, `factory.mark_unavailable`: dispatch in `main/ui/ui_widget_factory.c`.
- `editor.add`: a plain or picker Add action bound in `js/bind.js`, or one named custom handler it calls.
  `js/config.js` picker aliases may reuse types; plain/custom creators need no picker entry.
- `editor.preview`: `tilePreviewMarkup` in `js/tile-previews.js` or a dedicated preview selected in `js/canvas.js`.
- `editor.sizes`: `widgetSizeLimitsForCanvas` in `js/state.js`; `editor.names`: `WIDGET_NAME_TYPES` in `js/config.js`.
- `docs.content`: `WIDGETS` in `tools/widget_docs_content.py`; `docs.group`: exactly one `WIDGET_GROUPS` entry in
  `tools/gallery_content.py`, also imported by the guide generator.

One failure line names missing places per type; shared implementations and aliases are valid. Existing help/language
checks require English Inspector help to match the guide summary, with matching German keys. Regenerate guides with
`python3 tools/generate_widget_docs.py`. Existing case/enum checks require `tools/test_layout_cases.json` coverage;
Gallery tags reuse those cases (`gallery_only` stays out of test packs). The simulator-field check covers parsed keys.
Presence checks do not verify numeric limits, CMake inclusion, HA bindings or visual parity. The preview-parity skill
stays outside the gate; see [Checks and device tests](testing.md) for behavioral checks and text-cap calibration.

## HA attributes

For attributes larger than about 1 KB, review the domain serializer in `main/ha/ha_attr_serialize.c` and retain the
fields the widget reads. Otherwise the roughly 1.28 KB overflow fallback in `ha_attrs_build_generic_overflow_fallback()`
keeps only device class, unit, friendly name and entity picture; forecast, modes or room data can disappear.

## Web editor

Edit the plain sources in `components/webui/www/`. [Web editor source](../editor-source.md) covers asset lists,
local previews, device-backed proxies and browser checks. Use a read-only proxy for inspection; saves go to its panel.
New text needs matching English and German keys in `js/i18n.js` and `js/i18n-de.js`.

## Data compatibility

The supported upgrade path is from upstream v0.8.2 (`420df764`). Whatever it stored must keep working:
`/littlefs/layout.json` and `/littlefs/settings.json` (nested `wifi`/`ha`/`time`/`ui`), the NVS namespace `runtime_sec`
with `wifi_pwd`/`ha_token`, and the type name `roborock_tile` (renamed `vacuum_tile` on load). Older formats and the
fork's unpublished intermediate formats get no migration; check `git show 420df764:<path>` before adding or removing
one. The editor is served by the firmware, so it never handles an older firmware's API.

## Build rules

- Warnings are errors (`-Werror`, GCC 15); some only appear with `-Os`.
- LVGL calls only from the UI task or with the display lock held; esp_wifi calls go through the mutex in `wifi_mgr.c`.
- **Flash budget:** the app slot is 5 MB with about 7 % free (local v0.9.0 paneljc build, easter egg off). See
  [Flash budget](../hardware/storage.md#flash-budget-paneljc) before adding fonts, images or libraries.
  `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` would free about 340 KB but is **not enabled** (not regression-tested, harder
  debugging); decide on it before removing features.

## Licensing and headers

The licence is FNCL 1.1 (non-commercial, `LICENSE`). Every source file carries an SPDX line and the upstream
copyright, never removed. New files add
`Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md`;
substantially modified upstream files add
`Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md`. Code from elsewhere is
credited to its real source and licence in the file and in `LICENSE` section 12 (pattern:
`main/drivers/display_init_paneljc.c`), not to andre68723.

## Documentation to update with a change

| Change | Update |
| --- | --- |
| user-visible behavior | Current version in `docs/release-notes.md`, grouped by topic as a delta from the previous release |
| build requirements or tooling | Matching Development reference |
| user-visible feature | `README.md` feature list |
| notifications or MQTT | `docs/notifications.md` |
| a widget or feature that needs a specific Home Assistant integration or sensor | `docs/home-assistant.md` |
| a rendering style (flat/glow/glass/matte) change | `docs/appearance.md` |
| a widget's internal layout, minimum touch target or value/unit formatting | `docs/widget-anatomy.md` |
| supported boards, ports or controller revisions | `docs/choose-display.md` and the matching `docs/hardware/` board guide |
| CPU, memory, rendering or layout limits | `docs/hardware/resources.md` |
| flash layout, persistent data or app-size budget | `docs/hardware/storage.md` |
| brightness, wake behaviour or display protection | `docs/display.md`; driver evidence in `docs/hardware/peripherals.md` |
| fonts | `main/ui/fonts/README.md` |
| third-party code or assets | `LICENSE` section 12 and the file header |
