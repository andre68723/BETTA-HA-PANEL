<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->
<img src="images/BETTAOS.jpg" alt="BETTA OS Logo" width="10%" />

# BETTA HA Panel

A runtime-configurable Home Assistant wall panel for ESP32-P4 touchscreen devices. Build your dashboard directly on the device — no YAML edits, no firmware rebuilds.

<p float="left">
  <img src="images/heating%20on%20example.jpg" alt="Heating tile" width="32%" />
  <img src="images/light%20on%20example.jpg" alt="Light tile" width="32%" />
  <img src="images/weather%20forecast.jpg" alt="Weather forecast" width="32%" />
</p>

---

## Supported hardware

BETTA HA Panel ships as **two firmware variants**, one per supported device:

| Variant   | Device                                                 | Resolution | Factory image                                                                     |
|-----------|--------------------------------------------------------|------------|-----------------------------------------------------------------------------------|
| `panel4`  | Waveshare **ESP32-P4-WIFI6-Touch-LCD-4B** (4")         | 720 × 720  | [betta86-ha-panel-v0.8.0-panel4.factory.bin](release/betta86-ha-panel-v0.8.0-panel4.factory.bin)   |
| `panel10` | Waveshare **ESP32-P4 Module Nano + 10.1" DSI panel**   | 1280 × 800 | [betta86-ha-panel-v0.8.0-panel10.factory.bin](release/betta86-ha-panel-v0.8.0-panel10.factory.bin) |

Both variants share the same dashboard engine, web editor, and Home Assistant integration. Pick the image that matches your board.

---

## Main features

- **Live Home Assistant link** — WebSocket connection with REST fallback for forecasts and long-poll states.
- **On-device editor** — BETTA Editor in the browser at `http://<panel-ip>`; drag-and-drop widgets, multi-page layouts, room-grouped entity picker.
- **Widget library** — sensor, button, slider, graph, light, heating, weather, 3-day weather, media player, todo list, cover, energy dashboard, empty tile.
- **Advanced light control** — brightness, color temperature, RGB — exposed only when Home Assistant reports the capability.
- **Energy dashboard** — automatic grid / solar / battery / gas / water flow visualization driven by the Home Assistant energy model.
- **Graphs** — line, smoothed line, or bar-chart modes; event-rate sampling up to 4096 points with progressive decimation.
- **First-run provisioning** — `BETTA-Setup` Wi-Fi AP, guided Wi-Fi + Home Assistant setup, Quick Setup flow for a starter dashboard.
- **OTA updates** — upload an `.ota.bin` or point to an OTA URL from the web editor; no reflash required after v0.7.1.
- **Multilingual** — built-in English, German, Spanish, French; custom translation JSON upload/download.
- **Touch-friendly UX** — auto-dimming backlight after idle, pointer-capture drag/resize, stable GT911 touch startup.

---

## Getting started

1. **Download** the factory image for your board from the table above.
2. **Flash** it with any ESP32 flasher — for example the browser-based [esptool-js](https://espressif.github.io/esptool-js/):
   - Use the outer USB-C port.
   - Baud rate `115200`, flash offset `0x0`.
   - The factory image includes the ESP32-C6 network coprocessor firmware.
3. **Reboot** the device. It opens a Wi-Fi AP called `BETTA-Setup`.
4. Connect to `BETTA-Setup`, open `http://192.168.4.1`, pick your country, scan for your network and save.
5. After reboot the panel joins your LAN. Open its IP in a browser, link Home Assistant via long-lived access token, and build your first page with **Quick Setup**.

Future updates install via OTA from the editor — no cable needed.

<img width="1426" alt="BETTA Editor layout view" src="https://github.com/user-attachments/assets/9f8ba27f-943a-4e7b-8e18-ff7bf37bfea0" />

---

## What's new in v0.8.0

- **Dual panel support** — first release with dedicated `panel4` and `panel10` firmware images.
- **Media Player widget** — title / artist display, transport controls, volume, progress bar flanked by current and total time.
- **Todo List widget** — read/complete Home Assistant todo entities directly on the panel.
- **Cover widget** — up / stop / down for blinds and shutters.
- **Web editor polish** — whole-tile row click targets, red-circle delete buttons with confirm dialogs, canvas auto-sizes to the selected panel variant.
- **Missing-entity diagnostics** — the editor shows a banner when layout entities are absent in Home Assistant; details at `/api/ha/diagnostics`.
- **Multi-variant release tooling** — `tools/make_factory_bin.ps1 -Variant both` produces factory and OTA images for both devices in a single run.
- **Stability** — hardened WebSocket/TLS send path, robust watchdog for the initial entity sync, moved large buffers off the HA client task stack (fixes a rare crash when adjusting media player volume).

Full history: [release-notes.md](release-notes.md).

---

## Building from source

Prerequisites: **ESP-IDF v5.5.2**, Python 3.11+, the Smart86 / Waveshare BSP components (pulled automatically via the component manager).

```powershell
# Pick a variant preset
idf.py -B build-panel4  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.panel4"  build
idf.py -B build-panel10 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.panel10" build

# Package release images (factory + OTA) for one or both variants
pwsh tools/make_factory_bin.ps1 -Variant both
```

Build artifacts land in `release/` and `release/ota/`. Previous versions are moved to `release/archive/`.

---

## Editor preview

<p>
  <img width="49%" alt="Widget inspector" src="https://github.com/user-attachments/assets/97be77c3-0716-4641-994c-efe90c929953" />
  <img width="49%" alt="Page settings" src="https://github.com/user-attachments/assets/9caf6e2b-6ea9-4b76-b404-1b58da822712" />
</p>
<p>
  <img width="40%" alt="Settings view" src="https://github.com/user-attachments/assets/8c05cce8-983f-4715-a1fb-38ebbcbee563" />
  <img width="58%" alt="Energy dashboard" src="https://github.com/user-attachments/assets/96d51c1d-743a-4c7a-b2c6-29f2cff1e41f" />
</p>

---

## License

Source released under [LicenseRef-FNCL-1.1](LICENSE). See [release-notes.md](release-notes.md) for per-version changes.
