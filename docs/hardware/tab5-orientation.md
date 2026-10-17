<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Tab5 orientation

How the Tab5 detects rotation and how portrait and landscape layouts are stored.

## Tab5 orientation

`drivers/display_orientation.c` samples the BSP-shared BMI270 I2C bus every
200 ms outside the display lock. `orientation_policy.c` requires a dominant
upright gravity axis for one second and rejects flat, diagonal and shaking
samples. The managed BMI270 patch bounds shared-bus reads and writes to 100 ms.
The task retries unavailable sensor initialization after 30 s and retains the
current display orientation if sensor reads fail.

Rotation events go through the UI queue. The UI uses a bounded display-lock
attempt, defers while a popup or saver is active, changes LVGL rotation and
rebuilds the selected page. LVGL also rotates native touch coordinates.
`ui/display_geometry.c` provides runtime frame dimensions; `APP_SCREEN_*` and
`APP_CONTENT_BOX_*` remain the canonical landscape layout dimensions.
`layout/layout_orientation.c` generates missing portrait positions without
covering explicit positions and round-trips editor projections back to canonical
storage. Host orientation tests exercise dwell/hysteresis, all four geometries,
packing and preservation of landscape/portrait positions.
