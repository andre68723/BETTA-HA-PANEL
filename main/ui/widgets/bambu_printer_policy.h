/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Pure state policy of the Bambu printer widget (bambu_printer): normalising the print status of the Home
 * Assistant Bambu Lab integration, parsing its measurements, and deciding which command buttons may be used.
 * No LVGL and no Home Assistant calls, so the host tests cover it.
 */
#pragma once

#include <stdbool.h>

typedef enum {
    BAMBU_STATE_MISSING = 0, /* no state received yet / entity not in the model */
    BAMBU_STATE_UNAVAILABLE, /* HA reports unavailable or unknown */
    BAMBU_STATE_OTHER,       /* a status this firmware does not know: shown verbatim, no controls */
    BAMBU_STATE_OFFLINE,
    BAMBU_STATE_IDLE,
    BAMBU_STATE_INIT,
    BAMBU_STATE_PREPARE,
    BAMBU_STATE_SLICING,
    BAMBU_STATE_RUNNING,
    BAMBU_STATE_PAUSE,
    BAMBU_STATE_FINISH,
    BAMBU_STATE_FAILED,
} bambu_state_t;

typedef enum {
    BAMBU_CMD_PAUSE = 0,
    BAMBU_CMD_RESUME,
    BAMBU_CMD_STOP,
} bambu_command_t;

/* Maps the integration's gcode_state option (running, pause, finish, ...) to a state. An empty text is MISSING. */
bambu_state_t bambu_state_parse(const char *raw);
/* i18n key of a state's label ("bambu.state.running"). */
const char *bambu_state_key(bambu_state_t state);
/* English fallback of that label. */
const char *bambu_state_fallback(bambu_state_t state);
/* Prepare, slicing, running and pause: a job is in progress, its readings are live. */
bool bambu_state_job_active(bambu_state_t state);
/* Finish and failed: the readings are those of the last job and must be labelled as such. */
bool bambu_state_job_last(bambu_state_t state);

/* A finite number from a sensor state. Rejects empty text, unknown/unavailable, trailing characters, NaN and
 * infinity. */
bool bambu_parse_number(const char *text, float *out);
/* A progress reading as a whole percent: parsed, rounded, then clamped to 0..100. */
bool bambu_progress_percent(const char *text, int *out);
/* Remaining time as whole minutes (rounded up) from the state and its unit_of_measurement: min, h or s. A missing or
 * other unit, a negative value or a non-number is rejected: the number alone never decides the unit. */
bool bambu_remaining_minutes(const char *text, const char *unit, int *out_minutes);

/* A button entity can be pressed unless it is missing/unavailable. "unknown" is normal before the first press. */
bool bambu_button_usable(const char *button_state);

typedef struct {
    bool primary_enabled;  /* the left button: Pause while running, Resume while paused */
    bool primary_resume;   /* the left button currently resumes */
    bool stop_enabled;
} bambu_actions_t;

/* Which buttons are enabled. `online` is HA connected and synchronised; `pending` is a command in flight. A button
 * needs both a printer state that allows it and a usable entity. */
bambu_actions_t bambu_actions(bambu_state_t state, bool online, bool pending, bool pause_usable, bool resume_usable,
                              bool stop_usable);
/* True when the printer state is the one the command is expected to cause. */
bool bambu_command_confirmed(bambu_command_t command, bambu_state_t state);
