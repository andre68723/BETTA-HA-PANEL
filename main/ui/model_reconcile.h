/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t revision;
    bool valid;
} ui_applied_state_t;

/* Revision order is valid for consumers less than 2^31 changes behind. A
 * newer delivered event also satisfies an older pending recovery entry. */
static inline bool ui_model_already_applied(const ui_applied_state_t *state, uint32_t revision)
{
    return state->valid && state->revision - revision < UINT32_C(0x80000000);
}
