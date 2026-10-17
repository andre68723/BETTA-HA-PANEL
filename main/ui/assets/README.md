<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk | Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Splash House Image (Optional)

You can replace the boot splash center emblem with a static image.

## Files

Place these files in this folder:

- `splash_house_image.h`
- `splash_house_image.c`

The symbol name must be:

- `splash_house_image`

Example declaration in `splash_house_image.h`:

```c
#pragma once
#include "lvgl.h"
LV_IMAGE_DECLARE(splash_house_image);
```

## Important for performance/stability

- Use LVGL image converter output as C array (not runtime JPG decode on target).
- Keep the image at near-final display size (avoid runtime scaling/rotation).
- Prefer ARGB8888 or RGB565A8 output from converter.

This avoids expensive software image transform paths on ESP32-P4 during boot splash.

# Boot splash logo (BETTA fish)

The splash and OTA screens draw the symbol `SMART86OS_Betta`. Two sources
exist; CMake picks the first that is available:

- `SMART86OS_Betta.png` plus the stub `SMART86OS_Betta_png.c`: the logo as an
  embedded lossless PNG (108 KB), decoded by LVGL's PNG decoder
  (`LV_USE_LODEPNG`) when it is drawn (about 125 ms on the ESP32-P4, image
  cache off). Used when the PNG exists.
- `SMART86OS_Betta.c`: the original 246 KB raw RGB565 array (370x340), kept as
  the fallback.

The PNG was generated from the array without loss: expand each RGB565 pixel to
RGB888 (`(v<<3)|(v>>2)` for 5-bit and `(v<<2)|(v>>4)` for 6-bit channels) and
save it with Pillow's `optimize=True`. Decoding it on the device reproduces the
original pixels exactly (checked with a CRC over the decoded ARGB8888 data).
The artwork is Copyright (c) 2026 Cpt_Kirk; the PNG is the same image, re-encoded.
