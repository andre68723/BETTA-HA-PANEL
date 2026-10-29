/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/quote_history_feed.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "ui/ui_memory.h"
#include "util/history_feed_policy.h"

/* Every saved stock tile may have eight charts; one further registration belongs to the popup.
 * Grow in PSRAM as widgets register, rather than silently starving tiles after the first 24 quotes. */
#define FEED_LIMIT (APP_MAX_WIDGETS_TOTAL * 8 + 1)
#define FEED_GROW 24
#define FEED_TICK_MS 2000

typedef struct {
    bool configured;          /* a quote of a supported provider and a period */
    bool priority;
    bool fallback;            /* Easy Stock's endpoint failed: Home Assistant's means instead */
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char symbol[HA_QUOTE_SYMBOL_LEN];
    ha_quote_provider_t provider;
    ha_quote_period_t period;
    double price_hint;
    lv_obj_t *obj;
    quote_history_feed_cb_t cb;
    void *user;
    uint32_t generation;      /* of the request in flight for this slot */
} feed_entry_t;

static history_feed_slot_t *s_slots;
static feed_entry_t *s_entries;
static int s_capacity;
static history_feed_state_t s_state;
static lv_timer_t *s_timer;
static ha_quote_series_t *s_series;        /* scratch, PSRAM */

static bool grow(void)
{
    if (s_capacity >= FEED_LIMIT) return false;
    const int next = s_capacity + FEED_GROW < FEED_LIMIT ? s_capacity + FEED_GROW : FEED_LIMIT;
    history_feed_slot_t *slots = ui_calloc_prefer_psram(next, sizeof(*slots));
    feed_entry_t *entries = ui_calloc_prefer_psram(next, sizeof(*entries));
    if (slots == NULL || entries == NULL) { free(slots); free(entries); return false; }
    if (s_capacity > 0) {
        memcpy(slots, s_slots, s_capacity * sizeof(*slots));
        memcpy(entries, s_entries, s_capacity * sizeof(*entries));
    }
    free(s_slots);
    free(s_entries);
    s_slots = slots;
    s_entries = entries;
    s_capacity = next;
    return true;
}

static ha_quote_series_t *scratch(void)
{
    if (s_series == NULL) s_series = ui_calloc_prefer_psram(1, sizeof(*s_series));
    return s_series;
}

static bool same_request(const feed_entry_t *a, const feed_entry_t *b)
{
    return strcmp(a->entity_id, b->entity_id) == 0 && a->provider == b->provider && a->period == b->period &&
           a->fallback == b->fallback;
}

/* Hands an answer to the slot and to every visible-or-not slot asking for the same; they count as fetched now. */
static void deliver(int slot, quote_feed_status_t status, const ha_quote_series_t *series, int64_t now_ms)
{
    const feed_entry_t asked = s_entries[slot];
    for (int i = 0; i < s_capacity; i++) {
        if (!s_slots[i].used || !s_entries[i].configured || (i != slot && !same_request(&s_entries[i], &asked))) continue;
        s_slots[i].last_try_ms = now_ms;
        if (status != QUOTE_FEED_FAILED) s_slots[i].last_ok_ms = now_ms;
        s_entries[i].cb(s_entries[i].user, status, series);
    }
}

#if defined(BETTA_SIMULATOR)

static int64_t now_ms(void)
{
    return (int64_t)time(NULL) * 1000;
}

/* The simulator has no Home Assistant: a smooth synthetic history ending at the current price, with the kind and
 * reference the real source would give, so screenshots show every label. */
