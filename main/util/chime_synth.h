/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { CHIME_NONE, CHIME_DOORBELL, CHIME_NOTIFY } chime_kind_t;
#define CHIME_SAMPLE_RATE 16000
bool chime_kind_parse(const char *text, chime_kind_t *out);
bool chime_should_play(bool enabled, bool night_mute, bool night_active, int volume);
size_t chime_synth_samples(chime_kind_t kind);
/* Bounded amplitude and smooth attack/release; no audio assets. */
void chime_synth_render(chime_kind_t kind, size_t offset, int16_t *pcm, size_t count);
