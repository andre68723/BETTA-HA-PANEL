/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Reduce a Home Assistant Recorder response as it arrives, without holding the JSON reply.
 */
#include "ha/ha_history_reduce.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "util/iso_time.h"

static void reduce_object(ha_history_reducer_t *r)
{
    cJSON *item = cJSON_Parse(r->object);
    if (item == NULL) return;
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(item, "state");
    const cJSON *when = cJSON_GetObjectItemCaseSensitive(item, "last_changed");
    if (!cJSON_IsString(when)) when = cJSON_GetObjectItemCaseSensitive(item, "last_updated");
    int64_t epoch;
    if (cJSON_IsString(state) && cJSON_IsString(when) &&
        iso_time_parse(when->valuestring, &epoch) && epoch >= r->snapshot->start_epoch &&
        epoch <= r->snapshot->end_epoch) {
        ha_history_snapshot_t *s = r->snapshot;
        if (s->numeric) {
            char *end = NULL;
            double value = strtod(state->valuestring, &end);
            size_t bucket = (size_t)((epoch - s->start_epoch) * HA_HISTORY_BUCKETS /
                                     (s->end_epoch - s->start_epoch + 1));
            if (bucket >= HA_HISTORY_BUCKETS) bucket = HA_HISTORY_BUCKETS - 1;
            ha_history_bucket_t *b = &s->buckets[bucket];
            if (end != state->valuestring && *end == '\0' && isfinite(value)) {
                if (b->count == 0 || value < b->min) b->min = (float)value;
                if (b->count == 0 || value > b->max) b->max = (float)value;
                b->sum += (float)value;
                b->last = (float)value;
                b->gap = 0;
                if (b->count < UINT16_MAX) b->count++;
                s->valid_count++;
            } else {
                b->gap = 1; /* unavailable or unknown from here on */
            }
        } else {
            size_t index = (s->event_head + s->event_count) % HA_HISTORY_EVENTS;
            if (s->event_count == HA_HISTORY_EVENTS) {
                index = s->event_head;
                s->event_head = (s->event_head + 1) % HA_HISTORY_EVENTS;
                s->shortened = true;
            } else s->event_count++;
            s->events[index].epoch = epoch;
            snprintf(s->events[index].state, sizeof(s->events[index].state), "%s", state->valuestring);
            s->valid_count++;
        }
    }
    cJSON_Delete(item);
}

void ha_history_reduce_finish(ha_history_snapshot_t *s)
{
    if (s == NULL || !s->numeric) return;
    bool holding = false;
    float hold = 0.0f;
    for (size_t i = 0; i < HA_HISTORY_BUCKETS; i++) {
        ha_history_bucket_t *b = &s->buckets[i];
        if (b->count > 0) {
            holding = !b->gap;
            hold = b->last;
        } else if (b->gap) {
            holding = false;
        } else if (holding) {
            b->min = b->max = b->sum = hold;
            b->last = hold;
            b->count = 1;
        }
    }
}

void ha_history_reduce_init(ha_history_reducer_t *r, ha_history_snapshot_t *s,
                            bool numeric, int64_t start_epoch, int64_t end_epoch)
{
    memset(r, 0, sizeof(*r));
    memset(s, 0, sizeof(*s));
    r->snapshot = s;
    s->numeric = numeric;
    s->start_epoch = start_epoch;
    s->end_epoch = end_epoch;
}

bool ha_history_reduce_feed(ha_history_reducer_t *r, const char *data, size_t len)
{
    if (r == NULL || r->snapshot == NULL || data == NULL) return false;
    for (size_t i = 0; i < len; i++) {
        char ch = data[i];
        if (r->depth == 0) {
            if (ch != '{') continue;
            r->depth = 1;
            r->object_len = 0;
            r->object_overflow = false;
            r->in_string = false;
            r->escaped = false;
        } else if (r->in_string) {
            if (r->escaped) r->escaped = false;
            else if (ch == '\\') r->escaped = true;
            else if (ch == '"') r->in_string = false;
        } else if (ch == '"') r->in_string = true;
        else if (ch == '{') r->depth++;
        else if (ch == '}') r->depth--;
        if (!r->object_overflow) {
            if (r->object_len + 1 < sizeof(r->object)) r->object[r->object_len++] = ch;
            else r->object_overflow = true;
        }
        if (r->depth == 0) {
            if (!r->object_overflow) {
                r->object[r->object_len] = '\0';
                reduce_object(r);
            } else r->snapshot->shortened = true;
        }
    }
    return true;
}

const ha_history_event_t *ha_history_event_at(const ha_history_snapshot_t *s, size_t index)
{
    if (s == NULL || index >= s->event_count) return NULL;
    return &s->events[(s->event_head + index) % HA_HISTORY_EVENTS];
}
