<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->
## Getting Started

Download the factory image for your hardware variant:

| Variant | Hardware | Factory image |
|---------|----------|---------------|
| panel4  | Smart86 Box (4" panel, ESP32-P4) | [betta86-ha-panel-v0.8.2-panel4.factory.bin](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/release/betta86-ha-panel-v0.8.2-panel4.factory.bin) |
| panel10 | Smart86 Box (10.1" panel, ESP32-P4) | [betta86-ha-panel-v0.8.2-panel10.factory.bin](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/release/betta86-ha-panel-v0.8.2-panel10.factory.bin) |
| panels3 | Guition ESP32-S3-4848S040 (4.8" 480×480, ESP32-S3) | [betta86-ha-panel-v0.8.2-panels3.factory.bin](https://github.com/cptkirki/BETTA-HA-PANEL/blob/main/release/betta86-ha-panel-v0.8.2-panels3.factory.bin) |

- Flash it with esptool or with a web flasher of your choice, for example: https://espressif.github.io/esptool-js/
- Use the outer USB-C port on the Smart86 Box for flashing.
- Choose baud rate `115200`.
  <img width="204" height="129" alt="image" src="https://github.com/user-attachments/assets/38bdee4a-e2e4-42ea-81ea-962cbd1dc082" />
- Connect to the correct COM port.
- Set the flash address to `0x0`.
  <img width="411" height="236" alt="image" src="https://github.com/user-attachments/assets/151c0026-15cc-450d-af28-d5629c5ec5e5" />
- Select the downloaded `.bin` file. The factory image already includes all required firmware components (bootloader, partition table, app, and for P4 variants the C6 network adapter firmware).
- Click `Program`.
  <img width="662" height="469" alt="image" src="https://github.com/user-attachments/assets/732007c8-9e7d-4411-8360-665553782b6c" />
- Reboot the device by pressing the reset button or briefly cutting power.
- On first boot, the panel creates a setup access point named `BETTA-Setup`.
- Connect to `BETTA-Setup` and open http://192.168.4.1.
- Enter your Wi-Fi country/region code, scan for your network, enter the password, and save.
- After reboot, the panel joins your Wi-Fi and receives an IP address from your local network.
- Open that IP address in your browser to continue the Home Assistant setup and start configuring the dashboard.
  <img width="613" height="416" alt="image" src="https://github.com/user-attachments/assets/a81fab35-d599-490a-9e1f-2675924b099d" />
- After v0.7.1 is installed, future app updates can be installed from the BETTA Editor with an OTA `.bin` upload or OTA URL.


## ESP-Hosted C6 Firmware Source

The original C6 adapter firmware used for this release was:

- https://github.com/esphome/esp-hosted-firmware/releases/tag/v2.11.7

Important:

- The C6 `network_adapter` firmware is already embedded into the generated factory image `betta86-ha-panel-<version>.factory.bin`.
- Running `tools/make_factory_bin.ps1` creates factory and OTA images and archives superseded images in `release/archive/` and `release/ota/archive/`.
- For flashing/distribution, the factory image is sufficient.
- For in-app OTA updates, use `release/ota/betta86-ha-panel-<version>.ota.bin`.
- Keeping `network_adapter_esp32c6*.bin` in the repo is optional and not required for release delivery.

Project policy (build/runtime):

- Use C6 firmware only from `release/`.
- Do not auto-build C6 firmware in this repository.
- Trigger C6 OTA only when running version mismatches host version (including `0.0.0`).
