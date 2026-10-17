/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/ota_chip_rev.h"

#include <stdio.h>

/* esp_image_header_t, packed: magic 0, segment_count 1, spi_mode 2, spi_speed_size 3, entry_addr 4..7, wp_pin 8,
 * spi_pin_drv 9..11, chip_id 12..13, min_chip_rev 14, min_chip_rev_full 15..16, max_chip_rev_full 17..18. */
#define IMAGE_MAGIC 0xE9
#define OFFSET_MIN_FULL 15
#define OFFSET_MAX_FULL 17
#define HEADER_MIN_LEN 19

bool ota_image_revs_parse(const uint8_t *head, size_t len, ota_image_revs_t *out)
{
    if (head == NULL || out == NULL || len < HEADER_MIN_LEN || head[0] != IMAGE_MAGIC) {
        return false;
    }
    out->min_rev = (uint16_t)(head[OFFSET_MIN_FULL] | (head[OFFSET_MIN_FULL + 1] << 8));
    out->max_rev = (uint16_t)(head[OFFSET_MAX_FULL] | (head[OFFSET_MAX_FULL + 1] << 8));
    return true;
}

ota_rev_result_t ota_image_revs_check(const ota_image_revs_t *image, uint16_t chip_rev)
{
    if (image == NULL) {
        return OTA_REV_OK;
    }
    if (chip_rev < image->min_rev) {
        return OTA_REV_BELOW_MIN;
    }
    if (image->max_rev != 0xFFFFU && chip_rev > image->max_rev) {
        return OTA_REV_ABOVE_MAX;
    }
    return OTA_REV_OK;
}

size_t ota_rev_format(uint16_t rev, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0U) {
        return 0U;
    }
    int n = snprintf(out, out_len, "v%u.%u", (unsigned)(rev / 100U), (unsigned)(rev % 100U));
    if (n < 0) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)n < out_len ? (size_t)n : out_len - 1U;
}

size_t ota_rev_mismatch_text(const ota_image_revs_t *image, uint16_t chip_rev, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0U) {
        return 0U;
    }
    out[0] = '\0';
    if (image == NULL) {
        return 0U;
    }
    char lo[12], hi[12], chip[12];
    (void)ota_rev_format(image->min_rev, lo, sizeof(lo));
    (void)ota_rev_format(image->max_rev, hi, sizeof(hi));
    (void)ota_rev_format(chip_rev, chip, sizeof(chip));
    const bool newer_chip = ota_image_revs_check(image, chip_rev) == OTA_REV_ABOVE_MAX;
    int n;
    if (image->max_rev == 0xFFFFU) {
        n = snprintf(out, out_len, "This image supports ESP32-P4 %s and newer, this chip is %s", lo, chip);
    } else {
        n = snprintf(out, out_len, "This image supports ESP32-P4 %s to %s, this chip is %s (%s)", lo, hi, chip,
                     newer_chip ? "a v3 image is needed" : "an older-silicon image is needed");
    }
    if (n < 0) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)n < out_len ? (size_t)n : out_len - 1U;
}
