<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Build and flash

Users install a [release image](../getting-started.md); this page is for building the firmware.

## Set up

```bash
git clone https://github.com/andre68723/BETTA-HA-PANEL.git && cd BETTA-HA-PANEL
mkdir -p ~/esp && cd ~/esp
git clone --branch v6.1 --depth 1 --recursive https://github.com/espressif/esp-idf.git esp-idf-v6.1
cd esp-idf-v6.1 && ./install.sh esp32p4          # ESP-IDF 6.1, Python 3.11+, macOS/Linux
source ~/esp/esp-idf-v6.1/export.sh              # in every new terminal
```

Builds from the development checkout can contain changes newer than any release.

**C6 adapter image.** The binary is not tracked; put the version your checkout needs into `release/` before building
(ESP-Hosted 2.12.13 for the current source):

```bash
curl -L --fail https://github.com/esphome/esp-hosted-firmware/releases/download/v2.12.13/network_adapter_esp32c6.bin -o release/network_adapter_esp32c6_2.12.13.bin
shasum -a 256 release/network_adapter_esp32c6_2.12.13.bin   # 09e4ed4ff648444ad045c1d934825af407648f58898586ce9f4275e53fdb23f6
```

The build embeds it and the P4 updates the C6 over SDIO when needed. Without the file the build warns and skips that
updater ([adapter policy](https://github.com/andre68723/BETTA-HA-PANEL/blob/main/release/README.md#esp-hosted-c6-firmware-source)).

## Build and flash a variant

```bash
scripts/build.sh panel4 build
scripts/build.sh panel4 -p <port> flash monitor       # exit the monitor with Ctrl+]
scripts/package_release.sh <built-version> panel4     # factory and OTA files plus SHA256SUMS in release/out/<version>/
```

Variants: `panel4`, `panel10`, `paneljc`, `paneljc-v2`, `tab5`. `package_release.sh` checks the version the build
reports (for example `v0.9.0`) and publishes nothing. `flash` writes bootloader, table and app separately, not a
merged factory file; flash the factory file with the [USB procedure](../getting-started.md#2-flash-the-panel).

## Install a downloaded image

The [browser route](../getting-started.md#2-flash-the-panel) needs no developer toolchain.
For command-line recovery, install [uv](https://docs.astral.sh/uv/getting-started/installation/) and replace
`<port>` with the serial port (macOS `/dev/cu.usbmodemXXXX`, Linux `/dev/ttyACM0`, Windows `COM3`):

```bash
uvx --from esptool esptool --chip esp32p4 --port <port> chip-id
uvx --from esptool esptool --chip esp32p4 --port <port> --baud 115200 write-flash 0x0 betta86-ha-panel-v0.9.0-panel4.factory.bin
```

Match the image to the device and silicon revision. The example is for `panel4`; use your own variant's file.
Write factory images at **`0x0`**, never an OTA-only image. A factory write overwrites the credential region.
Wait for verification before resetting. Opening the serial port can reset the device.

Compare the file's SHA-256 with the release's `SHA256SUMS`, for example:

```bash
shasum -a 256 betta86-ha-panel-v0.9.0-panel4.factory.bin
```

## Rules for builds and flashing

- **Always go through `scripts/build.sh <variant> [idf.py arguments]`.** It passes the full define set, selects target,
  manifest, config overlays and `build/<variant>/`. A bare `idf.py` resets the CMake cache to the root default (panel4,
  32 MB flash), and that image boot-loops on a 16 MB board.
- The configuration comes from `config/sdkconfig.defaults*` into `build/<variant>/sdkconfig` (git-ignored). Never edit it;
  delete the build directory to regenerate. Partition tables are in `config/`.
- Before flashing, check `head -1 build/<variant>/flash_args` for the right `--flash-size` and the expected app
  (`betta-ha-panel-jc.bin` for paneljc). Opening the USB serial port can reset the board.
- Before changing a partition layout, follow [Changing the partition table](#changing-the-partition-table).

## Flash over Wi-Fi

```bash
scripts/ota_flash.sh <panel-ip> build/paneljc/betta-ha-panel-jc.bin
```

Before it uploads, the script refuses an image that is older than a file in `main/`, `components/` or `config/`
(`--allow-stale` overrides): build again and flash once.

The script posts the plain app binary (not the factory image, not `*_flashed.bin`) to `POST /api/ota/upload`, waits for
the reboot and prints the version (about 30 s on paneljc). The panel accepts only an ESP app image with the running
firmware's project name, so an image for another variant is refused and nothing is written. Only the app slot is
written: a new bootloader or table and the first flash of a blank board need USB. The editor offers the same upload
(Settings → Firmware).

## Changing the partition table

An OTA writes only the app slot, so a new table needs one USB flash. `nvs` (0x9000, 24 KiB), `otadata` and `phy_init`
have not moved in any table of this fork, so Wi-Fi/HA/MQTT passwords and the admin login survive a flash that leaves
that range alone: `scripts/build.sh <variant> -p <port> flash` does, the combined `*.factory.bin` erases the NVS range.
A `storage` partition that moves or resizes is not found at its new place: the first boot formats an empty LittleFS and
layout, settings, themes and graph history are gone (it sits at 0xD60000 now). To carry the files over:

1. Identify the device (`esptool flash-id`: flash size and MAC). Back up `GET /api/layout`, `GET /api/settings` and the
   old partitions: `esptool read-flash 0x0 0x20000 low.bin` (boot, table, NVS, otadata) and
   `read-flash <old storage offset> <old size> old_fs.bin`.
2. `pip install littlefs-python`, then `tools/lfs_repack.py old_fs.bin <new storage size> new_fs.img`.
3. Write the matching parts with the app built for the new table in one call (offsets in `build/<variant>/flash_args`),
   for example on paneljc:
   `esptool --chip esp32p4 -p <port> write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x2000 build/paneljc/bootloader/bootloader.bin 0x8000 build/paneljc/partition_table/partition-table.bin 0xf000 build/paneljc/ota_data_initial.bin 0x20000 build/paneljc/betta-ha-panel-jc.bin 0xD60000 new_fs.img`,
   then `erase-region 0xD20000 0x40000` (an old app image where the new `coredump` partition starts could pass for a
   dump) and reset.
4. Compare `GET /api/layout` and `GET /api/settings` with the backup and check `ota_image_state` in `GET /api/version`.

Done this way on panel4 and paneljc (2026-10-03/04): both returned on their IP with identical layout and settings (only
the live signal strength differed) and Home Assistant connected.