static void simulate(int slot)
{
    feed_entry_t *e = &s_entries[slot];
    ha_quote_series_t *s = scratch();
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    const int64_t now = (int64_t)time(NULL);
    int64_t start = now - 86400;
    (void)ha_quote_period_start(e->period, now, &start);
    static const int points[HA_QUOTE_PERIOD_COUNT] = {288, 169, 31, 210, 260};
    const bool easy = e->provider == HA_QUOTE_PROVIDER_EASY_STOCK && e->period >= HA_QUOTE_PERIOD_30D;
    s->kind = e->period == HA_QUOTE_PERIOD_1D ? HA_QUOTE_SERIES_STATES : e->period == HA_QUOTE_PERIOD_1W ? HA_QUOTE_SERIES_HOURLY_MEANS
              : easy ? HA_QUOTE_SERIES_CLOSES : e->period == HA_QUOTE_PERIOD_30D ? HA_QUOTE_SERIES_DAILY_MEANS
              : HA_QUOTE_SERIES_WEEKLY_MEANS;
    s->period = e->period;
    s->window_start = start;
    s->window_end = now;
    const int n = s->kind == HA_QUOTE_SERIES_WEEKLY_MEANS ? 52 : points[e->period];
    unsigned seed = 0;
    for (const char *p = e->entity_id; *p; p++) seed = seed * 31U + (unsigned char)*p;
    const double price = e->price_hint > 0.0 ? e->price_hint : 100.0;
    const double drift = ((int)(seed % 7U) - 3) * 0.012 * (1.0 + (double)e->period);
    for (int i = 0; i < n; i++) {
        const double t = (double)i / (double)(n - 1);
        const double wave = 0.018 * sin(t * (5.0 + (double)(seed % 5U)) + (double)(seed % 3U));
        const double v = price * (1.0 - drift * (1.0 - t) + wave * (1.0 - t * 0.6) - wave * 0.4 * t);
        s->point[i] = (ha_quote_point_t){.t = start + (int64_t)((double)(now - start) * t), .v = (float)v};
    }
    s->point[n - 1].v = (float)price;
    s->count = (uint16_t)n;
    if (s->kind == HA_QUOTE_SERIES_CLOSES) {
        s->has_reference = true;
        s->reference = (ha_quote_point_t){.t = start - 86400, .v = s->point[0].v * 0.995f};
        s->last_provisional = true;
    }
    deliver(slot, QUOTE_FEED_READY, s, now_ms());
}

static void feed_tick(lv_timer_t *timer)
{
    (void)timer;
}

#else

#include "esp_log.h"
#include "esp_timer.h"

#include "ha/ha_easy_stock.h"
#include "ha/ha_history.h"
#include "ha/ha_history_week.h"

#define EASY_CACHE 4
#define EASY_CACHE_MS (15LL * 60LL * 1000LL)

typedef struct {
    char symbol[HA_QUOTE_SYMBOL_LEN];
    int64_t at_ms;
    ha_quote_daily_t daily;
} easy_cache_t;

static const char *TAG = "quote_feed";
static uint32_t s_generation = 0x60000000U; /* apart from the popup's and the graph feed's generations */
static easy_cache_t *s_easy;   /* EASY_CACHE entries, PSRAM */

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int slot_of(uint32_t generation)
{
    for (int i = 0; i < s_capacity; i++) {
        if (s_slots[i].used && s_entries[i].configured && s_entries[i].generation == generation && s_state.inflight == i) return i;
    }
    return -1;
}

static void on_snapshot(uint32_t generation, ha_history_snapshot_t *snapshot, esp_err_t status)
{
    const int slot = slot_of(generation);
    if (slot >= 0) {
        s_state.inflight = -1;
        ha_quote_series_t *s = scratch();
        if (status != ESP_OK || snapshot == NULL || s == NULL) deliver(slot, QUOTE_FEED_FAILED, NULL, now_ms());
        else deliver(slot, ha_quote_series_from_snapshot(snapshot, s) ? QUOTE_FEED_READY : QUOTE_FEED_EMPTY, s, now_ms());
    }
    ha_history_snapshot_free(snapshot);
}

