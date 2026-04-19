<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->
<img src="images/BETTAOS.jpg" alt="BETTA OS Logo" width="20%" />

# BETTA HA Panel

A runtime configurable Home Assistant dashboard for the ESP32-P4 Smart 86 Box development board.

## Project Description
BETTA HA Panel turns the Smart86 Box into a standalone 720x720 Home Assistant wall panel. It is built for a dedicated touchscreen experience with fast access to your most important entities, scenes, and automations.

The dashboard is configured directly on the device via the integrated BETTA Editor. Layout and settings are stored as JSON in LittleFS, so you can iterate quickly without rebuilding firmware for every UI change.

- Live connection to Home Assistant via WebSocket, with optional REST fallback for selected state/forecast refreshes.
- Local BETTA Editor at `http://<panel-ip>` for layout, widgets, Wi-Fi, Home Assistant, language, and time settings.
- Integrated first-run provisioning flow for Wi-Fi and Home Assistant, including the setup AP `BETTA-Setup`.
- Multi-page dashboard with draggable/resizable widgets: sensor, button, slider, graph, empty tile, light tile, heating tile, weather tile, and 3-day weather tile.
- Advanced light tiles with brightness, capability-aware dimming, RGB color control, color temperature control, presets, and compact Home Assistant light attributes.
- Weather tiles with compact forecast handling and WS/REST forecast refresh support.
- Multilingual web editor and device UI support with built-in English, German, Spanish, and French strings, plus custom translation JSON upload/download.
- Version-aware release flow: factory images are named by release version and old images are archived automatically.

## What's new in v0.7

- New versioned factory image: `release/betta86-ha-panel-v0.7.factory.bin`.
- The previous generic factory image was moved to `release/archive/`.
- The web editor now displays the running firmware version from `/api/version`.
- Light tiles now detect Home Assistant light capabilities and only show dimming/color controls when the entity supports them.
- RGB and color-temperature light control was added, including color swatches, Kelvin presets, and richer visual feedback.
- Home Assistant state handling was made more compact for lights, media players, and weather forecasts to reduce runtime memory pressure.
- Weather forecast refresh can use Home Assistant `weather.get_forecasts` and keeps the stored forecast compact.
- Widget inspector controls were expanded: button mode, button accent color, slider entity domain, slider direction, slider accent color, graph line color, graph time window, and graph render point count.
- Wi-Fi scanning is available in both first-run provisioning and settings, where supported by the current runtime mode.
- The release tooling now reads the project version, emits a versioned factory image, and archives older factory images.
- ESP-Hosted C6 adapter firmware can be embedded from `release/`, so the generated factory image is sufficient for distribution.
- For flashing and distribution, the factory image is enough; no separate C6 firmware file is required for the current release image.

## Getting Started
- Download the latest factory image: [betta86-ha-panel-v0.7.factory.bin](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/release/betta86-ha-panel-v0.7.factory.bin)
- Flash it with esptool or with a web flasher of your choice, for example: https://espressif.github.io/esptool-js/
- Use the outer USB-C port on the Smart86 Box for flashing.
- Choose baud rate `115200`.
  <img width="204" height="129" alt="image" src="https://github.com/user-attachments/assets/38bdee4a-e2e4-42ea-81ea-962cbd1dc082" />
- Connect to the correct COM port.
- Set the flash address to `0x0`.
  <img width="411" height="236" alt="image" src="https://github.com/user-attachments/assets/151c0026-15cc-450d-af28-d5629c5ec5e5" />
- Select the downloaded `.bin` file. The v0.7 factory image is about 4.6 MB and already includes the C6 network adapter firmware used for this release.
- Click `Program`.
  <img width="662" height="469" alt="image" src="https://github.com/user-attachments/assets/732007c8-9e7d-4411-8360-665553782b6c" />
- Reboot the device by pressing the reset button or briefly cutting power.
- On first boot, the panel creates a setup access point named `BETTA-Setup`.
- Connect to `BETTA-Setup` and open http://192.168.4.1.
- Enter your Wi-Fi country/region code, scan for your network, enter the password, and save.
- After reboot, the panel joins your Wi-Fi and receives an IP address from your local network.
- Open that IP address in your browser to continue the Home Assistant setup and start configuring the dashboard.
  <img width="613" height="416" alt="image" src="https://github.com/user-attachments/assets/a81fab35-d599-490a-9e1f-2675924b099d" />



## A Few Examples
Widgets can be configured and placed (drag and drop) on the canvas:
<img width="1426" height="766" alt="image" src="https://github.com/user-attachments/assets/9f8ba27f-943a-4e7b-8e18-ff7bf37bfea0" />

<p>
  <img src="images/light%20on%20example.jpg" alt="Light tiles ON example" width="49%" />
  <img src="images/heating%20on%20example.jpg" alt="Heating ON example" width="49%" />
</p>

Light tiles support on/off, brightness, RGB-capable lights, and color-temperature-capable lights. The controls are only exposed when Home Assistant reports the corresponding capability for the selected entity.

The BETTA Editor also supports entity autocomplete, domain validation, page titles, widget geometry, widget-specific options, import/export of the layout JSON, and custom UI translations.

<img width="1406" height="856" alt="image" src="https://github.com/user-attachments/assets/c5959f98-e151-4d9e-ac31-d636e9d65dcc" />

![alt text](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/images/weather%20forecast.jpg "3day weather forecast")

Configuration via webconfig in BETTA Editor:
<p>
  <img width="358" height="873" alt="image" src="https://github.com/user-attachments/assets/8c05cce8-983f-4715-a1fb-38ebbcbee563" />
</p>

Available Widgets:

<img width="319" height="130" alt="image" src="https://github.com/user-attachments/assets/1a248656-d01f-4585-a639-6d7dceafd08d" />
