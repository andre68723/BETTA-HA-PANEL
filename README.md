<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->
<img src="images/BETTAOS.jpg" alt="BETTA OS Logo" width="10%" /> 
# BETTA HA Panel 

<p float="left">
    <img src="images/heating%20on%20example.jpg" alt="Heating ON example" width="25%" />
    <img src="https://github.com/user-attachments/assets/97be77c3-0716-4641-994c-efe90c929953" width="25%" />
</p>
A runtime configurable Home Assistant dashboard for the ESP32-P4 Smart 86 Box development board.

## Project Description
BETTA HA Panel turns the Smart86 Box into a standalone 720x720 Home Assistant wall panel. It is built for a dedicated touchscreen experience with fast access to your most important entities, scenes, and automations.

The dashboard is configured directly on the device via the integrated BETTA Editor. Layout and settings are stored as JSON in LittleFS, so you can iterate quickly without rebuilding firmware for every UI change.

- Live connection to Home Assistant via WebSocket, with optional REST fallback for selected state/forecast refreshes.
- Local BETTA Editor at `http://<panel-ip>` for layout, widgets, Wi-Fi, Home Assistant, language, and time settings.
- Integrated first-run provisioning flow for Wi-Fi and Home Assistant, including the setup AP `BETTA-Setup` and a Quick Setup flow for the first dashboard.
- Multi-page dashboard with draggable/resizable widgets: sensor, button, slider, graph, empty tile, light tile, heating tile, weather tile, and 3-day weather tile.
- Home Assistant entity pickers in the editor, grouped by room where possible, so common tiles can be added without manually typing entity IDs.
- Advanced light tiles with brightness, capability-aware dimming, RGB color control, color temperature control, presets, and compact Home Assistant light attributes.
- Weather tiles with compact forecast handling and WS/REST forecast refresh support.
- Multilingual web editor and device UI support with built-in English, German, Spanish, and French strings, plus custom translation JSON upload/download.
- Browser-based OTA firmware updates from local `.ota.bin` files or OTA URLs.
- Display auto-dimming lowers the backlight after idle time and restores full brightness on touch.
- Version-aware release flow: factory and OTA images are named by release version and old images are archived automatically.

## What's new in v0.7.1

- OTA firmware updates are now available from the web editor, using either a local `.ota.bin` upload or an OTA URL.
- The release tooling now creates both `release/betta86-ha-panel-v0.7.1.factory.bin` and `release/ota/betta86-ha-panel-v0.7.1.ota.bin`.
- The web editor can discover Home Assistant entities while adding tiles, with room grouping, caching, progress display, and sensor search for large installations.
- A Quick Setup flow opens after Home Assistant provisioning to help new users build their first dashboard.
- The Settings view was reorganized so section navigation stays in the sidebar and the active settings page opens in the main workspace.
- OTA flashing now shows a dedicated progress screen on the panel and avoids the previous display flicker during flash writes.
- GT911 touch startup is more reliable, and the panel now dims to 30% after 3 minutes of no touch.

See [release-notes.md](release-notes.md) for the full changelog, upgrade notes, and OTA details.

## Getting Started
- Download the latest factory image: [betta86-ha-panel-v0.7.1.factory.bin](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/release/betta86-ha-panel-v0.7.1.factory.bin)
- Flash it with esptool or with a web flasher of your choice, for example: https://espressif.github.io/esptool-js/
- Use the outer USB-C port on the Smart86 Box for flashing.
- Choose baud rate `115200`.
  <img width="204" height="129" alt="image" src="https://github.com/user-attachments/assets/38bdee4a-e2e4-42ea-81ea-962cbd1dc082" />
- Connect to the correct COM port.
- Set the flash address to `0x0`.
  <img width="411" height="236" alt="image" src="https://github.com/user-attachments/assets/151c0026-15cc-450d-af28-d5629c5ec5e5" />
- Select the downloaded `.bin` file. The v0.7.1 factory image is about 4.8 MB and already includes the C6 network adapter firmware used for this release.
- Click `Program`.
  <img width="662" height="469" alt="image" src="https://github.com/user-attachments/assets/732007c8-9e7d-4411-8360-665553782b6c" />
- Reboot the device by pressing the reset button or briefly cutting power.
- On first boot, the panel creates a setup access point named `BETTA-Setup`.
- Connect to `BETTA-Setup` and open http://192.168.4.1.
- Enter your Wi-Fi country/region code, scan for your network, enter the password, and save.
- After reboot, the panel joins your Wi-Fi and receives an IP address from your local network.
- Open that IP address in your browser to continue the Home Assistant setup and start configuring the dashboard.
  <img width="613" height="416" alt="image" src="https://github.com/user-attachments/assets/a81fab35-d599-490a-9e1f-2675924b099d" />
- After v0.7.1 is installed, future app updates can be installed from the BETTA Editor with an OTA `.bin` upload or OTA URL. See [release-notes.md](release-notes.md#ota-and-packaging).



## A Few Examples
Widgets can be configured and placed (drag and drop) on the canvas:
<img width="1426" height="766" alt="image" src="https://github.com/user-attachments/assets/9f8ba27f-943a-4e7b-8e18-ff7bf37bfea0" />

<p>
    <img src="images/light%20on%20example.jpg" alt="Light tiles ON example" width="49%"/>
    <img src="images/heating%20on%20example.jpg" alt="Heating ON example" width="49%" />
</p>
<p>
    <img src="https://github.com/user-attachments/assets/97be77c3-0716-4641-994c-efe90c929953" width="49%" />
    <img src="https://github.com/user-attachments/assets/9caf6e2b-6ea9-4b76-b404-1b58da822712" width="49%" />
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

### Energy Dashboard
the energy Dashboard automatically gets all information from Homeassistant
<img width="1142" height="809" alt="image" src="https://github.com/user-attachments/assets/96d51c1d-743a-4c7a-b2c6-29f2cff1e41f" />
