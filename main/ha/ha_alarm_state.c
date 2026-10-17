/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_alarm_state.h"

#include <string.h>

#include "cJSON.h"

/* AlarmControlPanelEntityFeature bits in supported_features. */
#define FEATURE_ARM_HOME 1
#define FEATURE_ARM_AWAY 2
#define FEATURE_ARM_NIGHT 4
#define FEATURE_ARM_CUSTOM_BYPASS 16
#define FEATURE_ARM_VACATION 32

static const struct {
    const char *name;
    ha_alarm_state_t state;
    const char *key;
} s_states[] = {
    {"disarmed", HA_ALARM_DISARMED, "alarm.state.disarmed"},
    {"armed_home", HA_ALARM_ARMED_HOME, "alarm.state.armed_home"},
    {"armed_away", HA_ALARM_ARMED_AWAY, "alarm.state.armed_away"},
    {"armed_night", HA_ALARM_ARMED_NIGHT, "alarm.state.armed_night"},
    {"armed_vacation", HA_ALARM_ARMED_VACATION, "alarm.state.armed_vacation"},
    {"armed_custom_bypass", HA_ALARM_ARMED_CUSTOM_BYPASS, "alarm.state.armed_custom_bypass"},
    {"arming", HA_ALARM_ARMING, "alarm.state.arming"},
    {"pending", HA_ALARM_PENDING, "alarm.state.pending"},
    {"disarming", HA_ALARM_DISARMING, "alarm.state.disarming"},
    {"triggered", HA_ALARM_TRIGGERED, "alarm.state.triggered"},
};

static const struct {
    const char *service;
    const char *key;
    int feature; /* 0: always available */
} s_actions[HA_ALARM_ACTION_COUNT] = {
    [HA_ALARM_ACTION_DISARM] = {"alarm_disarm", "alarm.action.disarm", 0},
    [HA_ALARM_ACTION_ARM_HOME] = {"alarm_arm_home", "alarm.action.arm_home", FEATURE_ARM_HOME},
    [HA_ALARM_ACTION_ARM_AWAY] = {"alarm_arm_away", "alarm.action.arm_away", FEATURE_ARM_AWAY},
    [HA_ALARM_ACTION_ARM_NIGHT] = {"alarm_arm_night", "alarm.action.arm_night", FEATURE_ARM_NIGHT},
    [HA_ALARM_ACTION_ARM_VACATION] = {"alarm_arm_vacation", "alarm.action.arm_vacation", FEATURE_ARM_VACATION},
    [HA_ALARM_ACTION_ARM_CUSTOM_BYPASS] = {"alarm_arm_custom_bypass", "alarm.action.arm_custom_bypass",
                                           FEATURE_ARM_CUSTOM_BYPASS},
};

ha_alarm_state_t ha_alarm_state_parse(const char *state)
{
    for (size_t i = 0; state != NULL && i < sizeof(s_states) / sizeof(s_states[0]); i++) {
        if (strcmp(state, s_states[i].name) == 0) {
            return s_states[i].state;
        }
    }
    return HA_ALARM_UNKNOWN;
}

const char *ha_alarm_state_key(ha_alarm_state_t state)
{
    for (size_t i = 0; i < sizeof(s_states) / sizeof(s_states[0]); i++) {
        if (s_states[i].state == state) {
            return s_states[i].key;
        }
    }
    return "common.unknown";
}

bool ha_alarm_state_armed(ha_alarm_state_t state)
{
    return state == HA_ALARM_ARMED_HOME || state == HA_ALARM_ARMED_AWAY || state == HA_ALARM_ARMED_NIGHT ||
           state == HA_ALARM_ARMED_VACATION || state == HA_ALARM_ARMED_CUSTOM_BYPASS;
}

bool ha_alarm_state_alert(ha_alarm_state_t state)
{
    return state == HA_ALARM_PENDING || state == HA_ALARM_TRIGGERED;
}