static void on_week(uint32_t generation, ha_history_week_t *week)
{
    const int slot = slot_of(generation);
    if (slot >= 0 && week != NULL) {
        s_state.inflight = -1;
        ha_quote_series_t *s = scratch();
        if (week->status != ESP_OK || s == NULL) deliver(slot, QUOTE_FEED_FAILED, NULL, now_ms());
        else if (week->outcome != HA_STATS_OUTCOME_OK) deliver(slot, QUOTE_FEED_EMPTY, NULL, now_ms());
        else deliver(slot, ha_quote_series_from_week(&week->series, week->window.first_slot, s) ? QUOTE_FEED_READY : QUOTE_FEED_EMPTY,
                     s, now_ms());
    }
    ha_history_week_free(week);
}

static void on_range(uint32_t generation, ha_history_range_t *range)
{
    const int slot = slot_of(generation);
    if (slot >= 0 && range != NULL) {
        s_state.inflight = -1;
        ha_quote_series_t *s = scratch();
        if (range->status != ESP_OK || s == NULL) deliver(slot, QUOTE_FEED_FAILED, NULL, now_ms());
        else if (range->outcome != HA_STATS_OUTCOME_OK) deliver(slot, QUOTE_FEED_EMPTY, NULL, now_ms());
        else deliver(slot, ha_quote_series_from_range(&range->series, s_entries[slot].period, (int64_t)time(NULL), s)
                               ? QUOTE_FEED_READY : QUOTE_FEED_EMPTY, s, now_ms());
    }
    ha_history_range_free(range);
}

/* Keyed by symbol alone on purpose: Easy Stock's GET /api/easy_stock/history?symbol= answers from the symbol only (the first entry
 * with that symbol), so nothing else -- entity, currency, connection -- can make two answers differ. */
static easy_cache_t *easy_cached(const char *symbol, int64_t now)
{
    for (int i = 0; s_easy != NULL && i < EASY_CACHE; i++) {
        if (s_easy[i].at_ms != 0 && now - s_easy[i].at_ms < EASY_CACHE_MS && strcmp(s_easy[i].symbol, symbol) == 0) return &s_easy[i];
    }
    return NULL;
}

/* Every configured slot of this symbol on the daily periods gets its period from the closes. */
static void deliver_daily(const char *symbol, const ha_quote_daily_t *daily, int64_t now)
{
    ha_quote_series_t *s = scratch();
    if (s == NULL) return;
    for (int i = 0; i < s_capacity; i++) {
        feed_entry_t *e = &s_entries[i];
        if (!s_slots[i].used || !e->configured || e->fallback || e->provider != HA_QUOTE_PROVIDER_EASY_STOCK ||
            e->period < HA_QUOTE_PERIOD_30D || strcmp(e->symbol, symbol) != 0) {
            continue;
        }
        const bool ok = ha_quote_series_from_daily(daily, e->period, (int64_t)time(NULL), s);
        s_slots[i].last_try_ms = now;
        s_slots[i].last_ok_ms = now;
        e->cb(e->user, ok ? QUOTE_FEED_READY : QUOTE_FEED_EMPTY, s);
    }
}

static void on_easy(uint32_t generation, ha_easy_stock_history_t *history)
{
    const int slot = slot_of(generation);
    if (slot >= 0 && history != NULL) {
        s_state.inflight = -1;
        const int64_t now = now_ms();
        if (history->status == ESP_OK && history->outcome == HA_QUOTE_HISTORY_OK) {
            if (s_easy == NULL) s_easy = ui_calloc_prefer_psram(EASY_CACHE, sizeof(*s_easy));
            if (s_easy != NULL) {
                int oldest = 0;
                for (int i = 1; i < EASY_CACHE; i++) {
                    if (s_easy[i].at_ms < s_easy[oldest].at_ms) oldest = i;
                }
                strlcpy(s_easy[oldest].symbol, s_entries[slot].symbol, sizeof(s_easy[oldest].symbol));
                s_easy[oldest].daily = history->daily;
                s_easy[oldest].at_ms = now;
            }
            deliver_daily(s_entries[slot].symbol, &history->daily, now);
        } else if (history->status == ESP_OK && history->outcome == HA_QUOTE_HISTORY_EMPTY) {
            deliver(slot, QUOTE_FEED_EMPTY, NULL, now);
        } else {
            /* no endpoint (an older integration), another symbol or a bad reply: Home Assistant's own means instead */
            ESP_LOGW(TAG, "Easy Stock history of %s unavailable (%s, outcome %d), using statistics", s_entries[slot].symbol,
                     esp_err_to_name(history->status), (int)history->outcome);
            for (int i = 0; i < s_capacity; i++) {
                if (s_slots[i].used && strcmp(s_entries[i].symbol, s_entries[slot].symbol) == 0) {
                    s_entries[i].fallback = true;
                    s_slots[i].last_try_ms = 0; /* due now, through the other source */
                    s_slots[i].last_ok_ms = 0;
                }
            }
        }
    }
    ha_easy_stock_free(history);
}

