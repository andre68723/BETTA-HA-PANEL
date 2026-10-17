/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_lock_state.h"

#include <stddef.h>
#include <string.h>

#include "cJSON.h"

static const struct {
    const char *name;
    ha_lock_state_t state;
    const char *key;
} s_states[] = {
    {"locked", HA_LOCK_LOCKED, "lock.state.locked"},
    {"unlocked", HA_LOCK_UNLOCKED, "lock.state.unlocked"},
    {"locking", HA_LOCK_LOCKING, "lock.state.locking"},
    {"unlocking", HA_LOCK_UNLOCKING, "lock.state.unlocking"},
    {"open", HA_LOCK_OPEN, "lock.state.open"},
    {"opening", HA_LOCK_OPENING, "lock.state.opening"},
    {"jammed", HA_LOCK_JAMMED, "lock.state.jammed"},
};

ha_lock_state_t ha_lock_state_parse(const char *state)
{
    for (size_t i = 0; state != NULL && i < sizeof(s_states) / sizeof(s_states[0]); i++) {
        if (strcmp(state, s_states[i].name) == 0) {
            return s_states[i].state;
        }
    }
    return HA_LOCK_UNKNOWN;
}

const char *ha_lock_tap_service(ha_lock_state_t state)
{
    switch (state) {
    case HA_LOCK_LOCKED: return "unlock";
    case HA_LOCK_UNLOCKED:
    case HA_LOCK_OPEN:
    case HA_LOCK_JAMMED: return "lock";
    case HA_LOCK_LOCKING:
    case HA_LOCK_UNLOCKING:
    case HA_LOCK_OPENING:
    case HA_LOCK_UNKNOWN: break;
    }
    return NULL;
}

bool ha_lock_state_unsecured(ha_lock_state_t state)
{
    return state == HA_LOCK_UNLOCKED || state == HA_LOCK_UNLOCKING || state == HA_LOCK_OPEN ||
           state == HA_LOCK_OPENING || state == HA_LOCK_JAMMED;
}

bool ha_lock_supports_open(const char *attributes_json)
{
    cJSON *attrs = attributes_json != NULL ? cJSON_Parse(attributes_json) : NULL;
    const cJSON *features = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
    const bool open = cJSON_IsNumber(features) && ((int)features->valuedouble & 1) != 0;
    cJSON_Delete(attrs);
    return open;
}

const char *ha_lock_state_key(ha_lock_state_t state)
{
    for (size_t i = 0; i < sizeof(s_states) / sizeof(s_states[0]); i++) {
        if (s_states[i].state == state) {
            return s_states[i].key;
        }
    }
    return "common.unknown";
}
