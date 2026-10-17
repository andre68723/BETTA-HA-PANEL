<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Choose your display

Choose your device below. Its guide shows the flashing USB connector, firmware
selection and any revision checks before installation.

## Choose your display device

Devices are ordered by physical display size. Dashboard area excludes the top bar
and bottom navigation; the reserved bars use 120 pixels of screen height.

| Device | Display | Dashboard area | Firmware | Audio / camera in BETTA | Validation in this fork |
| --- | --- | --- | --- | --- | --- |
| [Waveshare LCD-4B](hardware/panel4.md) | 4-inch, 720 × 720 | 720 × 600 | `panel4` | Chimes; no camera sensing | Exercised on a v1.3 unit |
| [M5Stack Tab5](hardware/tab5.md) | 5-inch, 1280 × 720 / 720 × 1280, automatic rotation | 1280 × 600 / 720 × 1160 | `tab5` | Chimes; light/presence sensing, off by default | One ST7121/v1.3 device trial on 2026-10-05 |
| [Waveshare LCD-X 10.1-inch kit](hardware/panel10.md) | 10.1-inch, 1280 × 800 | 1280 × 680 | `panel10` | Audio unused; no camera sensing | Not yet tried on a real LCD-X kit |
| [Guition JC8012P4A1, original panel](hardware/paneljc.md) | 10.1-inch, 1280 × 800 | 1280 × 680 | `paneljc` | Audio disabled; light/presence sensing | Exercised on hardware |
| [Guition JC8012P4A1, V2 panel](hardware/paneljc.md) | 10.1-inch, 1280 × 800 | 1280 × 680 | `paneljc-v2` | Audio disabled; light/presence sensing | Not yet tried on a real unit |

"Exercised" means the firmware has run on that device. "Not yet tried" means nobody has run it on a real unit, so
expect rough edges.

Open the device name for its illustrated guide, then follow
[installation and first-run setup](getting-started.md).

## Display, camera and sound

Brightness, dimming, night mode and display protection share the same controls
on every display device. See [Display and brightness](display.md) for the settings and wake
behaviour.

The audio/camera column describes BETTA's available functions. Camera sensing
adjusts brightness or wakes the display; it does not record or stream video.
Chimes are doorbell and notification tones. A quiet Tab5 test chime was heard on 2026-10-05; broader acoustic checks remain pending.
See [Cameras and chimes](hardware/peripherals.md) for validation details and
[Notifications](notifications.md#sound) for setup.

## For developers

[CPU, memory and rendering](hardware/resources.md), [flash partitions and storage](hardware/storage.md), [cameras and chimes](hardware/peripherals.md) and [builds and recovery](development.md) are in the Development section.
