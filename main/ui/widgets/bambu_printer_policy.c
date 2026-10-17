/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/bambu_printer_policy.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bambu_state_t bambu_state_parse(const char *raw)
{
    static const struct {
        const char *raw;
        bambu_state_t state;
    } map[] = {
        {"offline", BAMBU_STATE_OFFLINE}, {"idle", BAMBU_STATE_IDLE},       {"init", BAMBU_STATE_INIT},
        {"prepare", BAMBU_STATE_PREPARE}, {"slicing", BAMBU_STATE_SLICING}, {"running", BAMBU_STATE_RUNNING},
        {"pause", BAMBU_STATE_PAUSE},     {"finish", BAMBU_STATE_FINISH},   {"failed", BAMBU_STATE_FAILED},
    };
    if (raw == NULL || raw[0] == '\0') {
        return BAMBU_STATE_MISSING;
    }
    if (strcmp(raw, "unavailable") == 0 || strcmp(raw, "unknown") == 0) {
        return BAMBU_STATE_UNAVAILABLE;
    }
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(raw, map[i].raw) == 0) {
            return map[i].state;
        }
    }
    return BAMBU_STATE_OTHER;
}

static const struct {
    const char *key;
    const char *fallback;
} k_labels[] = {
    [BAMBU_STATE_MISSING] = {"bambu.state.missing", "No data"},
    [BAMBU_STATE_UNAVAILABLE] = {"bambu.state.unavailable", "Unavailable"},
    [BAMBU_STATE_OTHER] = {"bambu.state.other", "Unknown"},
    [BAMBU_STATE_OFFLINE] = {"bambu.state.offline", "Offline"},
    [BAMBU_STATE_IDLE] = {"bambu.state.idle", "Idle"},
    [BAMBU_STATE_INIT] = {"bambu.state.init", "Starting"},
    [BAMBU_STATE_PREPARE] = {"bambu.state.prepare", "Preparing"},
    [BAMBU_STATE_SLICING] = {"bambu.state.slicing", "Slicing"},
    [BAMBU_STATE_RUNNING] = {"bambu.state.running", "Printing"},
    [BAMBU_STATE_PAUSE] = {"bambu.state.pause", "Paused"},
    [BAMBU_STATE_FINISH] = {"bambu.state.finish", "Finished"},
    [BAMBU_STATE_FAILED] = {"bambu.state.failed", "Failed"},
};

const char *bambu_state_key(bambu_state_t state)
{
    return state <= BAMBU_STATE_FAILED ? k_labels[state].key : k_labels[BAMBU_STATE_OTHER].key;
}

const char *bambu_state_fallback(bambu_state_t state)
{
    return state <= BAMBU_STATE_FAILED ? k_labels[state].fallback : k_labels[BAMBU_STATE_OTHER].fallback;
}

bool bambu_state_job_active(bambu_state_t state)
{
    return state == BAMBU_STATE_PREPARE || state == BAMBU_STATE_SLICING || state == BAMBU_STATE_RUNNING ||
           state == BAMBU_STATE_PAUSE;
}

bool bambu_state_job_last(bambu_state_t state)
{
    return state == BAMBU_STATE_FINISH || state == BAMBU_STATE_FAILED;
}

bool bambu_parse_number(const char *text, float *out)
{
    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    char *end = NULL;
    const double value = strtod(text, &end);
    if (end == text || *end != '\0' || !isfinite(value) || fabs(value) > 1e9) {
        return false;
    }
    *out = (float)value;
    return true;
}

bool bambu_progress_percent(const char *text, int *out)
{
    float value = 0.0f;
    if (out == NULL || !bambu_parse_number(text, &value)) {
        return false;
    }
    int percent = (int)lroundf(value);
    *out = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    return true;
}

bool bambu_remaining_minutes(const char *text, const char *unit, int *out_minutes)
{
    float value = 0.0f;
    if (out_minutes == NULL || unit == NULL || !bambu_parse_number(text, &value) || value < 0.0f) {
        return false;
    }
    double minutes;
    if (strcmp(unit, "min") == 0) {
        minutes = value;
    } else if (strcmp(unit, "h") == 0) {
        minutes = (double)value * 60.0;
    } else if (strcmp(unit, "s") == 0) {
        minutes = (double)value / 60.0;
    } else {
        return false;
    }
    /* A hair of tolerance: 0.2833 h stored as a float must not turn 17 minutes into 18. */
    minutes = ceil(minutes - 0.01);
    if (minutes < 0.0) {
        minutes = 0.0;
    }
    if (minutes > 99999.0) {
        return false;
    }
    *out_minutes = (int)minutes;
    return true;
}

bool bambu_button_usable(const char *button_state)
{
    return button_state != NULL && button_state[0] != '\0' && strcmp(button_state, "unavailable") != 0;
}

bambu_actions_t bambu_actions(bambu_state_t state, bool online, bool pending, bool pause_usable, bool resume_usable,
                              bool stop_usable)
{
    bambu_actions_t actions = {0};
    if (!online || pending) {
        actions.primary_resume = state == BAMBU_STATE_PAUSE;
        return actions;
    }
    if (state == BAMBU_STATE_RUNNING) {
        actions.primary_enabled = pause_usable;
        actions.stop_enabled = stop_usable;
    } else if (state == BAMBU_STATE_PAUSE) {
        actions.primary_resume = true;
        actions.primary_enabled = resume_usable;
        actions.stop_enabled = stop_usable;
    }
    return actions;
}

bool bambu_command_confirmed(bambu_command_t command, bambu_state_t state)
{
    switch (command) {
    case BAMBU_CMD_PAUSE:
        return state == BAMBU_STATE_PAUSE;
    case BAMBU_CMD_RESUME:
        return state == BAMBU_STATE_RUNNING;
    case BAMBU_CMD_STOP:
        return state == BAMBU_STATE_IDLE || state == BAMBU_STATE_FAILED || state == BAMBU_STATE_FINISH;
    }
    return false;
}
