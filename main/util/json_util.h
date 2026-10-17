/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

cJSON *json_util_parse(const char *json);
char *json_util_print_unformatted(const cJSON *json);
bool json_util_get_string(const cJSON *obj, const char *key, const char **out_value);
bool json_util_get_int(const cJSON *obj, const char *key, int *out_value);
void json_util_safe_free(char **ptr);

/* A JSON number as an unsigned 32-bit value: 0 for a non-number, NaN, negative or zero input, UINT32_MAX for a value
 * that is too large. Converting an out-of-range double to an integer is undefined in C, so the server's numbers
 * (message ids, feature masks) go through this. Includes <stdint.h> for the result type. */
uint32_t json_util_number_u32(const cJSON *item);
