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

static void parse_attrs_object(const cJSON *root, parqet_attrs_t *out)
{
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
    parse_attrs_object(root, out);
    cJSON_Delete(root);
    return true;
}

bool parqet_attrs_is_portfolio(const cJSON *attrs)
{
    return cJSON_IsObject(attrs) && cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(attrs, "top_holdings")) &&
           cJSON_IsString(cJSON_GetObjectItemCaseSensitive(attrs, "portfolio_id"));
}

static void add_string_copy(cJSON *dst, const cJSON *src, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(src, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) cJSON_AddStringToObject(dst, key, item->valuestring);
}

/* Prints the identity fields, the optional generic metadata and the first `holdings` positions. NULL on allocation failure. */
static char *print_compact(const cJSON *src, const parqet_attrs_t *a, int holdings, bool metadata)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *top = cJSON_CreateArray();
    if (obj == NULL || top == NULL) {
        cJSON_Delete(obj);
        cJSON_Delete(top);
        return NULL;
    }
    cJSON_AddStringToObject(obj, "entry_id", a->entry_id);
    cJSON_AddStringToObject(obj, "portfolio_id", a->portfolio_id);
    if (a->interval[0] != '\0') cJSON_AddStringToObject(obj, "interval", a->interval);
    if (a->unit[0] != '\0') cJSON_AddStringToObject(obj, "unit_of_measurement", a->unit);
    cJSON_AddNumberToObject(obj, "holdings_count", a->holdings_count);
    if (metadata) {
        add_string_copy(obj, src, "device_class");
        add_string_copy(obj, src, "state_class");
        add_string_copy(obj, src, "friendly_name");
    }
    for (int i = 0; i < holdings; i++) {
        cJSON *h = cJSON_CreateObject();
        if (h == NULL) break;
        cJSON_AddStringToObject(h, "name", a->holding[i].name);
        cJSON_AddNumberToObject(h, "value", floor(a->holding[i].value * 100.0 + 0.5) / 100.0);
        cJSON_AddNumberToObject(h, "weight", floor(a->holding[i].weight * 100.0 + 0.5) / 100.0);
        cJSON_AddItemToArray(top, h);
    }
    cJSON_AddItemToObject(obj, "top_holdings", top);
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return json;
}

bool parqet_attrs_compact(const cJSON *src, char *out_json, size_t out_size)
{
    if (out_json == NULL || out_size == 0) return false;
    snprintf(out_json, out_size, "{}");
    if (!parqet_attrs_is_portfolio(src)) return false;
    parqet_attrs_t a;
    memset(&a, 0, sizeof(a));
    parse_attrs_object(src, &a);
    /* The widget cannot request other intervals without both ids, so a sensor without them is not worth keeping. */
    if (a.entry_id[0] == '\0' || a.portfolio_id[0] == '\0') return false;
    for (int holdings = a.count; holdings >= 0; holdings--) {
        for (int meta = 1; meta >= 0; meta--) {
            char *json = print_compact(src, &a, holdings, meta != 0);
            if (json == NULL) return false;
            const size_t len = strlen(json);
            const bool fits = len < out_size;
            if (fits) memcpy(out_json, json, len + 1U);
            cJSON_free(json);
            if (fits) return true;
        }
    }
    return false;
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
