/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/parqet_portfolio.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* Copies at most cap-1 bytes and never ends inside a UTF-8 sequence. */
static void copy_utf8(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--; /* src[n] is a continuation byte: step back to the lead byte */
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static double json_number(const cJSON *object, const char *key, bool *ok)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        *ok = false;
        return 0.0;
    }
    return item->valuedouble;
}

bool parqet_attrs_parse(const char *attributes_json, parqet_attrs_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (attributes_json == NULL || attributes_json[0] == '\0') return false;
    cJSON *root = cJSON_Parse(attributes_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *interval = cJSON_GetObjectItemCaseSensitive(root, "interval");
    if (cJSON_IsString(interval) && interval->valuestring != NULL) copy_utf8(out->interval, sizeof(out->interval), interval->valuestring);
    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(root, "unit_of_measurement");
    if (cJSON_IsString(unit) && unit->valuestring != NULL) copy_utf8(out->unit, sizeof(out->unit), unit->valuestring);
    const cJSON *entry = cJSON_GetObjectItemCaseSensitive(root, "entry_id");
    if (cJSON_IsString(entry) && entry->valuestring != NULL) copy_utf8(out->entry_id, sizeof(out->entry_id), entry->valuestring);
    const cJSON *portfolio = cJSON_GetObjectItemCaseSensitive(root, "portfolio_id");
    if (cJSON_IsString(portfolio) && portfolio->valuestring != NULL) copy_utf8(out->portfolio_id, sizeof(out->portfolio_id), portfolio->valuestring);
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "holdings_count");
    if (cJSON_IsNumber(count) && count->valuedouble >= 0 && count->valuedouble < 100000) out->holdings_count = (int)count->valuedouble;

    const cJSON *top = cJSON_GetObjectItemCaseSensitive(root, "top_holdings");
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, top) {
        if (out->count >= PARQET_MAX_HOLDINGS) break;
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        bool ok = true;
        const double value = json_number(item, "value", &ok);
        const double weight = json_number(item, "weight", &ok);
        if (!ok || !cJSON_IsString(name) || name->valuestring == NULL || name->valuestring[0] == '\0' || weight < 0.0) continue;
        parqet_holding_t *h = &out->holding[out->count++];
        copy_utf8(h->name, sizeof(h->name), name->valuestring);
        h->value = value;
        h->weight = weight;
    }
    cJSON_Delete(root);
    return true;
}

bool parqet_number(const char *state, double *out)
{
    if (state == NULL || state[0] == '\0' || out == NULL) return false;
    char *end = NULL;
    const double v = strtod(state, &end);
    if (end == state || *end != '\0' || !isfinite(v)) return false;
    *out = v;
    return true;
}

void parqet_interval_label(const char *interval, char *out, size_t cap)
{
    if (out == NULL || cap == 0) return;
    out[0] = '\0';
    if (interval == NULL || interval[0] == '\0') return;
    if (strcmp(interval, "max") == 0) {
        copy_utf8(out, cap, "Max");
        return;
    }
    size_t i = 0;
    for (; interval[i] != '\0' && i + 1 < cap; i++) out[i] = (char)toupper((unsigned char)interval[i]);
    out[i] = '\0';
}

void parqet_format_number(char *out, size_t cap, double value, int decimals, bool german, bool plus)
{
    if (out == NULL || cap == 0) return;
    out[0] = '\0';
    if (!isfinite(value)) return;
    if (decimals < 0) decimals = 0;
    if (decimals > 4) decimals = 4;
    char plain[48];
    snprintf(plain, sizeof(plain), "%.*f", decimals, fabs(value));
    const char *dot = strchr(plain, '.');
    const size_t int_len = dot != NULL ? (size_t)(dot - plain) : strlen(plain);
    const bool zero = strspn(plain, "0.") == strlen(plain); /* -0.001 rounds to 0: no sign */
    char text[64];
    size_t n = 0;
    if (!zero && value < 0.0) {
        n += (size_t)snprintf(text + n, sizeof(text) - n, "\xE2\x88\x92");
    } else if (!zero && plus) {
        text[n++] = '+';
    }
    for (size_t i = 0; i < int_len && n + 4 < sizeof(text); i++) {
        if (i > 0 && (int_len - i) % 3 == 0) text[n++] = german ? '.' : ',';
        text[n++] = plain[i];
    }
    if (dot != NULL) {
        text[n++] = german ? ',' : '.';
        for (const char *d = dot + 1; *d != '\0' && n + 1 < sizeof(text); d++) text[n++] = *d;
    }
    text[n] = '\0';
    copy_utf8(out, cap, text);
}

const char *parqet_currency_symbol(const char *unit)
{
    if (unit == NULL) return "";
    if (strcmp(unit, "EUR") == 0) return "\xE2\x82\xAC";
    if (strcmp(unit, "USD") == 0) return "$";
    if (strcmp(unit, "GBP") == 0) return "\xC2\xA3";
    return unit;
}