static esp_err_t start(int slot)
{
    feed_entry_t *e = &s_entries[slot];
    e->generation = ++s_generation;
    const int64_t now = (int64_t)time(NULL);
    switch (e->period) {
    case HA_QUOTE_PERIOD_1D:
        return ha_history_request_today(e->entity_id, e->generation, on_snapshot);
    case HA_QUOTE_PERIOD_1W:
        return ha_history_request_week(e->entity_id, "", false, "", e->generation, on_week);
    default:
        break;
    }
    if (e->provider == HA_QUOTE_PROVIDER_EASY_STOCK && !e->fallback && e->symbol[0] != '\0') {
        return ha_easy_stock_request(e->symbol, e->generation, on_easy);
    }
    /* YTD within its first 30 days reads the daily means, else the weekly ones */
    int64_t year_start = now;
    (void)ha_quote_period_start(HA_QUOTE_PERIOD_YTD, now, &year_start);
    const bool days = e->period == HA_QUOTE_PERIOD_30D || (e->period == HA_QUOTE_PERIOD_YTD && now - year_start < 29LL * 86400LL);
    return ha_history_request_range(e->entity_id, days ? HA_RANGE_30_DAYS : HA_RANGE_12_MONTHS, e->generation, on_range);
}

static int pick_priority(int64_t now)
{
    for (int i = 0; i < s_capacity; i++) {
        const history_feed_slot_t *slot = &s_slots[i];
        if (slot->used && slot->visible && s_entries[i].priority && slot->last_ok_ms == 0 &&
            (slot->last_try_ms == 0 || now - slot->last_try_ms >= HISTORY_FEED_RETRY_MS)) {
            return i;
        }
    }
    return -1;
}

static void feed_tick(lv_timer_t *timer)
{
    (void)timer;
    for (int i = 0; i < s_capacity; i++) {
        s_slots[i].visible = s_slots[i].used && s_entries[i].configured && s_entries[i].obj != NULL &&
                             lv_obj_is_visible(s_entries[i].obj);
    }
    const int64_t now = now_ms();
    history_feed_decision_t decision = history_feed_decide(s_slots, s_capacity, &s_state, now);
    if (decision.timed_out >= 0) {
        ESP_LOGW(TAG, "no answer for the history of %s, asking again later", s_entries[decision.timed_out].entity_id);
        s_slots[decision.timed_out].last_try_ms = now;
        s_state.inflight = -1;
    }
    if (s_state.inflight < 0) {
        const int urgent = pick_priority(now);
        if (urgent >= 0) decision.next = urgent;
    }
    if (decision.next < 0 || s_state.inflight >= 0) return;
    feed_entry_t *e = &s_entries[decision.next];
    s_slots[decision.next].last_try_ms = now;
    /* Easy Stock closes fetched for another period or widget a moment ago serve this one too */
    if (e->provider == HA_QUOTE_PROVIDER_EASY_STOCK && !e->fallback && e->period >= HA_QUOTE_PERIOD_30D) {
        const easy_cache_t *cached = easy_cached(e->symbol, now);
        if (cached != NULL) {
            deliver_daily(e->symbol, &cached->daily, now);
            return;
        }
    }
    if (start(decision.next) == ESP_OK) {
        s_state.inflight = decision.next;
        s_state.inflight_since_ms = now;
    } else {
        ESP_LOGW(TAG, "could not request the history of %s", e->entity_id);
    }
}

