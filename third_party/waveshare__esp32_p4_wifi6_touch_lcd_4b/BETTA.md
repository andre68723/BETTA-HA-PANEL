<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -->
# Waveshare LCD-4B BSP override

Official `waveshare/esp32_p4_wifi6_touch_lcd_4b` 3.0.1 registry archive.
Repository: https://github.com/waveshareteam/Waveshare-ESP32-components
Commit: `69b3e7ba512e3676519196f5d91680445600a101`.
Archive SHA256: `8fb865290e3c4ba53377b1f04de7eda0b586a310b3388704eda4b1583a6a0496`.

The only upstream modification is the `esp_lvgl_adapter` dependency constraint:
`~0.6` becomes `0.7.3`. The newer adapter supports LVGL 9.6 and the custom bitmap
callback used by BETTA's shared rotation layer. The component manager resolves
constraints before the configure-time patch hook, so a local manifest override
is required. The source and Apache-2.0 license remain unchanged.

Only the panel4 manifest selects this override. Hardware initialization stays
in this BSP; BETTA registers displays and touch through its shared adapter layer.
