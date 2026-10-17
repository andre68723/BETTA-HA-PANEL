/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ha/ha_camera_stream.h"
#include "ha/ha_client.h"
#include "host_adapters.h"
#include <stdlib.h>
#include <stdio.h>

struct ha_camera_stream { unsigned sequence; };
static unsigned active, starts;
static bool sleeping;
static ha_camera_stream_status_t state = HA_CAMERA_LIVE;
unsigned sim_camera_active(void) { return active; }
unsigned sim_camera_starts(void) { return starts; }
void sim_camera_status(ha_camera_stream_status_t value) { state = value; }
void sim_camera_sleep(bool value) { sleeping = value; }
bool ui_display_protection_is_active(void) { return sleeping; }

ha_camera_stream_t *ha_camera_stream_start(const char *entity, int w, int h)
{
    (void)w; (void)h;
    if (!entity || active >= 2) return NULL;
    ha_camera_stream_t *s = calloc(1, sizeof(*s));
    if (s) { active++; starts++; fprintf(stderr, "Simulator camera: synthetic frames, no network\n"); }
    return s;
}
void ha_camera_stream_stop(ha_camera_stream_t *s)
{
    if (s) { active--; free(s); }
}
bool ha_camera_stream_take(ha_camera_stream_t *s, ha_cover_result_t *out, ha_camera_stream_status_t *status)
{
    *status = ha_client_is_connected() ? state : HA_CAMERA_FAILED;
    if (*status != HA_CAMERA_LIVE) return false;
    uint16_t *data = malloc(64 * 36 * sizeof(*data));
    if (!data) return false;
    for (unsigned y = 0; y < 36; y++) for (unsigned x = 0; x < 64; x++) {
        data[y * 64 + x] = (uint16_t)((((x + s->sequence) % 32) << 11) | ((y % 32) << 5));
    }
    s->sequence++;
    *out = (ha_cover_result_t){
        .valid = true, .source_w = 64, .source_h = 36,
        .image = {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                            .w = 64, .h = 36, .stride = 128},
                  .data = (const uint8_t *)data, .data_size = 64 * 36 * 2},
    };
    return true;
}
