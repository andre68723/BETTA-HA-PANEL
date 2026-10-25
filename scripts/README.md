<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -->

# Scripts

Manual entry points; run as `scripts/<name>` from the repository root.
Internal helpers and generators live in [tools/](../tools/README.md).

| Script / options | Description |
| --- | --- |
| **Development and previews** | |
| [**check.sh**](check.sh)<br><br>`--changed`, `--quick`<br>`--full --force`, `--list` | **Run the project checks**, using the same runner as CI. Covers editor tests, source checks, host tests and simulator layouts; successful results are cached and each step keeps its log.<br><br>Use **changed** for affected inputs, **quick** to skip C/SDL, or **full** for all text sizes. **List** explains the selection. [Check levels and logs](../docs/development/tools.md#checks) |
| [**editor_local.sh**](editor_local.sh)<br><br>`--variant <variant>`<br>`--demo compact`, `--ui spike`<br>`--reset` | **Try the current editor without a panel or Home Assistant.** Wide displays start with all 34 widget types on eight pages, populated with synthetic data. Press **Enter** to open the default browser; saves stay local.<br><br>Choose the **compact demo** for fewer examples or **Spike** for the prototype. **Reset replaces local edits** with the current demo. [Local editor guide](../docs/editor-source.md#local-test-server) |
| [**simulator.sh**](simulator.sh)<br><br>`<variant>`<br>`--fixture`, `--page`, `--style`<br>`--screenshot`, `--regression` | **Inspect one widget or page rendered by real LVGL/SDL.** Select a fixture, page and appearance, then use an interactive preview or save a screenshot.<br><br>Also runs individual **simulator regressions**. This checks firmware rendering on the development machine; it does not establish physical display or touch behaviour. [Simulator guide](../docs/simulator.md) |
| [**test_pages.sh**](test_pages.sh)<br><br>`panel4 tab5 paneljc`<br>`--styles`, `--text-steps`<br>`--languages`, `--no-open` | **Review the widget catalog across all three tested display sizes.** Builds the simulator, renders the test pages, checks placement and content, and opens a browsable overview.<br><br>Limit the run by **appearance, text size or language**. Use `--no-open` to generate the overview without opening a browser. [Test-page guide](../docs/simulator.md#test-pages) |
| **Build and release** | |
| [**build.sh**](build.sh)<br><br>`<variant> build`<br>`<variant> reconfigure build` | **Build one firmware variant with its matching configuration.** Requires an active **ESP-IDF 6.1** environment. Successful builds archive their ELF so later crash reports can be decoded against the exact firmware.<br><br>Use **reconfigure** after configuration or dependency changes. Always use this wrapper for firmware builds. [Build guide](../docs/development/build.md) |
| [**clean.sh**](clean.sh)<br><br>`--list`, `<variant>...`<br>`--all`, `--release N` | **Free disk space used by builds and packaged releases.** Start with `--list` to inspect what exists, then select individual variants or all build directories.<br><br>`--release N` keeps the **N newest packaged releases**. Shared dependencies, lock files and the C6 adapter firmware are preserved. |
| [**ota_flash.sh**](ota_flash.sh)<br><br>`<panel-ip> <image.bin>`<br>`--allow-stale` | **Install firmware on a running panel over Wi-Fi.** Uploads the selected OTA image and checks that its project name matches the device.<br><br>The script refuses images older than the relevant sources. `--allow-stale` explicitly permits such a build. **This command changes the device firmware.** [OTA guide](../docs/development/build.md) |
| [**package_release.sh**](package_release.sh)<br><br>`<version> <variant>...`<br>macOS / Linux | **Package finished builds without rebuilding.** Creates factory images, OTA images and checksums under `release/out/<version>/`; refuses builds reporting another version.<br><br>Runs on **macOS and Linux**. It packages files locally and **publishes nothing**. [Release packaging](../docs/development/release.md#publish-a-release) |
| **Device diagnostics** | |
| [**auto_update_device_test.sh**](auto_update_device_test.sh)<br><br>`<panel-ip>`<br>`state`, `install`<br>`rollback`, `reset` | **Exercise automatic updates on a test device.** Inspect the reported update state or run the installation, rollback and reset scenarios described in the test procedure.<br><br>**Start with `state`** for inspection. The other modes deliberately change the device or its update state. [Automatic-update test procedure](../docs/development/release.md#test-the-automatic-update) |
| [**screenshot.sh**](screenshot.sh)<br><br>`<panel-ip> [out.png]` | **Capture what the panel currently shows**, including open popups and overlays, and save it as a PNG.<br><br>Uses the panel's **LVGL screenshot API**. The image is a software snapshot, not proof of the physical framebuffer or touch behaviour. [Screenshot guide](../docs/development/testing.md#screenshots) |

**Firmware variants:** `panel4`, `panel10`, `paneljc`, `paneljc-v2`, `tab5`.
The local editor's all-widgets demo requires a wide display; Panel4 uses the compact demo.

Daily check: `uv run --locked --only-group checks scripts/check.sh --changed`.
Builds and checks do not deploy firmware.
