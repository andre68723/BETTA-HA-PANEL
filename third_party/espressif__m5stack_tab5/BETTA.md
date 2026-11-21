<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -->
# M5Stack Tab5 BSP override

Official Espressif `m5stack_tab5` 1.3.3, Apache-2.0.
[Registry source](https://components.espressif.com/components/espressif/m5stack_tab5/versions/1.3.3/readme).
Archive SHA-256 `66d818452346ee3defc464ccee2979b4e1bcb757796d8cce3b80f771dfaa2913`.
[Upstream source](https://github.com/espressif/esp-bsp/tree/fb5714e02cf4289d6f092f817a7ef1e0b72a49ed/bsp/m5stack_tab5).

- Pin `esp_codec_dev` to 1.6.2, matching the other BETTA profiles. Component
  resolution runs before the configure-time source patch hook.
- Preserve BETTA's existing handling of a null display configuration in
  `src/bsp_display.c`. The default DSI lane rate follows the BSP's own start
  path, including the lower ST7121 rate. This guard previously lived in
  `tools/apply_component_patches.py`.
- Point the API guide's links to sibling components at the registry.