#endif /* BETTA_SIMULATOR */

void *quote_history_feed_register(lv_obj_t *obj, bool priority, quote_history_feed_cb_t cb, void *user)
{
    if (obj == NULL || cb == NULL) return NULL;
    int slot = 0;
    while (slot < s_capacity && s_slots[slot].used) slot++;
    if (slot == s_capacity && !grow()) return NULL;
    if (s_timer == NULL) {
        history_feed_state_init(&s_state);
        s_timer = lv_timer_create(feed_tick, FEED_TICK_MS, NULL);
        if (s_timer == NULL) {
            free(s_slots);
            free(s_entries);
            s_slots = NULL;
            s_entries = NULL;
            s_capacity = 0;
            return NULL;
        }
    }
    {
        const int i = slot;
        memset(&s_slots[i], 0, sizeof(s_slots[i]));
        memset(&s_entries[i], 0, sizeof(s_entries[i]));
        s_slots[i].used = true;
        s_entries[i].obj = obj;
        s_entries[i].priority = priority;
        s_entries[i].cb = cb;
        s_entries[i].user = user;
        /* Index handles survive growth of the backing arrays. Zero stays the invalid handle. */
        return (void *)(uintptr_t)(i + 1);
    }
}

void quote_history_feed_set(void *handle, const char *entity_id, ha_quote_provider_t provider, const char *symbol,
                            ha_quote_period_t period, double price_hint)
{
    if (handle == NULL) return;
    const uintptr_t index = (uintptr_t)handle - 1;
    if (index >= (uintptr_t)s_capacity) return;
    const int i = (int)index;
    if (i < 0 || i >= s_capacity || !s_slots[i].used) return;
    feed_entry_t *e = &s_entries[i];
    const bool supported = entity_id != NULL && entity_id[0] != '\0' && period >= 0 && period < HA_QUOTE_PERIOD_COUNT &&
                           provider == HA_QUOTE_PROVIDER_EASY_STOCK;
    const char *sym = symbol != NULL ? symbol : "";
    const bool same = e->configured == supported && e->provider == provider && e->period == period &&
                      strcmp(e->entity_id, entity_id != NULL ? entity_id : "") == 0 && strcmp(e->symbol, sym) == 0;
    e->price_hint = price_hint;
    if (same) return;
    if (s_state.inflight == i) s_state.inflight = -1; /* its answer is for the old request: no matching generation */
    e->configured = supported;
    e->provider = provider;
    e->period = period;
    e->fallback = false;
    e->generation = 0;
    strlcpy(e->entity_id, entity_id != NULL ? entity_id : "", sizeof(e->entity_id));
    strlcpy(e->symbol, sym, sizeof(e->symbol));
    s_slots[i].last_ok_ms = 0;
    s_slots[i].last_try_ms = 0;
#if defined(BETTA_SIMULATOR)
    if (supported) simulate(i);
#endif
}

void quote_history_feed_unregister(void *handle)
{
    if (handle == NULL) return;
    const uintptr_t index = (uintptr_t)handle - 1;
    if (index >= (uintptr_t)s_capacity) return;
    const int i = (int)index;
    if (i < 0 || i >= s_capacity) return;
    if (s_state.inflight == i) s_state.inflight = -1;
    memset(&s_slots[i], 0, sizeof(s_slots[i]));
    memset(&s_entries[i], 0, sizeof(s_entries[i]));
    for (int k = 0; k < s_capacity; k++) {
        if (s_slots[k].used) return;
    }
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    free(s_slots);
    free(s_entries);
    s_slots = NULL;
    s_entries = NULL;
    s_capacity = 0;
}
