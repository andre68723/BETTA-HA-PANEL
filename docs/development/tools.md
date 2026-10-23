<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Scripts in tools/

Prefixes: `check_*` verify, `generate_*` write committed output. Usage is in each script's header, a longer
description of every script in [tools/README.md](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/tools/README.md).

## Run levels

| Level | Rule | Runs |
| --- | --- | --- |
| **Every check** | Seconds, and any change can break it | JavaScript syntax, editor tests, HTML and translations, file sizes |
| **Every check, skipped when unchanged** | Slow, but fully decided by its input files | Simulator regressions, widget layout at the standard text step, host tests |
| **`--full`, always in CI** | Slow and a rare cause of a first failure | Widget layout at the Larger and Largest text steps |
| **Before a flash or release** | Needs the toolchain or a device | `check.sh --build`, packaging, OTA flash, device tests |
| **On demand** | Needs judgement, a browser or a panel | Generators, `compare_editor_previews.py`, `style_diff.py`, `calibrate_text_caps.py`, `stress_test.py` |

`check.sh` prints one line per step and keeps each step's output in `${TMPDIR:-/tmp}/betta-check/<step>.log`; only a
failing step prints its failure lines. A passing slow step stores a fingerprint of its command and input files in
`build/check-stamps/` and is skipped while nothing changed (`--force` ignores the stamps, `--verbose` streams output).
A cold run takes about five minutes, a repeat of an unchanged tree three seconds. Add a new check by the rule in the
table: a step with inputs goes through `step "<name>" "<inputs>" <command>` in `tools/check.sh`.

| Script | Job | Runs |
| --- | --- | --- |
| **Automatic** | | |
| `apply_component_patches.py` | Edits to fetched components ([patches](components.md#patches-to-fetched-components)) | every CMake configure |
| `generate_component_versions.py` | Component versions for `GET /api/version` from `dependencies.lock` | every CMake configure |
| **Checks** | | |
| `check.sh` | All checks without a device ([Checks](testing.md)) | by hand before a commit; CI with `--full` |
| `check_web.py`<br>`check_source_sizes.py`<br>`check_docs_size.py`<br>`check_simulator_fields.py`<br>`check_gallery_fresh.py`<br>`check_widget_layout.py`<br>`test_host.sh` | Single checks | every check (the slow ones skipped when unchanged) |
| `generate_widget_fonts.py` | Font subsets; `--check` runs in `check.sh` | every check, skipped when unchanged; regenerate when a font changes |
| `check_component_updates.py` | Outdated components ([components](components.md#keeping-components-current)) | on demand, before touching lock files |
| **Build and flash** | | |
| `build.sh` | The one way to call `idf.py` ([why](build.md#rules-for-builds-and-flashing)) | before a flash |
| `clean.sh` | Build directories; `--release` old folders in `release/out/` | by hand |
| `ota_flash.sh` | Flash one running panel over Wi-Fi; refuses an image older than the sources | before a flash |
| `package_release.sh` | Factory and OTA files of a finished build ([Releases](release.md)) | by hand |
| `auto_update_device_test.sh` | Automatic update on a test build | on demand |
| `lfs_repack.py` | Carry a panel's files over a partition change | on demand |
| `vendor_cam_sensor.py` | Re-vendor the camera driver | on demand |
| **Inspect** | | |
| `simulator.sh` | LVGL widgets on the desktop ([simulator](../simulator.md)) | on demand |
| `screenshot.sh`<br>`capture_device_pages.py` | Screen of a real panel | on demand |
| `stress_test.py` | Page switches, discovery and editor load on a panel | on demand |
| `compare_editor_previews.py`<br>`style_diff.py` | Review aids, gate nothing ([editor preview](../editor-source.md#layout-preview), [styles](../appearance-implementation.md)) | on demand |
| `calibrate_text_caps.py` | Text size caps ([conventions](conventions.md#text-and-fonts)) | on demand, after a layout change |
| **Docs and gallery** | | |
| `generate_simulator_gallery.py` | Gallery images and `gallery.md` | when a widget's look changes |
| `generate_widget_docs.py` | Widget guides | when widget metadata changes |
| `generate_docs_examples.py` | Dashboard, editor and top bar example images | when those change |
| `generate_check_diagram.py` | Diagram in [Checks](testing.md) | when `check.sh` changes |
| `import_device_captures.py` | Real panel captures into the docs | after `capture_device_pages.py` |
| `generate_gallery.py` | Gallery from a real panel | on demand |
| **Libraries** | | |
| `gallery_content.py`<br>`gallery_freshness.py`<br>`ui_styles.py` | Shared code, no commands | imported |
