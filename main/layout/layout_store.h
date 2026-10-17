/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "cJSON.h"
#include "esp_err.h"

esp_err_t layout_store_init(void);
esp_err_t layout_store_load(char **json_out);
esp_err_t layout_store_save(const char *json);
const char *layout_store_default_json(void);
/* The stored layout (or the built-in default when none is stored) parsed; NULL without memory or
 * on a parse error. The caller deletes it. */
cJSON *layout_store_load_parsed(void);
