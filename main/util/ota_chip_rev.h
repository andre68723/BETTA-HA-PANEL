/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Chip revision check for OTA images. An ESP32-P4 image is built for one range of silicon revisions (the standard images
 * for v1.x, revisions 100..199; ESP-IDF cannot mix v1.x and v3.x in one image). The second stage bootloader refuses an
 * image outside the chip's revision only at boot, after the image has been written. This reads the range from the
 * image header the editor or the updater is about to write and rejects a mismatch before anything is written, with
 * a message that names both ranges. Pure (host tested); api_ota.c passes the header bytes and the chip's revision.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t min_rev; /* lowest chip revision the image supports, major * 100 + minor (v1.3 = 103) */
    uint16_t max_rev; /* highest chip revision it supports; 0xFFFF in the header means no upper limit */
} ota_image_revs_t;

typedef enum {
    OTA_REV_OK = 0,
    OTA_REV_BELOW_MIN, /* the chip is older than the image supports */
    OTA_REV_ABOVE_MAX, /* the chip is newer than the image supports (e.g. a v3.x chip and a v1.x image) */
} ota_rev_result_t;

/* Reads the range from the first bytes of an app image (esp_image_header_t). False when the bytes are too few or the magic
 * byte 0xE9 is missing. */
bool ota_image_revs_parse(const uint8_t *head, size_t len, ota_image_revs_t *out);
ota_rev_result_t ota_image_revs_check(const ota_image_revs_t *image, uint16_t chip_rev);
/* "v1.3" for 103, "v3.1" for 301. Returns the text length. */
size_t ota_rev_format(uint16_t rev, char *out, size_t out_len);
/* One sentence for the editor: "This image supports ESP32-P4 v1.0 to v1.99, this chip is v3.1 (a v3 image is needed)". */
size_t ota_rev_mismatch_text(const ota_image_revs_t *image, uint16_t chip_rev, char *out, size_t out_len);
