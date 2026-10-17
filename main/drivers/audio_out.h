/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

bool audio_out_available(void);
/* Owns codec/I2S only for this call; cancel is polled between short chunks. */
esp_err_t audio_out_play(const int16_t *pcm, size_t samples, int volume,
                         bool (*cancel)(void));
