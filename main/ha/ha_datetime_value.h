/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    HA_DATETIME_INPUT,
    HA_DATETIME_TIME,
    HA_DATETIME_DATE,
    HA_DATETIME_UTC,
} ha_datetime_kind_t;

typedef struct {
    ha_datetime_kind_t kind;
    bool has_date;
    bool has_time;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} ha_datetime_value_t;

/* datetime.* is converted from its ISO offset into the process's configured local TZ. */
bool ha_datetime_parse(const char *entity_id, const char *state, const char *attributes_json,
                       ha_datetime_value_t *out);
bool ha_datetime_format(const ha_datetime_value_t *value, const char *language, char *out, size_t out_len);
/* Returns the HA service domain/name and a JSON service_data body. For datetime.* the local
 * fields are converted back to UTC; invalid DST spring-forward times are rejected. */
bool ha_datetime_service_payload(const char *entity_id, const ha_datetime_value_t *value,
                                 const char **domain, const char **service, char *out, size_t out_len);
