/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/parqet_performance.h"

#include <math.h>
#include <string.h>

static double num(const cJSON *object, const char *key, bool *ok)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        if (ok != NULL) *ok = false;
        return 0.0;
    }
    return item->valuedouble;
}

/* object.a.b.c as a number; false (and 0) when a part is missing */
static bool path3(const cJSON *root, const char *a, const char *b, const char *c, double *out)
{
    const cJSON *x = cJSON_GetObjectItemCaseSensitive(root, a);
    x = cJSON_GetObjectItemCaseSensitive(x, b);
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(x, c);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        *out = 0.0;
        return false;
    }
    *out = item->valuedouble;
    return true;
}

static void copy_name(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

typedef enum { RANK_VALUE, RANK_BEST, RANK_WORST } rank_t;

static double rank_key(const parqet_perf_holding_t *h, rank_t rank)
{
    return rank == RANK_VALUE ? h->value : rank == RANK_BEST ? h->ret : -h->ret; /* larger is better in every ranking */
}

/* Inserts `h` into a list ordered by `rank`, best first, keeping PARQET_PERF_RANK entries. */
static void rank_holding(parqet_perf_holding_t *list, int *count, const parqet_perf_holding_t *h, rank_t rank)
{
    int at = *count;
    if (at == PARQET_PERF_RANK) {
        if (rank_key(h, rank) <= rank_key(&list[PARQET_PERF_RANK - 1], rank)) return;
        at = PARQET_PERF_RANK - 1; /* replaces the weakest kept */
    } else {
        (*count)++;
    }
    while (at > 0 && rank_key(&list[at - 1], rank) < rank_key(h, rank)) {
        list[at] = list[at - 1];
        at--;
    }
    list[at] = *h;
}

bool parqet_perf_parse(const cJSON *result, const char *interval, parqet_perf_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(result)) return false;
    if (interval != NULL) copy_name(out->interval, sizeof(out->interval), interval);
    const cJSON *perf = cJSON_GetObjectItemCaseSensitive(result, "performance");
    if (!cJSON_IsObject(perf)) return false;

    /* the portfolio's own figures: the valuation is required, the rest is zero when absent */
    const cJSON *valuation = cJSON_GetObjectItemCaseSensitive(perf, "valuation");
    bool ok = true;
    out->value_start = num(valuation, "atIntervalStart", &ok);
    out->value_end = num(valuation, "atIntervalEnd", &ok);
    if (!ok) return false;
    path3(perf, "unrealizedGains", "inInterval", "gainGross", &out->unrealized_gain);
    path3(perf, "unrealizedGains", "inInterval", "returnGross", &out->unrealized_return);
    path3(perf, "realizedGains", "inInterval", "gainGross", &out->realized_gain);
    path3(perf, "dividends", "inInterval", "gainGross", &out->dividends);
    path3(perf, "taxes", "inInterval", "taxes", &out->taxes);
    path3(perf, "fees", "inInterval", "fees", &out->fees);
    path3(perf, "kpis", "inInterval", "xirr", &out->xirr);
    path3(perf, "kpis", "inInterval", "ttwror", &out->ttwror);

    /* the securities that are still held: the largest by value, the best and the worst by their return in the interval */
    int eligible = 0;
    const cJSON *holding = NULL;
    cJSON_ArrayForEach(holding, cJSON_GetObjectItemCaseSensitive(result, "holdings")) {
        const cJSON *position = cJSON_GetObjectItemCaseSensitive(holding, "position");
        const cJSON *asset = cJSON_GetObjectItemCaseSensitive(holding, "asset");
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(asset, "type");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        const cJSON *sold = cJSON_GetObjectItemCaseSensitive(position, "isSold");
        if (cJSON_IsTrue(sold) || (cJSON_IsString(type) && type->valuestring != NULL && strcmp(type->valuestring, "cash") == 0)) continue;
        if (!cJSON_IsString(name) || name->valuestring == NULL || name->valuestring[0] == '\0') {
            name = cJSON_GetObjectItemCaseSensitive(holding, "nickname");
        }
        bool value_ok = true;
        const double value = num(position, "currentValue", &value_ok);
        if (!value_ok || value <= 0.0 || !cJSON_IsString(name) || name->valuestring == NULL || name->valuestring[0] == '\0') continue;
        parqet_perf_holding_t h = {.value = value};
        copy_name(h.name, sizeof(h.name), name->valuestring);
        h.weight = out->value_end > 0.0 ? value / out->value_end * 100.0 : 0.0;
        const cJSON *performance = cJSON_GetObjectItemCaseSensitive(holding, "performance");
        path3(performance, "unrealizedGains", "inInterval", "gainGross", &h.gain);
        h.has_return = path3(performance, "unrealizedGains", "inInterval", "returnGross", &h.ret);
        rank_holding(out->largest, &out->largest_count, &h, RANK_VALUE);
        if (!h.has_return) continue;
        eligible++;
        rank_holding(out->top, &out->top_count, &h, RANK_BEST);
        rank_holding(out->flop, &out->flop_count, &h, RANK_WORST);
    }
    /* with fewer than two full rankings the lists would show the same holding twice: the worst ones are what is left after the best */
    if (out->flop_count > eligible - out->top_count) out->flop_count = eligible - out->top_count > 0 ? eligible - out->top_count : 0;

    /* the value curve, thinned to at most PARQET_PERF_POINTS evenly spaced entries (first and last kept) */
    const cJSON *charts = cJSON_GetObjectItemCaseSensitive(result, "charts");
    int total = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, charts) {
        const cJSON *history = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(entry, "values"), "history");
        if (cJSON_IsNumber(history) && isfinite(history->valuedouble)) total++;
    }
    if (total > 0) {
        const int keep = total < PARQET_PERF_POINTS ? total : PARQET_PERF_POINTS;
        int index = 0;
        int slot = 0;
        cJSON_ArrayForEach(entry, charts) {
            const cJSON *history = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(entry, "values"), "history");
            if (!cJSON_IsNumber(history) || !isfinite(history->valuedouble)) continue;
            /* slot `slot` takes the entry at index slot * (total - 1) / (keep - 1): the first and the last entry stay, the others are spread evenly */
            const int target = keep > 1 ? slot * (total - 1) / (keep - 1) : 0;
            if (index == target && slot < keep) {
                out->point[slot++] = (float)history->valuedouble;
            }
            index++;
        }
        out->point_count = slot;
    }
    return true;
}
