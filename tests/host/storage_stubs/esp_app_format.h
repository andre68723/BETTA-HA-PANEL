/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdint.h>
typedef struct { uint8_t data[24]; } esp_image_header_t;
typedef struct { uint8_t data[8]; } esp_image_segment_header_t;
