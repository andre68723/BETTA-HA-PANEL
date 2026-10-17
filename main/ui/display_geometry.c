/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/display_geometry.h"
#include "app_config.h"
#include <stdatomic.h>

#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
static atomic_int s_rotation = 90;
int display_geometry_rotation(void) { return atomic_load(&s_rotation); }
bool display_geometry_set_rotation(int degrees)
{
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) return false;
    atomic_store(&s_rotation, degrees);
    return true;
}
bool display_geometry_portrait(void) { return display_geometry_rotation() % 180 == 0; }
int display_geometry_width(void) { return display_geometry_portrait() ? 720 : 1280; }
int display_geometry_height(void) { return display_geometry_portrait() ? 1280 : 720; }
#else
int display_geometry_rotation(void) { return 0; }
bool display_geometry_set_rotation(int degrees) { (void)degrees; return false; }
bool display_geometry_portrait(void) { return false; }
int display_geometry_width(void) { return APP_SCREEN_WIDTH; }
int display_geometry_height(void) { return APP_SCREEN_HEIGHT; }
#endif
