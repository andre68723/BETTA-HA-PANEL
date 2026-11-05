# Waveshare LCD-X BSP override

Official Waveshare `esp32_p4_wifi6_touch_lcd_x` 2.0.3, Apache-2.0.
Registry source: https://components.espressif.com/components/waveshare/esp32_p4_wifi6_touch_lcd_x/versions/2.0.3
Archive SHA256: `98f2c92c297e3478441305cd6e573e4cc31c9fb50ae9b4a60696bffdae79b62c`.
Upstream repository revision: `0694fdb9fa7751cc2719a9d1927de70547b89ba0`,
path `bsp/esp32_p4_wifi6_touch_lcd_x`, as recorded in the registry manifest.
Original headers and LICENSE are retained; line endings are normalized to LF and trailing whitespace is removed.

Local changes by andre68723:

- Pin the adapter constraint from `~0.6` to `0.7.5`, matching BETTA's shared runtime.
- Select the named DSI PHY clock default for the P4 silicon family instead of
  numeric zero. This follows the manufacturer factory BSP at Waveshare repository commit
  `f15098fe62072156db1e7baf9a0945801f8a8877`; Rev3.x uses the PLL-reference clock enum.

BETTA uses only the BSP panel/touch handles and PWM backlight. Its own
`display_port.c` owns LVGL and the adapter task. The shared BETTA audio path drives ES8311 output on the BSP pins; the camera
uses the BSP I2C handle with esp_video. Microphones, SD and USB host helpers
are not initialized. The panel10 profile selects the 10.1-inch JD9365
panel, two DPI framebuffers and the shared fixed 270-degree landscape view.
Touch uses the GT911-family driver for GT9271 with unassigned reset/interrupt
pins, 0x5D/0x14 probing and polling. No Nano board compatibility is claimed.
Hardware operation in this fork remains unverified.
