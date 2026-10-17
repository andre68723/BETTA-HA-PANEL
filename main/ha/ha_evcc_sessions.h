/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "app_config.h"
#define HA_EVCC_SESSION_LIMIT 20
#define HA_EVCC_SESSION_MONTHS 24
/* Completed sessions only. The cache is keyed by the mode entity, resolved through
 * its registry identity on each refresh, never by the editor's remembered label. */
typedef enum { EVCC_SESSIONS_IDLE, EVCC_SESSIONS_LOADING, EVCC_SESSIONS_READY,
               EVCC_SESSIONS_UNSUPPORTED, EVCC_SESSIONS_ERROR } evcc_sessions_status_t;
typedef struct { int64_t start; double energy; char vehicle[APP_MAX_NAME_LEN]; } evcc_session_t;
typedef struct {
    evcc_sessions_status_t status;
    uint32_t version;
    unsigned count;
    evcc_session_t rows[HA_EVCC_SESSION_LIMIT];
} evcc_sessions_t;
void ha_evcc_sessions_request(const char *mode_entity, bool force);
void ha_evcc_sessions_snapshot(const char *mode_entity, evcc_sessions_t *out);
void ha_evcc_sessions_latest(const char *mode_entity, evcc_session_t *out, evcc_sessions_status_t *status, bool *exists);
bool ha_evcc_sessions_is_due(void);
cJSON *ha_evcc_sessions_next_request(uint32_t token);
void ha_evcc_sessions_result(uint32_t token, bool success, const cJSON *result);
void ha_evcc_sessions_send_failed(bool retry);
void ha_evcc_sessions_cancel(void);
/* Pure reduction shared by the worker, simulator fixtures and host tests. */
void ha_evcc_sessions_reduce(evcc_sessions_t *out, const cJSON *rows, const char *loadpoint);
#ifdef BETTA_SIMULATOR
void ha_evcc_sessions_seed(const char *mode_entity, const char *loadpoint, const cJSON *rows);
#endif
