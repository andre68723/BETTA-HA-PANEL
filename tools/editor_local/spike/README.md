<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -->
# Local editor prototype

Run from the repository root:

```bash
scripts/editor_local.sh --ui spike
```

The complete Inspector test demo is the default. To use a separate port:

```bash
scripts/editor_local.sh --ui spike --port 8767
```

This wide-board demo has 44 widgets covering all 34 canonical types on six topic pages,
plus Room overview and Energy. Optional sensor bindings, room members, forecast days,
printer actions and wallbox bindings are populated with synthetic entities. Variants cover
all graph modes, gauge shapes, solar orientations and room extras. Long IDs exercise wrapping.
Printer/loadpoint discovery also returns synthetic candidates. Its default save file is separate
from the compact demo (`--demo compact`); `--reset` resets only the chosen file. Small panel4 geometry is not supported.

Press Enter in the terminal to open the printed localhost URL in the default browser. The startup view needs no URL parameter.
The View selector switches between the classic editor and the prototype,
transferring unsaved layout edits and selection. Save writes only the local state file; Export downloads JSON.
Undo/Redo uses the same bounded layout history as the classic editor; it resets when switching or successfully reloading. Use `--variant` to choose another board geometry.

The adapter provides page tabs, a searchable library with 30 distinct original SVG icons, contextual properties,
labelled Save/Export, zoom, independent grid/snapping and a read-only browser preview. The sidebar starts with the
current page's widgets; **+ Widget** opens the library and adding a tile returns to the page's list. Canvas tools
are folded into **View** beneath the canvas. Position/size has a separate Inspector tab. Style opens automatically
when the full Inspector fits; a manual open/close choice is respected for the selected widget. Page options stay
beside the tabs, while widget properties open on selection. The page list is also available on the left for
renaming, reordering and other page actions. Inspector entity IDs wrap in compact editing fields, with the
existing Search picker beside them. Energy timeline battery/forecast bindings and counter requirements are
collapsed sections; its presentation belongs to Style. Library entries use synthetic
bindings. It shares the current editor model, Inspector events and preview code.
Selecting Energy or Room overview opens that page's settings automatically, including on initial load.
The Inspector keeps a fixed desktop column even without a selection, so switching pages or selecting widgets
does not resize the preview. Layout/Settings sit below the device metadata in the sidebar instead of adding a second header.
Energy page settings use compact label/control rows, wrapping sensor IDs and the existing sensor picker;
title/navigation, battery and gas/water start collapsed. Sensor edits apply through the original handlers.
The sidebar retains the editor version and device/type/build metadata; the layout-source caption is omitted.
Extended Inspector help uses a short label and an info icon opening the complete text in a popup.
Sensor fields use compact label/control rows. Changing widgets closes advanced sections and starts
the Inspector at the top. Page and widget lists scroll independently, leaving both accessible
even in the twelve-page test layout.
Room overview lists compact room disclosures, with one room form open at a time.

These files are development assets. Firmware CMake does not include them. Device commands and Home Assistant
service calls are unavailable in the local server. The normal editor is available independently with no arguments.

The global function wrappers and snapshot history are prototype implementations. Production adoption needs
reversible workspace mounting, central edit transactions, localization and regression checks in both views.
Panning, multiple selection, alignment guides, split JSON editing and preset libraries remain planned work.
The open design plan lives in the ignored `.local/plans/` directory. Temporary preview images and review results
belong under `/private/tmp/betta-<task>/`.

Local API persistence and device-command isolation checks:

```bash
python3 -m unittest discover -s tests/tools -p 'test_editor_local.py'
```