const char *ha_alarm_action_service(ha_alarm_action_t action)
{
    return (unsigned)action < HA_ALARM_ACTION_COUNT ? s_actions[action].service : NULL;
}

const char *ha_alarm_action_key(ha_alarm_action_t action)
{
    return (unsigned)action < HA_ALARM_ACTION_COUNT ? s_actions[action].key : "common.unknown";
}

static int supported_features(const cJSON *attrs)
{
    const cJSON *features = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
    return cJSON_IsNumber(features) ? (int)features->valuedouble : 0;
}

size_t ha_alarm_actions(ha_alarm_state_t state, const char *attributes_json, ha_alarm_action_t *out, size_t max)
{
    size_t n = 0;
    if (out == NULL || max == 0) {
        return 0;
    }
    if (ha_alarm_state_armed(state) || state == HA_ALARM_ARMING || state == HA_ALARM_PENDING ||
        state == HA_ALARM_TRIGGERED) {
        out[n++] = HA_ALARM_ACTION_DISARM;
        return n;
    }
    if (state != HA_ALARM_DISARMED) {
        return 0;
    }
    cJSON *attrs = attributes_json != NULL ? cJSON_Parse(attributes_json) : NULL;
    const int features = supported_features(attrs);
    cJSON_Delete(attrs);
    for (int action = HA_ALARM_ACTION_ARM_HOME; action < HA_ALARM_ACTION_COUNT && n < max; action++) {
        if ((features & s_actions[action].feature) != 0) {
            out[n++] = (ha_alarm_action_t)action;
        }
    }
    return n;
}

bool ha_alarm_action_from_name(const char *name, ha_alarm_action_t *out)
{
    for (int action = 0; name != NULL && action < HA_ALARM_ACTION_COUNT; action++) {
        /* The service is "alarm_" plus the name: "alarm_arm_home" -> "arm_home". */
        if (strcmp(name, s_actions[action].service + 6) == 0) {
            if (out != NULL) {
                *out = (ha_alarm_action_t)action;
            }
            return true;
        }
    }
    return false;
}

unsigned ha_alarm_action_mask_parse(const struct cJSON *array)
{
    unsigned mask = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        ha_alarm_action_t action;
        if (cJSON_IsString(item) && ha_alarm_action_from_name(item->valuestring, &action)) {
            mask |= 1U << action;
        }
    }
    return mask;
}

size_t ha_alarm_actions_filter(ha_alarm_action_t *actions, size_t count, unsigned mask)
{
    if (mask == 0 || actions == NULL) {
        return count;
    }
    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        if ((mask & (1U << actions[i])) != 0) {
            actions[kept++] = actions[i];
        }
    }
    return kept;
}

ha_alarm_code_format_t ha_alarm_code_format(const char *attributes_json)
{
    cJSON *attrs = attributes_json != NULL ? cJSON_Parse(attributes_json) : NULL;
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(attrs, "code_format");
    ha_alarm_code_format_t result = HA_ALARM_CODE_NONE;
    if (cJSON_IsString(format) && format->valuestring != NULL) {
        if (strcmp(format->valuestring, "number") == 0) {
            result = HA_ALARM_CODE_NUMBER;
        } else if (strcmp(format->valuestring, "text") == 0) {
            result = HA_ALARM_CODE_TEXT;
        }
    }
    cJSON_Delete(attrs);
    return result;
}

bool ha_alarm_action_needs_code(ha_alarm_action_t action, const char *attributes_json)
{
    if (ha_alarm_code_format(attributes_json) == HA_ALARM_CODE_NONE) {
        return false;
    }
    if (action == HA_ALARM_ACTION_DISARM) {
        return true;
    }
    cJSON *attrs = attributes_json != NULL ? cJSON_Parse(attributes_json) : NULL;
    const cJSON *required = cJSON_GetObjectItemCaseSensitive(attrs, "code_arm_required");
    const bool needed = !cJSON_IsFalse(required); /* absent: HA's default is true */
    cJSON_Delete(attrs);
    return needed;
}
