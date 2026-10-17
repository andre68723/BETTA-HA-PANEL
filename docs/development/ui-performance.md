<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- see docs/release-notes.md -->
# UI performance

## Page switching

Pages remain built and receive Home Assistant state changes while hidden.
Bambu, EVCC and Sonos Favorites cards therefore use their current state when
shown. Replaying each binding would render a whole card or rebuild its list
again before the first frame. EVCC still refreshes session history on show and
closes owned popups on hide.

The simulator runs the production widget factory and checks sensor/favorite
changes while hidden, then reopening without replaying the entity states.
Static gallery renders of all three card types remain pixel-identical in all
four styles.

## Compiler profile

Regular builds use ESP-IDF's Debug optimization (`-Og`), including LVGL.
Keep this setting for development and release builds. Assertions and watchdogs
remain enabled. The page-cache changes below do not depend on `-O2`.

The 2026-10-16 comparison also tested ESP-IDF's performance optimization
(`CONFIG_COMPILER_OPTIMIZATION_PERF=y`, `-O2`). Those measurements remain below
as trial results; the trial profile is not part of the build configuration.
See the [ESP-IDF 6.1 speed guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32p4/api-guides/performance/speed.html).

That build exposed stricter GCC string diagnostics. Layout error messages now
use explicit output bounds, and the HA client's checked copy helper uses
`strlcpy()`. Compiler warnings remain errors.

## Hardware acceleration

| Path | Original Guition JC8012P4A1 | M5Stack Tab5 | Waveshare 4-inch |
| --- | --- | --- | --- |
| Display buffer copies | DMA2D in panel driver | DMA2D in BSP | DMA2D in ST7703 driver |
| Display rotation | CPU, repeated-PPA-timeout workaround | Bounded PPA SRM | Bounded PPA SRM when rotated |
| JPEG pictures | Hardware JPEG decoder | Hardware JPEG decoder | Hardware JPEG decoder |
| LVGL drawing | Software renderer | Software renderer | Software renderer |
| LVGL PPA draw unit | Off | Off | Off |

Display rotation and the LVGL PPA draw unit are separate paths. The adapter
profile disables its own PPA rotation because the shared bitmap callback owns
rotation. `CONFIG_LVGL_PORT_ENABLE_PPA` belongs to the older compatibility port;
it does not describe the active adapter callback.

The current renderer uses partial RGB565 buffers. LVGL calls its PPA draw unit
experimental and reports no performance increase in partial mode because of
DMA2D bandwidth; enabling it is not a substitute for removing redundant UI
work. See [PPA drawing](https://lvgl.io/docs/open/integration/chip_vendors/espressif/hardware_accelerator_ppa)
and [DMA2D copies](https://lvgl.io/docs/open/integration/chip_vendors/espressif/hardware_accelerator_dma2d).

All three tested v1.3 devices run at 360 MHz with 200 MHz PSRAM, a 128 KiB L2 cache
and code/rodata fetched from PSRAM. The trial changes none of these settings.
LVGL's fast-code-in-IRAM option is off; using it would consume memory also
needed by networking and TLS and needs a separate measured budget.

## Measurement method

The 2026-10-16 trial compares three local fork builds: the running firmware,
the cached-page change with `-Og`, and that change with `-O2`. It covers every
configured page on an original Guition JC8012P4A1 (ESP32-P4 v1.3, 1280 × 800),
an M5Stack Tab5 (v1.3, 1280 × 720 landscape), and a Waveshare
ESP32-P4-WIFI6-Touch-LCD-4B (v1.3, 720 × 720), five samples per page and build.
Layouts are read back and compared across each OTA, and the running source
stamp and OTA validation are checked before measuring.

The value is the first completed LVGL refresh after `ui_pages_show_index()`.
It includes drawing/flushing but excludes the HTTP command's queueing time.
It is not a measurement from physical finger contact to visible pixels, or a
claim that animations and live data have finished. Subsequent frames in the
existing 700 ms diagnostic window are not used as the page-switch result.

## Results

Median of five measurements per cell. Smaller is faster. The last column
compares the original build with cached cards plus `-O2`. Small single-digit
changes can be normal timing variation. The raw logs are grouped by inferred
switch start (log timestamp minus elapsed time), so an older ring-buffer line
cannot be assigned to a later switch.

### 10-inch Guition

| Page | Before, -Og | Cached, -Og | Cached, -O2 | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Study | 373 ms | 353 ms | 298 ms | 20.1% |
| Energy | 236 ms | 239 ms | 198 ms | 16.1% |
| House | 213 ms | 203 ms | 180 ms | 15.5% |
| Energy timeline | 174 ms | 181 ms | 149 ms | 14.4% |
| History | 191 ms | 190 ms | 161 ms | 15.7% |
| Bambu and EVCC | 547 ms | 229 ms | 195 ms | 64.4% |

### 5-inch Tab5

| Page | Before, -Og | Cached, -Og | Cached, -O2 | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Study | 242 ms | 239 ms | 218 ms | 9.9% |
| Energy | 212 ms | 203 ms | 181 ms | 14.6% |
| House | 166 ms | 166 ms | 162 ms | 2.4% |
| Consumption | 181 ms | 184 ms | 168 ms | 7.2% |
| History | 150 ms | 153 ms | 141 ms | 6.0% |

### 4-inch Waveshare

| Page | Before, -Og | Cached, -Og | Cached, -O2 | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Old School | 116 ms | 119 ms | 104 ms | 10.3% |
| Kitchen | 127 ms | 127 ms | 111 ms | 12.6% |
| Energy | 119 ms | 118 ms | 98 ms | 17.6% |
| History | 79 ms | 76 ms | 72 ms | 8.9% |
| Rings | 73 ms | 74 ms | 67 ms | 8.2% |
| Media | 81 ms | 80 ms | 73 ms | 9.9% |

The cache change alone makes little difference on this layout. The Media page
had one 247 ms sample under `-O2` (the other four were 71–73 ms); this remains
in the dataset. The median improvement does not imply that every switch is faster.

All 255 samples completed. All three optimized images passed OTA validation, kept
identical layouts and finished the trial without a reset, retained crash or
watchdog stall. This is a short page-switch trial, not long-term or physical
touch validation. `-Og` remains the selected build mode; the `-O2` columns
describe the comparison trial only.

| Build | Original Guition | Tab5 | Waveshare 4-inch |
| --- | --- | --- | --- |
| Before | `4d419467-dirty`, 2026-10-16 08:07:04 | `4d419467-dirty`, 2026-10-15 11:41:21 | `4d419467`, 2026-10-15 11:39:57 |
| Cached, `-Og` | `fbc6f025-dirty`, 2026-10-16 08:52:31 | `fbc6f025-dirty`, 2026-10-16 08:47:11 | `fbc6f025-dirty`, 2026-10-16 09:34:33 |
| Cached, `-O2` | `fbc6f025-dirty`, 2026-10-16 08:57:39 | `fbc6f025-dirty`, 2026-10-16 09:00:14 | `fbc6f025-dirty`, 2026-10-16 09:38:06 |

`-O2` adds 33,632 bytes to the Guition app image and 29,408 bytes to the Tab5
image compared with the cached `-Og` builds. The trial images are 5,140,688 and
5,224,800 bytes, leaving about 24.6% and 23.3% of their 6.5 MiB OTA slots free.

On the Waveshare 4-inch, `-O2` adds 33,600 bytes. Its trial app image is
4,960,768 bytes, leaving about 27.2% of the 6.5 MiB OTA slot free.
