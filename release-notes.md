<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->

# Release Notes

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
