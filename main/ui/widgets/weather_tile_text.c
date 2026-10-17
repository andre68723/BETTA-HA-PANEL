/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"

void weather_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%.*s", (int)(dst_size - 1U), src);
}

void weather_normalize_condition_key(const char *src, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }

    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; i++) {
        char c = src[i];
        if (c == ' ' || c == '_') {
            c = '-';
        }
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        dst[out++] = c;
    }
    dst[out] = '\0';
}

bool weather_icon_utf8_from_codepoint(uint32_t codepoint, char out[5])
{
    if (out == NULL) {
        return false;
    }
    out[0] = '\0';

    if (codepoint <= 0x7FU) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return true;
    }
    if (codepoint <= 0x7FFU) {
        out[0] = (char)(0xC0U | ((codepoint >> 6) & 0x1FU));
        out[1] = (char)(0x80U | (codepoint & 0x3FU));
        out[2] = '\0';
        return true;
    }
    if (codepoint <= 0xFFFFU) {
        out[0] = (char)(0xE0U | ((codepoint >> 12) & 0x0FU));
        out[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (codepoint & 0x3FU));
        out[3] = '\0';
        return true;
    }

    out[0] = (char)(0xF0U | ((codepoint >> 18) & 0x07U));
    out[1] = (char)(0x80U | ((codepoint >> 12) & 0x3FU));
    out[2] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
    out[3] = (char)(0x80U | (codepoint & 0x3FU));
    out[4] = '\0';
    return true;
}


#if APP_UI_WEATHER_LOTTIE_ENABLED
bool weather_has_token(const char *key, const char *token)
{
    return (key != NULL && token != NULL && strstr(key, token) != NULL);
}
#endif

bool weather_has_alpha(const char *text)
{
    if (text == NULL) {
        return false;
    }
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (isalpha((unsigned char)text[i])) {
            return true;
        }
    }
    return false;
}

const char *weather_effective_condition_key(const w_weather_tile_ctx_t *ctx, const weather_values_t *values)
{
    if (ctx != NULL && ctx->last_condition_key[0] != '\0') {
        return ctx->last_condition_key;
    }
    if (values != NULL && values->condition_key[0] != '\0') {
        return values->condition_key;
    }
    return "";
}

/* Translates a Home Assistant weather condition ("partlycloudy",
 * "clear-night", ...) through the "weather.condition.<key with _>" i18n
 * entries. Returns false if the language has no entry for it. */
static bool weather_translate_condition(const char *src, char *dst, size_t dst_size)
{
    char key[32];
    weather_normalize_condition_key(src, key, sizeof(key));
    if (key[0] == '\0') {
        return false;
    }
    /* Provider spellings that map onto one of Home Assistant's conditions. */
    if (strcmp(key, "partly-cloudy") == 0) {
        weather_copy_text(key, sizeof(key), "partlycloudy");
    } else if (strcmp(key, "clear-day") == 0) {
        weather_copy_text(key, sizeof(key), "sunny");
    }
    char path[64] = "weather.condition.";
    size_t n = strlen(path);
    for (size_t i = 0; key[i] != '\0' && n + 1 < sizeof(path); i++) {
        path[n++] = (key[i] == '-') ? '_' : key[i];
    }
    path[n] = '\0';
    const char *text = ui_i18n_get(path, "");
    if (text[0] == '\0') {
        return false;
    }
    weather_copy_text(dst, dst_size, text);
    return true;
}

void weather_humanize_condition(const char *src, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL || src[0] == '\0') {
        weather_copy_text(dst, dst_size, "--");
        return;
    }
    if (weather_translate_condition(src, dst, dst_size)) {
        return;
    }
    /* No translation: show Home Assistant's key, capitalized. */

    size_t out = 0;
    bool uppercase_next = true;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dst_size; i++) {
        char c = src[i];
        if (c == '_' || c == '-') {
            c = ' ';
        }
        if (uppercase_next && c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        dst[out++] = c;
        uppercase_next = (c == ' ');
    }
    dst[out] = '\0';
}

void weather_update_icon_cache_from_state(w_weather_tile_ctx_t *ctx, const char *state_text)
{
    if (ctx == NULL || state_text == NULL || state_text[0] == '\0') {
        return;
    }

    char key[32] = {0};
    char human[32] = {0};
    weather_normalize_condition_key(state_text, key, sizeof(key));
    uint32_t cp = weather_icon_codepoint_for_key(key);
    if (cp == 0U) {
        return;
    }

    weather_copy_text(ctx->last_condition_key, sizeof(ctx->last_condition_key), key);
    weather_humanize_condition(state_text, human, sizeof(human));
    ctx->last_icon_cp = cp;
    weather_copy_text(ctx->last_condition_text, sizeof(ctx->last_condition_text), human);
}

static bool weather_parse_ymd(const char *datetime, int *year, int *month, int *day)
{
    if (datetime == NULL || strlen(datetime) < 10 || datetime[4] != '-' || datetime[7] != '-') {
        return false;
    }

    for (size_t i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!isdigit((unsigned char)datetime[i])) {
            return false;
        }
    }

    char local_date[11]; ui_day_grid_local_date(datetime, local_date); datetime = local_date;
    int y = (datetime[0] - '0') * 1000 + (datetime[1] - '0') * 100 + (datetime[2] - '0') * 10 + (datetime[3] - '0');
    int m = (datetime[5] - '0') * 10 + (datetime[6] - '0');
    int d = (datetime[8] - '0') * 10 + (datetime[9] - '0');
    if (m < 1 || m > 12 || d < 1 || d > 31) {
        return false;
    }

    if (year != NULL) {
        *year = y;
    }
    if (month != NULL) {
        *month = m;
    }
    if (day != NULL) {
        *day = d;
    }
    return true;
}

static int weather_weekday_from_ymd(int year, int month, int day)
{
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year -= 1;
    }
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

void weather_day_from_datetime(const char *datetime, char *out, size_t out_size)
{
    static const char *weekday_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (out == NULL || out_size == 0) {
        return;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (!weather_parse_ymd(datetime, &year, &month, &day)) {
        weather_copy_text(out, out_size, "--");
        return;
    }

    int weekday = weather_weekday_from_ymd(year, month, day);
    if (weekday < 0 || weekday > 6) {
        weather_copy_text(out, out_size, "--");
        return;
    }
    char path[24];
    snprintf(path, sizeof(path), "weather.weekday_%d", weekday);
    weather_copy_text(out, out_size, ui_i18n_get(path, weekday_names[weekday]));
}

bool weather_datetime_is_today(const char *datetime)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!weather_parse_ymd(datetime, &year, &month, &day)) {
        return false;
    }

    time_t now = time(NULL);
    struct tm local_now = {0};
    localtime_r(&now, &local_now);

    return (year == (local_now.tm_year + 1900)) && (month == (local_now.tm_mon + 1)) && (day == local_now.tm_mday);
}

bool weather_datetime_is_before_today(const char *datetime)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (!weather_parse_ymd(datetime, &year, &month, &day)) {
        return false;
    }

    time_t now = time(NULL);
    struct tm local_now = {0};
    localtime_r(&now, &local_now);

    int date_key = year * 10000 + month * 100 + day;
    int today_key = (local_now.tm_year + 1900) * 10000 + (local_now.tm_mon + 1) * 100 + local_now.tm_mday;
    return date_key < today_key;
}
