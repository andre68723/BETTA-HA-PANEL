/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * An alarm_control_panel entity's state, the actions its tile offers and when they need a code,
 * without LVGL. Pure C, host tested.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

struct cJSON;

typedef enum {
    HA_ALARM_UNKNOWN = 0, /* unknown, unavailable or an unrecognised state */
    HA_ALARM_DISARMED,
    HA_ALARM_ARMED_HOME,
    HA_ALARM_ARMED_AWAY,
    HA_ALARM_ARMED_NIGHT,
    HA_ALARM_ARMED_VACATION,
    HA_ALARM_ARMED_CUSTOM_BYPASS,
    HA_ALARM_ARMING,
    HA_ALARM_PENDING,
    HA_ALARM_DISARMING,
    HA_ALARM_TRIGGERED,
} ha_alarm_state_t;

typedef enum {
    HA_ALARM_ACTION_DISARM = 0,
    HA_ALARM_ACTION_ARM_HOME,
    HA_ALARM_ACTION_ARM_AWAY,
    HA_ALARM_ACTION_ARM_NIGHT,
    HA_ALARM_ACTION_ARM_VACATION,
    HA_ALARM_ACTION_ARM_CUSTOM_BYPASS,
    HA_ALARM_ACTION_COUNT,
} ha_alarm_action_t;

typedef enum {
    HA_ALARM_CODE_NONE = 0, /* no code needed */
    HA_ALARM_CODE_NUMBER,   /* code_format "number": digits only */
    HA_ALARM_CODE_TEXT,     /* code_format "text": any characters */
} ha_alarm_code_format_t;

ha_alarm_state_t ha_alarm_state_parse(const char *state);
/* The translation key of the state text, e.g. "alarm.state.armed_home"; "common.unknown" otherwise. */
const char *ha_alarm_state_key(ha_alarm_state_t state);
/* Armed in any mode (the tile's active state). Arming and pending are not armed yet. */
bool ha_alarm_state_armed(ha_alarm_state_t state);
/* Pending (entry delay) and triggered: the tile is shown in the alert colour. */
bool ha_alarm_state_alert(ha_alarm_state_t state);

/* The alarm_control_panel service of an action, e.g. "alarm_arm_home". */
const char *ha_alarm_action_service(ha_alarm_action_t action);
/* The translation key of an action's label, e.g. "alarm.action.arm_home". */
const char *ha_alarm_action_key(ha_alarm_action_t action);

/* The actions a tap on the tile offers in `state`, in display order: disarm while armed, arming,
 * pending or triggered; the arm modes in supported_features while disarmed; nothing while the state
 * is unknown or disarming. Returns the count written to `out` (at most `max`). */
size_t ha_alarm_actions(ha_alarm_state_t state, const char *attributes_json, ha_alarm_action_t *out, size_t max);

/* The action named in a layout's `alarm_actions` array ("disarm", "arm_home", "arm_away", "arm_night",
 * "arm_vacation", "arm_custom_bypass"). False for any other name. */
bool ha_alarm_action_from_name(const char *name, ha_alarm_action_t *out);
/* Bit mask (1 << action) of the valid names in a JSON array of strings; 0 when it is not an array or
 * holds no valid name, which means every action is offered. */
unsigned ha_alarm_action_mask_parse(const struct cJSON *array);
/* Keeps the actions whose bit is set in `mask` (0 keeps all), in order. Returns the new count. */
size_t ha_alarm_actions_filter(ha_alarm_action_t *actions, size_t count, unsigned mask);

/* code_format of the attributes JSON: none when it is null or absent. */
ha_alarm_code_format_t ha_alarm_code_format(const char *attributes_json);
/* True when the action must carry a code: a code_format is set, and for the arm actions also
 * code_arm_required (default true) is not false. Disarm always needs the code. */
bool ha_alarm_action_needs_code(ha_alarm_action_t action, const char *attributes_json);
