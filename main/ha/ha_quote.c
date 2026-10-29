/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_quote.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* Two prices are the same when they agree to this relative difference (rounding of the state); a change and a previous
 * close that add up to another price belong to another session or currency. */
#define QUOTE_SAME_PRICE 0.0005

ha_quote_provider_t ha_quote_provider_from_platform(const char *platform)
{
    if (platform == NULL) return HA_QUOTE_PROVIDER_PENDING;
    if (platform[0] == '\0') return HA_QUOTE_PROVIDER_FAILED;
    if (strcmp(platform, "easy_stock") == 0) return HA_QUOTE_PROVIDER_EASY_STOCK;
    return HA_QUOTE_PROVIDER_UNSUPPORTED;
}

bool ha_quote_number(const char *text, double *out)
{
    if (text == NULL || text[0] == '\0' || text[0] == ' ') return false;
    errno = 0;
    char *end = NULL;
    const double value = strtod(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || !isfinite(value)) return false;
    *out = value;
    return true;
}

static bool quote_json_number(const cJSON *item, double *out)
{
    if (cJSON_IsNumber(item)) {
        if (!isfinite(item->valuedouble)) return false;
        *out = item->valuedouble;
        return true;
    }
    return cJSON_IsString(item) && ha_quote_number(item->valuestring, out);
}

/* Copies the attribute when out is still empty; a cut text ends at a character, not inside one. */
static void quote_json_string(const cJSON *attrs, const char *key, char *out, size_t cap)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(attrs, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL || out[0] != '\0') return;
    if ((size_t)snprintf(out, cap, "%s", item->valuestring) < cap) return;
    const size_t len = cap - 1;
    size_t start = len - 1;
    while (start > 0 && ((unsigned char)out[start] & 0xC0) == 0x80) start--; /* the last character's first byte */
    const unsigned char lead = (unsigned char)out[start];
    const size_t need = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
    if (start + need > len) out[start] = '\0';
}

/* "SAP SE                        I" (a name padded to a fixed width) -> "SAP SE I" */
static void quote_collapse_spaces(char *text)
{
    size_t n = 0;
    bool space = true; /* drops leading spaces */
    for (const char *p = text; *p != '\0'; p++) {
        const bool blank = *p == ' ' || *p == '\t';
        if (blank && space) continue;
        text[n++] = blank ? ' ' : *p;
        space = blank;
    }
    while (n > 0 && text[n - 1] == ' ') n--;
    text[n] = '\0';
}

static int quote_state_decimals(const char *state)
{
    const char *p = state;
    while (*p && *p != '.' && *p != 'e' && *p != 'E') p++;
    int n = 0;
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9'; p++) n++;
    }
    if (*p == 'e' || *p == 'E') {
        const long exponent = strtol(p + 1, NULL, 10);
        if (exponent < -18) return 18;
        if (exponent > n) return 0;
        n -= (int)exponent;
    }
    return n > 18 ? 18 : n;
}

static ha_quote_market_t quote_market(const char *state)
{
    if (state == NULL) return HA_QUOTE_MARKET_UNKNOWN;
    if (strcmp(state, "REGULAR") == 0 || strcmp(state, "OPEN") == 0) return HA_QUOTE_MARKET_OPEN;
    if (strcmp(state, "PRE") == 0 || strcmp(state, "PREPRE") == 0) return HA_QUOTE_MARKET_PRE;
    if (strcmp(state, "POST") == 0 || strcmp(state, "POSTPOST") == 0) return HA_QUOTE_MARKET_POST;
    if (strcmp(state, "CLOSED") == 0) return HA_QUOTE_MARKET_CLOSED;
    return HA_QUOTE_MARKET_UNKNOWN;
}

static bool quote_same_price(double a, double b)
{
    const double scale = fmax(fabs(a), fabs(b));
    return scale == 0.0 || fabs(a - b) <= scale * QUOTE_SAME_PRICE;
}

