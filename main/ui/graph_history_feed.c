/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/graph_history_feed.h"

#if defined(BETTA_SIMULATOR)
/* The desktop simulator has no Home Assistant: a widget keeps the history its fixture seeds. */
void *graph_history_feed_register(const char *entity_id, lv_obj_t *obj, graph_history_feed_cb_t cb, void *user)
{
    (void)entity_id; (void)obj; (void)cb; (void)user;
    return NULL;
}

void graph_history_feed_unregister(void *handle)
{
    (void)handle;
}
#else

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "ha/ha_history.h"
#include "util/history_feed_policy.h"

#define FEED_TICK_MS 3000

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *obj;
    graph_history_feed_cb_t cb;
    void *user;
    uint32_t generation; /* of the request in flight for this slot */
} feed_entry_t;

static const char *TAG = "graph_feed";
static history_feed_slot_t s_slots[HISTORY_FEED_MAX];
static feed_entry_t s_entries[HISTORY_FEED_MAX];
static history_feed_state_t s_state;
static bool s_state_ready;
static uint32_t s_generation = 0x40000000U; /* apart from the popup's generations */
static lv_timer_t *s_timer;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int used_count(void)
{
    int n = 0;
    for (int i = 0; i < HISTORY_FEED_MAX; i++) {
        if (s_slots[i].used) n++;
    }
    return n;
}

static void feed_result(uint32_t generation, ha_history_snapshot_t *snapshot, esp_err_t status)
{
    const int64_t now = now_ms();
    int slot = -1;
    for (int i = 0; i < HISTORY_FEED_MAX; i++) {
        if (s_slots[i].used && s_entries[i].generation == generation && s_state.inflight == i) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) {
        s_state.inflight = -1;
        s_slots[slot].last_try_ms = now;
        if (status == ESP_OK && snapshot != NULL && snapshot->numeric) {
            s_slots[slot].last_ok_ms = now;
            s_entries[slot].cb(s_entries[slot].user, snapshot);
        } else {
            ESP_LOGW(TAG, "history of %s failed (%s)", s_entries[slot].entity_id, esp_err_to_name(status));
        }
    }
    ha_history_snapshot_free(snapshot);
}

static void feed_tick(lv_timer_t *timer)
{
    (void)timer;
    for (int i = 0; i < HISTORY_FEED_MAX; i++) {
        s_slots[i].visible = s_slots[i].used && s_entries[i].obj != NULL && lv_obj_is_visible(s_entries[i].obj);
    }
    const int64_t now = now_ms();
    const history_feed_decision_t decision = history_feed_decide(s_slots, HISTORY_FEED_MAX, &s_state, now);
    if (decision.timed_out >= 0) {
        ESP_LOGW(TAG, "no answer for the history of %s, asking again later", s_entries[decision.timed_out].entity_id);
        s_slots[decision.timed_out].last_try_ms = now;
        s_state.inflight = -1;
    }
    if (decision.next < 0) {
        return;
    }
    feed_entry_t *entry = &s_entries[decision.next];
    entry->generation = ++s_generation;
    s_slots[decision.next].last_try_ms = now;
    if (ha_history_request(entry->entity_id, true, entry->generation, feed_result) == ESP_OK) {
        s_state.inflight = decision.next;
        s_state.inflight_since_ms = now;
    } else {
        ESP_LOGW(TAG, "could not request the history of %s", entry->entity_id);
    }
}

void *graph_history_feed_register(const char *entity_id, lv_obj_t *obj, graph_history_feed_cb_t cb, void *user)
{
    if (entity_id == NULL || entity_id[0] == '\0' || obj == NULL || cb == NULL) {
        return NULL;
    }
    if (!s_state_ready) {
        history_feed_state_init(&s_state);
        s_state_ready = true;
    }
    for (int i = 0; i < HISTORY_FEED_MAX; i++) {
        if (s_slots[i].used) continue;
        memset(&s_slots[i], 0, sizeof(s_slots[i]));
        memset(&s_entries[i], 0, sizeof(s_entries[i]));
        s_slots[i].used = true;
        strlcpy(s_entries[i].entity_id, entity_id, sizeof(s_entries[i].entity_id));
        s_entries[i].obj = obj;
        s_entries[i].cb = cb;
        s_entries[i].user = user;
        if (s_timer == NULL) {
            s_timer = lv_timer_create(feed_tick, FEED_TICK_MS, NULL);
        }
        return &s_slots[i];
    }
    ESP_LOGW(TAG, "all %d history slots are taken, %s keeps its local history", HISTORY_FEED_MAX, entity_id);
    return NULL;
}

void graph_history_feed_unregister(void *handle)
{
    if (handle == NULL) {
        return;
    }
    const int i = (int)((history_feed_slot_t *)handle - s_slots);
    if (i < 0 || i >= HISTORY_FEED_MAX) {
        return;
    }
    if (s_state.inflight == i) {
        s_state.inflight = -1; /* its answer, if it still comes, is dropped: no matching generation */
    }
    memset(&s_slots[i], 0, sizeof(s_slots[i]));
    memset(&s_entries[i], 0, sizeof(s_entries[i]));
    if (used_count() == 0 && s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
}

#endif /* BETTA_SIMULATOR */
