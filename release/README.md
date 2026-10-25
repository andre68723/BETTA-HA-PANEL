<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk | Modified 2026 by andre68723 in the BETTA-HA-PANEL fork -->
# Firmware images

Use [Installation and first-run setup](../docs/getting-started.md) for both paths:
install a published GitHub release, or build the correct variant from source.
The guide covers display device/revision selection, factory versus OTA files, USB flashing
and Wi-Fi/Home Assistant setup.

The maintainer's [GitHub Releases](https://github.com/andre68723/BETTA-HA-PANEL/releases)
provide images for `panel4`, `paneljc`, `tab5`, `panel10` and `paneljc-v2`. `panel10` and `paneljc-v2` are only
compile-checked (the maintainer has no such hardware) and need testers.

| File | Use |
| --- | --- |
| `betta86-ha-panel-<version>-<variant>.factory.bin` | Combined USB install at `0x0`: bootloader, partition table and app |
| `betta86-ha-panel-<version>-<variant>.ota.bin` | App update for the matching display device and installed partition layout |
| `SHA256SUMS`, when packaged | Verify the downloaded/generated image hashes |

For local packaging, run `scripts/package_release.sh <built-version> <variant>...`
with ESP-IDF active. Images land in `release/out/<built-version>/`; see
[release packaging](../docs/development/release.md#publish-a-release).

## ESP-Hosted C6 Firmware Source

The current C6 adapter firmware used for this release is:

- https://github.com/esphome/esp-hosted-firmware/releases/tag/v3.0.10
  (`network_adapter_esp32c6.bin`, SHA-256 `c552957fad9f35cf31b0273099dd3b23dc5c388bb43884c6567756ab97fd8cce`)

Place it at `release/network_adapter_esp32c6_3.0.10.bin` before building (`.gitignore`d, so it
must be downloaded locally -- see "Project policy" below).

Previous versions used: v2.12.13, v2.12.12, v2.12.11, v2.11.7 (https://github.com/esphome/esp-hosted-firmware/releases)

Important:

- The C6 `network_adapter` firmware is already embedded into the generated factory image `betta86-ha-panel-<version>.factory.bin`.
- `scripts/package_release.sh <version> <variant>...` (macOS/Linux) creates the factory and OTA images from finished builds into `release/out/<version>/` (see [Releases](../docs/development/release.md#publish-a-release)). The former PowerShell packaging script was removed; Windows is not supported.
- For flashing/distribution, the factory image is sufficient.
- For in-app OTA updates, use the packaged `release/out/<version>/betta86-ha-panel-<version>-<variant>.ota.bin`.
- Keeping `network_adapter_esp32c6*.bin` in the repo is optional and not required for release delivery.

Project policy (build/runtime):

- Use C6 firmware only from `release/`.
- Do not auto-build C6 firmware in this repository.
- Trigger C6 OTA only when running version mismatches host version (including `0.0.0`).