bool ha_quote_parse(ha_quote_provider_t provider, const char *state, const char *attributes_json, ha_quote_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (provider != HA_QUOTE_PROVIDER_EASY_STOCK) return false;
    out->available = state != NULL && ha_quote_number(state, &out->price);
    if (out->available) out->price_decimals = quote_state_decimals(state);

    cJSON *attrs = attributes_json != NULL ? cJSON_Parse(attributes_json) : NULL;
    if (!cJSON_IsObject(attrs)) {
        cJSON_Delete(attrs);
        return true;
    }
    quote_json_string(attrs, "symbol", out->symbol, sizeof(out->symbol));
    quote_json_string(attrs, "long_name", out->long_name, sizeof(out->long_name));
    quote_json_string(attrs, "friendly_name", out->name, sizeof(out->name));
    quote_collapse_spaces(out->name);
    quote_collapse_spaces(out->long_name);
    /* The currency is named; the unit is the fallback. */
    quote_json_string(attrs, "currency", out->currency, sizeof(out->currency));
    quote_json_string(attrs, "unit_of_measurement", out->currency, sizeof(out->currency));

    const cJSON *market = cJSON_GetObjectItemCaseSensitive(attrs, "market_state");
    out->market = quote_market(cJSON_IsString(market) ? market->valuestring : NULL);

    double change = 0.0, pct = 0.0, prev = 0.0;
    const bool has_change = quote_json_number(cJSON_GetObjectItemCaseSensitive(attrs, "change"), &change);
    const bool has_pct = quote_json_number(cJSON_GetObjectItemCaseSensitive(attrs, "change_pct"), &pct);
    const bool has_prev = quote_json_number(
        cJSON_GetObjectItemCaseSensitive(attrs, "previous_close"), &prev) && prev > 0.0;

    if (out->available) {
        out->has_prev_close = has_prev;
        out->prev_close = prev;
        if (has_change) {
            out->has_change = true;
            out->change = change;
        } else if (has_prev) {
            out->has_change = true;
            out->change = out->price - prev;
        }
        if (has_pct) {
            out->has_change_pct = true;
            out->change_pct = pct;
        } else if (out->has_change && has_prev) {
            out->has_change_pct = true;
            out->change_pct = out->change / prev * 100.0;
        }
        /* A change and a previous close that do not describe this price belong to another session or currency. */
        if (has_change && has_prev && !quote_same_price(prev + change, out->price)) {
            out->has_change = out->has_change_pct = out->has_prev_close = false;
        }
    }

    const cJSON *traded = cJSON_GetObjectItemCaseSensitive(attrs, "traded_today");
    out->has_traded_today = cJSON_IsBool(traded);
    out->traded_today = cJSON_IsTrue(traded);
    const cJSON *live = cJSON_GetObjectItemCaseSensitive(attrs, "price_is_live");
    if (cJSON_IsBool(live)) {
        out->has_live = true;
        out->price_is_live = cJSON_IsTrue(live);
    }
    cJSON_Delete(attrs);
    return true;
}

void ha_quote_format_number(char *out, size_t cap, double value, int decimals, bool german, bool plus)
{
    if (out == NULL || cap == 0) return;
    if (decimals < 0) decimals = 0;
    if (decimals > 18) decimals = 18;
    if ((decimals == 18 && value != 0.0 && fabs(value) < 0.5e-18) || fabs(value) >= 1e15) {
        snprintf(out, cap, plus && value > 0.0 ? "+%.8g" : "%.8g", value);
        if (german) {
            char *dot = strchr(out, '.');
            if (dot != NULL) *dot = ',';
        }
        return;
    }
    char digits[48];
    snprintf(digits, sizeof(digits), "%.*f", decimals, fabs(value));
    /* -0.004 at two digits is zero, not "-0.00" */
    const bool zero = strspn(digits, "0.") == strlen(digits);
    const char *sign = zero ? "" : value < 0.0 ? "-" : plus ? "+" : "";
    const char thousands = german ? '.' : ',', point = german ? ',' : '.';
    const char *dot = strchr(digits, '.');
    const size_t int_len = dot != NULL ? (size_t)(dot - digits) : strlen(digits);
    size_t n = (size_t)snprintf(out, cap, "%s", sign);
    for (size_t i = 0; i < int_len && n + 1 < cap; i++) {
        if (i > 0 && (int_len - i) % 3 == 0 && n + 2 < cap) out[n++] = thousands;
        out[n++] = digits[i];
    }
    if (dot != NULL && n + 1 < cap) {
        out[n++] = point;
        for (const char *p = dot + 1; *p != '\0' && n + 1 < cap; p++) out[n++] = *p;
    }
    out[n < cap ? n : cap - 1] = '\0';
}

const char *ha_quote_currency_sign(const char *currency)
{
    if (currency == NULL) return "";
    if (strcmp(currency, "EUR") == 0) return "\xE2\x82\xAC";
    if (strcmp(currency, "USD") == 0) return "$";
    return currency;
}

int ha_quote_price_decimals(const ha_quote_t *quote)
{
    if (quote == NULL) return 2;
    int cap = 4;
    const double price = fabs(quote->price);
    if (price > 0.0 && price < 1.0) {
        cap = (int)fmin(18.0, 7.0 - floor(log10(price)));
    }
    const int digits = quote->price_decimals;
    return digits < 2 ? 2 : digits > cap ? cap : digits;
}
