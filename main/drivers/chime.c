/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/chime.h"
#if defined(BETTA_SIMULATOR)
void chime_configure(bool enabled, int volume, bool mute) { (void)enabled; (void)volume; (void)mute; }
bool chime_request(chime_kind_t kind, bool test, int volume) { (void)kind; (void)test; (void)volume; return false; }
#else
#include "drivers/audio_out.h"
#include "settings/night_mode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_enabled, s_running;
static bool s_night_mute = true;
static int s_volume = 40, s_pending_volume;
static chime_kind_t s_pending;
static bool s_cancel;

void chime_configure(bool enabled, int volume, bool night_mute)
{
    portENTER_CRITICAL(&s_lock);
    s_enabled = enabled;
    s_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    s_night_mute = night_mute;
    portEXIT_CRITICAL(&s_lock);
}

static bool cancelled(void)
{
    portENTER_CRITICAL(&s_lock);
    bool result = s_cancel;
    portEXIT_CRITICAL(&s_lock);
    return result;
}

static void playback_task(void *arg)
{
    (void)arg;
    for (;;) {
        portENTER_CRITICAL(&s_lock);
        chime_kind_t kind = s_pending;
        int volume = s_pending_volume;
        s_pending = CHIME_NONE;
        s_cancel = false;
        if (kind == CHIME_NONE) s_running = false;
        portEXIT_CRITICAL(&s_lock);
        if (kind == CHIME_NONE) break;
        size_t samples = chime_synth_samples(kind);
        int16_t *pcm = heap_caps_malloc(samples * sizeof(*pcm), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pcm) { ESP_LOGW("chime", "No PSRAM for chime"); continue; }
        chime_synth_render(kind, 0, pcm, samples);
        if (!cancelled()) (void)audio_out_play(pcm, samples, volume, cancelled);
        free(pcm);
    }
    vTaskDelete(NULL);
}

bool chime_request(chime_kind_t kind, bool test, int test_volume)
{
    if (!audio_out_available() || !chime_synth_samples(kind) || test_volume < 0 || test_volume > 100) return false;
    bool night = night_mode_active();
    portENTER_CRITICAL(&s_lock);
    int volume = test ? test_volume : s_volume;
    if (!test && !chime_should_play(s_enabled, s_night_mute, night, volume)) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    s_pending = kind;
    s_pending_volume = volume;
    s_cancel = true;
    bool start = !s_running;
    s_running = true;
    portEXIT_CRITICAL(&s_lock);
    if (start && xTaskCreatePinnedToCore(playback_task, "chime", 4096, NULL, 3, NULL, 1) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_running = false;
        s_pending = CHIME_NONE;
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    return true;
}

#endif
