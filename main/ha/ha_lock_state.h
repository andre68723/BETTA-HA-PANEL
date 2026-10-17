/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * A lock entity's state and what a tap on the lock tile does, without LVGL. Pure C, host tested.
 */
#pragma once

#include <stdbool.h>

typedef enum {
    HA_LOCK_UNKNOWN = 0, /* unknown, unavailable or an unrecognised state */
    HA_LOCK_LOCKED,
    HA_LOCK_UNLOCKED,
    HA_LOCK_LOCKING,
    HA_LOCK_UNLOCKING,
    HA_LOCK_OPEN,
    HA_LOCK_OPENING,
    HA_LOCK_JAMMED,
} ha_lock_state_t;

ha_lock_state_t ha_lock_state_parse(const char *state);
/* The lock service a tap on the tile calls: "unlock" when locked, "lock" when unlocked, open or
 * jammed (retry locking), NULL while it is moving or unknown. */
const char *ha_lock_tap_service(ha_lock_state_t state);
/* True for states that leave the door unsecured (shown as the tile's active state). */
bool ha_lock_state_unsecured(ha_lock_state_t state);
/* LockEntityFeature.OPEN (supported_features bit 0) in the entity's attributes JSON: the lock can
 * unlatch the door (lock.open), not only retract the bolt. */
bool ha_lock_supports_open(const char *attributes_json);
/* The translation key of the state text, e.g. "lock.state.jammed"; "common.unknown" otherwise. */
const char *ha_lock_state_key(ha_lock_state_t state);
