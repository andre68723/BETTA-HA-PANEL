/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Image descriptor for the BETTA splash logo, backed by the embedded PNG
 * SMART86OS_Betta.png instead of the 246 KB raw RGB565 array in
 * SMART86OS_Betta.c. The artwork is the same (Copyright (c) 2026 Cpt_Kirk),
 * re-encoded losslessly; LVGL's PNG decoder (LV_USE_LODEPNG) expands it when
 * it is drawn. main/CMakeLists.txt compiles this file instead of
 * SMART86OS_Betta.c when the PNG exists; both define the symbol
 * SMART86OS_Betta. See main/ui/assets/README.md.
 */
#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

/* Set by main/CMakeLists.txt from the size of the PNG file. */
#ifndef SMART86OS_BETTA_PNG_SIZE
#error "SMART86OS_BETTA_PNG_SIZE must be defined by the build"
#endif

extern const uint8_t _binary_SMART86OS_Betta_png_start[] asm("_binary_SMART86OS_Betta_png_start");

const lv_image_dsc_t SMART86OS_Betta = {
    .header.cf = LV_COLOR_FORMAT_RAW,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = 370,
    .header.h = 340,
    .data_size = SMART86OS_BETTA_PNG_SIZE,
    .data = _binary_SMART86OS_Betta_png_start,
};
