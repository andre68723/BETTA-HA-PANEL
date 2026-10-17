<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Device notes

Pin assignments, checks against manufacturer schematics, BSP versions and open points for each display device. Users want the [device guides](../choose-display.md); this page is for developers.

[Waveshare 4-inch](#panel4) · [M5Stack Tab5](#tab5) · [Waveshare 10.1-inch](#panel10) · [Guition JC8012P4A1](#paneljc)

## Waveshare 4-inch LCD-4B (`panel4`) { #panel4 }

Device guide: [Waveshare 4-inch LCD-4B](panel4.md).

Checked (2026-09-26) against the wiki, the product page, the
[main board schematic (PDF)](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B/ESP32-P4-WIFI6-Touch-LCD-4B.pdf)
and the GitHub repository at commit `5a7a9a2` (its `schematic/` copy is the same file).
The display, touch and audio pins come from the Waveshare BSP (`waveshare/esp32_p4_wifi6_touch_lcd_4b` 3.0.1);
the SDIO wiring is our own (`config/sdkconfig.defaults`).

| Item | Manufacturer | This firmware |
| --- | --- | --- |
| Chip, flash, PSRAM | ESP32-P4NRW32, 32 MB NOR flash, 32 MB PSRAM in package | same; flash DIO 80 MHz, PSRAM hex 200 MHz |
| Silicon revision | two profiles: `rev1_3` (v1.x, PSRAM 200 MHz) and `rev3_x` (v3.x, PSRAM 250 MHz, the repository's default); flash QIO | v1.x only (`REV_MIN_100`, `SELECTS_REV_LESS_V3`), like the tested unit (v1.3) |
| BSP | `waveshare/esp32_p4_wifi6_touch_lcd_4b` 3.0.1 | 3.0.1 with adapter 0.7.3 (local manifest override) |
| Display | 720 x 720 IPS, 2-lane MIPI DSI, reset GPIO 27 | same (BSP: ST7703, 2 lanes at 480 Mbit/s) |
| Backlight | PWM GPIO 26, boost enable `BL_EN` GPIO 33 | PWM on GPIO 26. GPIO 33 is not driven: `BL_EN` has a 100 kOhm pull-up to 5 V (R57), so the boost converter is always on |
| Touch GT911: I2C SDA/SCL, reset, interrupt | 7/8, 23, interrupt only on test point TP2 | same, polled without interrupt; address 0x5D or 0x14 (see below) |
| Audio ES8311/ES7210, amplifier | I2S GPIO 9-13, `PA_CTRL` GPIO 53 | ES8311 chime path exercised via API/log; acoustic and touch checks pending; ES7210 microphone unused |
| ESP32-C6 SDIO: CLK, CMD, D0-D3 | schematic: GPIO 18, 19, 14-17 (to C6 IO19, IO18, IO20-IO23) | same, slot 1, 4 bit, 40 MHz |
| ESP32-C6 reset | GPIO 54 to `C6_CHIP_PU` (0 Ohm R34) | same |

The wiki's "GPIO Allocation" table does not match the schematic for the C6: it lists GPIO 6, 14, 16, 18-20 and 22
and leaves out 15 and 17. The schematic connects GPIO 6 to C6 IO2 (unused by ESP-Hosted), the SDIO lines to GPIO 14-19
as in the table above, and C6 IO9 (boot) only to the P1 header, not to GPIO 22. The repository's `docs/board.md`
repeats the same wrong list, but its own ESP-Brookesia firmware selects ESP-Hosted's `P4_DEV_BOARD_FUNC_BOARD` preset,
whose defaults are exactly our pins. Go by the schematic; the configuration above runs on the device.

Open points from this check:

- **ESP32-P4 v3.x silicon.** Waveshare builds its examples for v3.x by default, so newer 4" units probably carry a
  v3.x chip. Our `panel4` image allows only v1.x (`REV_MAX_FULL=199`) and is expected not to boot on such a unit
  (untested, no v3 unit available). `esptool chip-id` shows the revision. A v3 build needs `REV_MIN_300` without
  `SELECTS_REV_LESS_V3`, the same as for the JC8012P4A1 [V3](paneljc.md#v3-esp32-p4-v3x-silicon).
- **GT911 I2C address.** The GT911 picks 0x5D or 0x14 from the INT level when it leaves reset. On this board INT
  (`TP_INT`) only goes to test point TP2 and has no pull resistor, so the level is not defined. Like Waveshare's
  BSP 3.0.1 ("GT911 address-probe fix"), `touch_init_panel4.c` now probes 0x5D, then 0x14, after pulsing the reset
  (GPIO 23), and logs `GT911 answers at 0x..`. The tested unit answers at 0x5D; the fallback was exercised on it
  once with the order reversed (0x14 failed, 0x5D found, touch initialized).
- **BSP integration.** BSP 3.0.1 runs through the shared adapter 0.7.3 layer. The local BSP override changes
  only its adapter constraint. The profile selects the proper 720 × 720 option and retains two DPI framebuffers;
  the ST7703 constructor installs the IDF 6 DMA2D copy hook. BETTA explicitly retains
  GPIO23 touch reset despite the new BSP's NC macro. See [components and patches](../development/components.md).
  On the local v1.3 unit, source `650fd258` passed OTA/API checks and owner-confirmed physical image orientation
  and touch page navigation on 2026-10-13.

## M5Stack Tab5 (`tab5`) { #tab5 }

Device guide: [M5Stack Tab5](tab5.md).

The display uses LVGL software rotation for all four orientations. USB-C supplies power;
an optional NP-F550 battery can be installed. Charging starts only after power-on.
The BMI270 accelerometer controls orientation; ES8388 supplies optional chimes.
Battery features, RTC, USB-A host and microSD are not used by this firmware. The BSP supports the early ILI9881C and ST7123 controllers;
On 2026-10-05 one unit (ESP32-P4 v1.3, ST7121, 16 MB flash) booted this firmware: the display initializes at 720x1280, and display/touch responded during the initial trial. Portrait display was confirmed after the rotation crash fix; repeated touch checks in all four orientations remain pending. The unit shipped with ESP32-C6 firmware 1.4.1, which the app replaced with the bundled 2.12.13 on the first start. The C6 reset line (GPIO15) is active high; with the active-low setting the C6 never answered on SDIO.

`main/drivers/display_init_tab5.c` and `touch_init_tab5.c` use the Espressif BSP [`espressif/m5stack_tab5`](https://components.espressif.com/components/espressif/m5stack_tab5) 1.3.2 (display, touch, I/O expander, backlight; `bsp_feature_enable(BSP_FEATURE_WIFI)` powers the C6). `config/sdkconfig.defaults.tab5`, `config/partitions.tab5.csv` (16 MB, two 6.5 MiB app slots) and `dependencies.lock.tab5` belong to the variant. See [flash and storage](storage.md) for the current partition layout. The previously recorded 8% slot headroom was for the older app-slot profile, not a measurement of the current layout.

Layout uses the 12-page bottom bar of the 10-inch boards, with widget role sizes like panel4 because the screen is denser (about 294 ppi, versus 254 ppi on panel4 and 149 ppi on paneljc).

Backlight uses GPIO22 and the unmodified BSP 1.3.2 configuration: LEDC timer 0, 5 kHz and 10-bit linear duty.
Known limitation on the owner's ST7121 unit (2026-10-13): 1–50 % had little visible difference; 75 % was brighter
and 0 % switched off. Alternative PWM profiles did not resolve this observation, so the BSP defaults are retained.
Successful writes log the duty, range and frequency read back from LEDC. These values confirm the PWM configuration,
not measured light output.
`GET /api/version?backlight=1` optionally samples GPIO22 in a bounded 8–50 ms window (8 ms with the BSP defaults)
and reports pad/matrix configuration,
LEDC frequency/duty, digital High counts and whether the requested PWM stayed stable during the sample.
Sampling restores the pin's previous input-enable state and leaves the PWM output path and settings unchanged.

The pin review on 2026-10-02 used the manufacturer's documentation: touch I2C on G31/G32, GT911 at 0x14 or ST7123/ST7121 at 0x55, touch interrupt G23, and display reset through the PI4IOE5V6408 expander. Display and touch responded on one ST7121/v1.3 unit on 2026-10-05; other controller revisions remain untested.

## Waveshare 10.1-inch panel (`panel10`) { #panel10 }

Device guide: [Waveshare 10.1-inch panel](panel10.md).

The shared ESP32-P4/C6 configuration uses 360 MHz CPU and 200 MHz Hex PSRAM. It does not establish hardware operation or a memory baseline for panel10. Match the exact controller/display assembly and chip revision before flashing. No audio driver is enabled for this variant.

`display_init_panel10.c` and `touch_init_panel10.c` currently use
`bsp/esp32_p4_nano.h`. The LCD-X controller assembly has different connector
geometry from that Nano board. Correcting this guide's illustration and links
does not validate its pin assignments, display initialization or touch mapping.

## Guition JC8012P4A1 (`paneljc`) { #paneljc }

Device guide: [Guition JC8012P4A1](paneljc.md).

The original `paneljc` profile uses CPU rotation through `lv_draw_sw_rotate`
in the shared adapter bitmap callback (`CONFIG_APP_DISPLAY_PPA` disabled). This bypasses
the PPA completion stalls; long-run stability remains under observation.
`paneljc-v2` retains PPA rotation. The display boot log identifies the backend.

For a panel that still responds over HTTP, collect `/api/version` and `/api/log` before using
RESET or opening a serial port. The recovery steps are in [development.md](../development.md).

Checked against that package (2026-09-26):

| Item | Manufacturer | This firmware |
| --- | --- | --- |
| Backlight PWM / LCD reset | GPIO 23 / 27 | same |
| Touch GSL3680: I2C SDA/SCL, reset, interrupt | 7/8, 22, 21 | same |
| ESP32-C6 SDIO: CLK, CMD, D0-D3, slot, width | 18, 19, 14-17, slot 1, 4 bit | same |
| ESP32-C6 reset (`C6_CHIP_PU`) | GPIO 54 (schematic, pin 98) | same. The package's `xiaozhi` demo sets 12, which is the audio I2S clock on this board: do not copy it |
| SDIO clock | 40 MHz | same (20 MHz until 2026-09-26, see [development.md](../development.md)) |
| Flash / PSRAM | 16 MB QIO 80 MHz / hex 200 MHz | 16 MB **DIO** 80 MHz / hex 200 MHz (DIO is the conservative choice; code runs from PSRAM) |
| V1 ("Old Panel") timing | 1000 Mbit/s, 60 MHz, hsync 20/20/40, vsync 8/4/20 | same |
| V1 init table | vendor JD9365 table | same registers; ends with the Espressif default sleep-out/display-on sequence (120 ms each) instead of the vendor's extra trailing sleep-out and tearing-effect command (no effect in DSI video mode) |
| V2 ("New Panel") timing | 70 MHz, hsync 20/20/40, vsync 10/4/30; 1000 Mbit/s (IDF 5.5.4 demo) or 1500 (IDF 6.0.2 demo) | same, at 1500 Mbit/s (V2 has not run on hardware here) |
| V2 init table | vendor JD9365 table | same registers and delays (the vendor spells the delays as empty `0x00` entries) |
| C6 slave firmware | JC-C6-slave v2.3.2 | ESP-Hosted network adapter 2.12.13 |

### JC8012P4A1 Panel Revisions

The Guition JC8012P4A1(C) ships with two known display-panel revisions under the
same model number/SKU. A "V3" unit uses the newer panel with different P4 silicon,
as described below.

**Guition's own rule** (the burning instructions in its package, see
[Manufacturer references](paneljc.md#manufacturer-references)): a **"V2" after the material number on the
label on the back of the case** means the new panel ("New Panel" in Guition's demos, `paneljc-v2`
here); without it, the original panel ("Old Panel", `paneljc`). Use that label first. Guition's image
for ESP32-P4 v3.x silicon is a "new panel" build, so v3 boards carry the V2 panel.

Before this rule was known, community reports tied the revision to a 4-digit date code (format
`YYWW`, year + ISO week) on the same sticker; it is a secondary hint only:

| Date code | Revision | Firmware support |
|-----------|----------|-------------------|
| up to ~2627 | "V1" / original panel | Default `paneljc` build (this is what `main/drivers/display_init_paneljc.c`'s unconditional init table/timing target) |
| ~2628 and up | "V2" panel | Build with `CONFIG_APP_PANELJC_PANEL_V2=y` (`paneljc-v2` CMake preset / `config/sdkconfig.defaults.paneljc.v2`) |
| any (chip ESP32-P4 v3.x) | "V3" | **Not supported by this build.** V3 is not a third panel but the same panel with ESP32-P4 v3.x production silicon; see [V3 silicon](#paneljc-v3) |

V1 and V2 need different JD9365 gamma/GOA init register values and
different DSI/DPI timing (pixel clock, lane bitrate, vsync porches). The
touch controller (GSL3680) configuration is identical between V1 and V2, so
this only affects the display driver, not touch.

**Important nuance:** the date-code boundary is evidence, not a guarantee (the "V2" label above is
the manufacturer's criterion).
At least one report exists of a 2627-dated unit that actually needed the
older V1 firmware despite being right at the documented cutoff. If a panel
shows a black or garbled screen with the option matching its date code, try
building with the other option before assuming a hardware fault.

The V2 variant is `paneljc-v2`; to build it yourself see [Build from source](../development/build.md#set-up).

Credit: the V2 init table and DSI/DPI timing were sourced from
[`jtenniswood/espcontrol`](https://github.com/jtenniswood/espcontrol), an
actively-maintained ESPHome project with a dedicated
`guition-esp32-p4-jc8012p4a1-v2` device profile for this exact board. That
project is licensed under the
[PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0),
which requires propagating its Required Notice line verbatim to anyone who
gets this code from us:

> Required Notice: Copyright (c) 2026 jtenniswood

### V3: ESP32-P4 v3.x silicon { #paneljc-v3 }

Research (espcontrol `docs/screens/jc8012p4a1-v3.md`, ESPHome issue 19237 and
PR 17843) shows that "V3" units carry an ESP32-P4 **v3.x production chip**
(`esptool chip-id` says revision v3.x, ESPHome needs `engineering_sample:
false`); the rear-case date does not tell them apart, and V3 takes precedence
over the date code. The display init and timing are the same as V2. What is
different:

- This firmware is built for pre-v3 silicon (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`,
  `REV_MIN_100`) and is expected not to run on a v3.x chip (untested: no v3 unit available). A V3 build would need
  `REV_MIN_300` without `SELECTS_REV_LESS_V3`, and has not been built or tried.
- The MIPI DSI PHY clock source must stay unassigned (0): the legacy
  `PLL_F20M` default aborts in `_mipi_dsi_ll_set_phy_pllref_clock_source()` on
  v3 silicon, ESP-IDF then selects XTAL by itself. `display_init_paneljc.c`
  does not set `phy_clk_src`, so it already does the right thing.

Further quirks reported for this board (touch firmware upload, C6 recovery, flash and cache settings) are in the [device technical notes](device-notes.md#paneljc).

### Reported quirks of the JC8012P4A1

Other quirks reported for this board (ESPHome and espcontrol threads):

- Touch: GSL3680 needs its firmware uploaded over I2C at every power-up (the
  same blob as the Seeed reTerminal D1001); a tap that wakes a dark screen must
  still reach the touch driver, so LVGL should keep running while the
  backlight is off (not verified here); noisy multi-contact wake taps occur.
- Wi-Fi: the ESP32-C6 over SDIO can disconnect repeatedly; a C6 recovery
  flash over USB is the fix. ESP-Hosted's preallocated mempool can fail with
  "HS_MP: mempool create failed: no mem" (espcontrol switches the mempool
  off; this project carries ESP-Hosted fixes in `tools/apply_component_patches.py`).
- Flash/cache: `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` (set here) avoids
  `spi_flash_disable_interrupts_caches_and_other_cpu` asserts and display
  flicker during OTA; changing it needs a full rebuild.
  `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` (ESP-IDF issue 19020),
  `CONFIG_ESP_TASK_WDT_TIMEOUT_S=60` and `CONFIG_SPI_FLASH_YIELD_DURING_ERASE=n`
  (OTA preparation time) are also used by ESPHome/espcontrol and here.
- Reported crash with SNTP/HA time sync plus LVGL on another stack (ESPHome
  issue 17667): a race between the time callback and the LVGL task; not seen here.
- CPU 360 MHz (400 MHz is experimental and did not help boot loops).
