/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/chime_synth.h"
#include <math.h>
#include <string.h>

bool chime_kind_parse(const char *text, chime_kind_t *out)
{
    if (!text || !out) return false;
    if (!strcmp(text, "none")) *out = CHIME_NONE;
    else if (!strcmp(text, "chime")) *out = CHIME_DOORBELL;
    else if (!strcmp(text, "notify")) *out = CHIME_NOTIFY;
    else return false;
    return true;
}

bool chime_should_play(bool enabled, bool night_mute, bool night_active, int volume)
{
    return enabled && volume > 0 && !(night_mute && night_active);
}

size_t chime_synth_samples(chime_kind_t kind)
{
    return kind == CHIME_DOORBELL ? CHIME_SAMPLE_RATE * 12 / 10
         : kind == CHIME_NOTIFY ? CHIME_SAMPLE_RATE * 3 / 10 : 0;
}

void chime_synth_render(chime_kind_t kind, size_t offset, int16_t *pcm, size_t count)
{
    if (!pcm) return;
    size_t length = chime_synth_samples(kind);
    for (size_t i = 0; i < count; i++) {
        size_t n = offset + i;
        if (n >= length) { pcm[i] = 0; continue; }
        size_t tone_start = kind == CHIME_DOORBELL && n >= 8000 ? 8000 : 0;
        size_t tone_end = kind == CHIME_DOORBELL && n < 8000 ? 8000 : length;
        double t = (double)(n - tone_start) / CHIME_SAMPLE_RATE;
        double frequency = kind == CHIME_DOORBELL && tone_start ? 523.25 : 659.25;
        double attack = fmin(1.0, (double)(n - tone_start) / 320);
        double release = fmin(1.0, (double)(tone_end - 1 - n) / 480);
        /* About 80 % of full scale (a quiet speaker made the old 37 % peak and the fast decay barely audible). */
        double envelope = attack * release * exp(-2.5 * t);
        pcm[i] = (int16_t)(26000 * envelope * sin(6.283185307179586 * frequency * t));
    }
}
