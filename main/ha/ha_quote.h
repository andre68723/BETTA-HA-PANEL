/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Market quotes of the Stocks & ETFs widget from the Easy Stock Home Assistant integration (registry platform "easy_stock"),
 * normalized into one model. The provider comes from the entity registry (ha_display_precision.h), never from an entity ID
 * or from attributes alone: an unrelated sensor with a "symbol" attribute is not a quote. Pure logic (host tested).
 *
 * The sensor state is the price. The daily change belongs to the last reported trading session; it is only kept when it
 * and the previous close describe the price of the state. A missing change is derived from a valid previous close, never
 * assumed to be zero.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HA_QUOTE_PROVIDER_PENDING = 0, /* the registry has not answered yet */
    HA_QUOTE_PROVIDER_FAILED,      /* the registry lookup failed or timed out */
    HA_QUOTE_PROVIDER_UNSUPPORTED, /* another integration: not a quote this widget reads */
    HA_QUOTE_PROVIDER_EASY_STOCK,
} ha_quote_provider_t;

typedef enum {
    HA_QUOTE_MARKET_UNKNOWN = 0,
    HA_QUOTE_MARKET_OPEN,
    HA_QUOTE_MARKET_PRE,
    HA_QUOTE_MARKET_POST,
    HA_QUOTE_MARKET_CLOSED,
} ha_quote_market_t;

#define HA_QUOTE_CURRENCY_LEN 8
#define HA_QUOTE_SYMBOL_LEN 24
#define HA_QUOTE_NAME_LEN 64

typedef struct ha_quote {
    bool available;                     /* the state is a finite price */
    double price;
    int price_decimals;                 /* digits after the point in the state (source precision), 0 .. 18 */
    char currency[HA_QUOTE_CURRENCY_LEN]; /* "EUR", "USD", "GBp" (pence, kept as it is) or "" */
    bool has_change;
    double change;                      /* last session, in the currency of the price */
    bool has_change_pct;
    double change_pct;
    bool has_prev_close;
    double prev_close;
    char symbol[HA_QUOTE_SYMBOL_LEN];
    char name[HA_QUOTE_NAME_LEN];       /* friendly_name: the short name for a list */
    char long_name[HA_QUOTE_NAME_LEN];  /* Easy Stock long_name ("... UCITS ETF USD Acc") */
    ha_quote_market_t market;
    bool has_live;                      /* price_is_live */
    bool price_is_live;
    bool has_traded_today;              /* absent stays unknown, not false */
    bool traded_today;
} ha_quote_t;

/* Registry platform -> provider. NULL is a pending lookup, "" a failed one. */
ha_quote_provider_t ha_quote_provider_from_platform(const char *platform);

/* A complete finite number ("208.65", "-0.5", "1e3"); false for "", "unknown", trailing text, NaN, infinity or overflow. */
bool ha_quote_number(const char *text, double *out);

/* Reads state and attributes (the entity's attributes JSON object) of an Easy Stock sensor. Returns false for another
 * provider; out->available is false for an unknown, unavailable or non-numeric state. */
bool ha_quote_parse(ha_quote_provider_t provider, const char *state, const char *attributes_json, ha_quote_t *out);

/* A number with thousands separators and `decimals` digits: 13361.4 -> "13.361,40" (german) or "13,361.40"; plus adds "+"
 * to positive values. Negative zero is shown as zero. */
void ha_quote_format_number(char *out, size_t cap, double value, int decimals, bool german, bool plus);

/* The sign shown after a price: "€" for EUR, "$" for USD, otherwise the currency code ("GBp", "CHF"). */
const char *ha_quote_currency_sign(const char *currency);

/* Source precision, at least 2; up to 4 for prices >= 1, eight significant digits below 1 (max 18 decimals). */
int ha_quote_price_decimals(const ha_quote_t *quote);
