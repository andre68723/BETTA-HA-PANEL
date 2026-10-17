/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"
/* All mutations run under flash_gate. Only a successfully parsed saved layout authorizes cleanup. */
void graph_history_store_id(const char *id, char *out, size_t capacity);
esp_err_t graph_history_store_cleanup(const char *directory, const cJSON *layout);
esp_err_t graph_history_store_save(const char *directory, const char *path, const cJSON *layout,
    const void *data, size_t length, size_t fs_total, size_t fs_used);

/* Nonblocking request; worker starts even when a layout contains no graphs. */
void w_graph_request_history_cleanup(void);
