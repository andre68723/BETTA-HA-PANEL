<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->
<img src="images/BETTAOS.jpg" alt="BETTA OS Logo" width="20%" />

# BETTA HA Panel 

A runtime configurable Home Assistant dashboard for the ESP32-P4 Smart 86 Box development board.

## project description
BETTA HA Panel turns the Smart86 Box into a standalone 720x720 Home Assistant wall panel. It is built for a dedicated touchscreen experience with fast access to your most important entities, scenes, and automations.

The dashboard is configured directly on the device via the integrated BETTA Editor. Layout and settings are stored as JSON in LittleFS, so you can iterate quickly without rebuilding firmware for every UI change.

- Live connection to Home Assistant via WebSocket (optionally with REST fallback)
- Local web editor at `http://<panel-ip>` for layout, widgets, and settings
- Integrated provisioning flow for Wi-Fi and Home Assistant (including the setup AP `BETTA-Setup`)
- Multi-page widget dashboard, e.g., sensor, button, slider, graph, light, heating, and weather tiles
- Subtle Lottie-animated weather icons for a more dynamic forecast view

## Getting started:
- download: [betta86-ha-panel.factory.bin](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/release/betta86-ha-panel.factory.bin)
- use esptool to flash or use a webflasher of your choice, for example: https://espressif.github.io/esptool-js/
- Use the outer USB-C port on the device for flashing.
- Choose Baudrate: 115200
  <img width="204" height="129" alt="image" src="https://github.com/user-attachments/assets/38bdee4a-e2e4-42ea-81ea-962cbd1dc082" />
- connect to the correct COM port
- change Flash Adress to: 0x0
  <img width="411" height="236" alt="image" src="https://github.com/user-attachments/assets/151c0026-15cc-450d-af28-d5629c5ec5e5" />
- choose the downloaded .bin file (make sure its fully downloaded 4.19 MB)
- Program
  <img width="662" height="469" alt="image" src="https://github.com/user-attachments/assets/732007c8-9e7d-4411-8360-665553782b6c" />
- reboot the device (cut power to restart or press reset button)
- the device will create an Access Point (AP) with ssid BETTA-Setup
- connect to the AP and open http://192.168.4.1
- provide your wifi region code, scan for wifi, enter your password and connect.
- once connected the device getting an IP in your local network
- open the IP to continue the setup process
  <img width="613" height="416" alt="image" src="https://github.com/user-attachments/assets/a81fab35-d599-490a-9e1f-2675924b099d" />



## A few examples:
Widgets can be configured and placed (drag and drop) on the canvas:
<img width="1426" height="766" alt="image" src="https://github.com/user-attachments/assets/9f8ba27f-943a-4e7b-8e18-ff7bf37bfea0" />

<p>
  <img src="images/light%20on%20example.jpg" alt="Light tiles ON example" width="49%" />
  <img src="images/heating%20on%20example.jpg" alt="Heating ON example" width="49%" />
</p>

Widgets can be configured and placed (drag and drop) on the canvas:

<img width="1406" height="856" alt="image" src="https://github.com/user-attachments/assets/c5959f98-e151-4d9e-ac31-d636e9d65dcc" />

![alt text](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/images/weather%20forecast.jpg "3day weather forecast")

Configuration via webconfig in BETTA Editor:
<p>
  <img width="358" height="873" alt="image" src="https://github.com/user-attachments/assets/8c05cce8-983f-4715-a1fb-38ebbcbee563" />
</p>

available Widgets:

<img width="319" height="130" alt="image" src="https://github.com/user-attachments/assets/1a248656-d01f-4585-a639-6d7dceafd08d" />
