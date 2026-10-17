/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_tile_internal.h"

#if APP_UI_WEATHER_LOTTIE_ENABLED
typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} weather_lottie_src_t;

extern const uint8_t weather_lottie_clear_day_start[] asm("_binary_weather_clear_day_json_start");
extern const uint8_t weather_lottie_clear_day_end[] asm("_binary_weather_clear_day_json_end");
extern const uint8_t weather_lottie_clear_night_start[] asm("_binary_weather_clear_night_json_start");
extern const uint8_t weather_lottie_clear_night_end[] asm("_binary_weather_clear_night_json_end");
extern const uint8_t weather_lottie_cloudy_start[] asm("_binary_weather_cloudy_json_start");
extern const uint8_t weather_lottie_cloudy_end[] asm("_binary_weather_cloudy_json_end");
extern const uint8_t weather_lottie_fog_start[] asm("_binary_weather_fog_json_start");
extern const uint8_t weather_lottie_fog_end[] asm("_binary_weather_fog_json_end");
extern const uint8_t weather_lottie_hail_start[] asm("_binary_weather_hail_json_start");
extern const uint8_t weather_lottie_hail_end[] asm("_binary_weather_hail_json_end");
extern const uint8_t weather_lottie_partly_cloudy_day_start[] asm("_binary_weather_partly_cloudy_day_json_start");
extern const uint8_t weather_lottie_partly_cloudy_day_end[] asm("_binary_weather_partly_cloudy_day_json_end");
extern const uint8_t weather_lottie_partly_cloudy_night_start[] asm("_binary_weather_partly_cloudy_night_json_start");
extern const uint8_t weather_lottie_partly_cloudy_night_end[] asm("_binary_weather_partly_cloudy_night_json_end");
extern const uint8_t weather_lottie_sleet_start[] asm("_binary_weather_sleet_json_start");
extern const uint8_t weather_lottie_sleet_end[] asm("_binary_weather_sleet_json_end");
extern const uint8_t weather_lottie_wind_start[] asm("_binary_weather_wind_json_start");
extern const uint8_t weather_lottie_wind_end[] asm("_binary_weather_wind_json_end");
extern const uint8_t weather_lottie_overcast_day_start[] asm("_binary_weather_overcast_day_json_start");
extern const uint8_t weather_lottie_overcast_day_end[] asm("_binary_weather_overcast_day_json_end");
extern const uint8_t weather_lottie_extreme_start[] asm("_binary_weather_extreme_json_start");
extern const uint8_t weather_lottie_extreme_end[] asm("_binary_weather_extreme_json_end");
extern const uint8_t weather_lottie_extreme_rain_start[] asm("_binary_weather_extreme_rain_json_start");
extern const uint8_t weather_lottie_extreme_rain_end[] asm("_binary_weather_extreme_rain_json_end");
extern const uint8_t weather_lottie_rain_start[] asm("_binary_weather_rain_json_start");
extern const uint8_t weather_lottie_rain_end[] asm("_binary_weather_rain_json_end");
extern const uint8_t weather_lottie_snow_start[] asm("_binary_weather_snow_json_start");
extern const uint8_t weather_lottie_snow_end[] asm("_binary_weather_snow_json_end");
extern const uint8_t weather_lottie_thunderstorms_start[] asm("_binary_weather_thunderstorms_json_start");
extern const uint8_t weather_lottie_thunderstorms_end[] asm("_binary_weather_thunderstorms_json_end");
extern const uint8_t weather_lottie_thunderstorms_rain_start[] asm("_binary_weather_thunderstorms_rain_json_start");
extern const uint8_t weather_lottie_thunderstorms_rain_end[] asm("_binary_weather_thunderstorms_rain_json_end");
#endif

lv_coord_t weather_cap_lottie_size(lv_coord_t size)
{
    if (size > WEATHER_LOTTIE_MAX_SIZE) {
        return WEATHER_LOTTIE_MAX_SIZE;
    }
    return size;
}


#if APP_UI_WEATHER_LOTTIE_ENABLED
static weather_lottie_src_t weather_pick_lottie_src(const char *key)
{
    weather_lottie_src_t none = {0};
    if (key == NULL || key[0] == '\0') {
        return none;
    }

    /* Home Assistant weather conditions (exact mapping). */
    if (strcmp(key, "sunny") == 0) {
        return (weather_lottie_src_t){weather_lottie_clear_day_start, weather_lottie_clear_day_end};
    }
    if (strcmp(key, "clear-night") == 0) {
        return (weather_lottie_src_t){weather_lottie_clear_night_start, weather_lottie_clear_night_end};
    }
    if (strcmp(key, "partlycloudy") == 0) {
        return (weather_lottie_src_t){weather_lottie_partly_cloudy_day_start, weather_lottie_partly_cloudy_day_end};
    }
    if (strcmp(key, "cloudy") == 0) {
        return (weather_lottie_src_t){weather_lottie_cloudy_start, weather_lottie_cloudy_end};
    }
    if (strcmp(key, "fog") == 0) {
        return (weather_lottie_src_t){weather_lottie_fog_start, weather_lottie_fog_end};
    }
    if (strcmp(key, "hail") == 0) {
        return (weather_lottie_src_t){weather_lottie_hail_start, weather_lottie_hail_end};
    }
    if (strcmp(key, "lightning") == 0) {
        return (weather_lottie_src_t){weather_lottie_thunderstorms_start, weather_lottie_thunderstorms_end};
    }
    if (strcmp(key, "lightning-rainy") == 0) {
        return (weather_lottie_src_t){weather_lottie_thunderstorms_rain_start, weather_lottie_thunderstorms_rain_end};
    }
    if (strcmp(key, "rainy") == 0) {
        return (weather_lottie_src_t){weather_lottie_rain_start, weather_lottie_rain_end};
    }
    if (strcmp(key, "pouring") == 0) {
        return (weather_lottie_src_t){weather_lottie_extreme_rain_start, weather_lottie_extreme_rain_end};
    }
    if (strcmp(key, "snowy") == 0) {
        return (weather_lottie_src_t){weather_lottie_snow_start, weather_lottie_snow_end};
    }
    if (strcmp(key, "snowy-rainy") == 0) {
        return (weather_lottie_src_t){weather_lottie_sleet_start, weather_lottie_sleet_end};
    }
    if (strcmp(key, "windy") == 0 || strcmp(key, "windy-variant") == 0) {
        return (weather_lottie_src_t){weather_lottie_wind_start, weather_lottie_wind_end};
    }
    if (strcmp(key, "exceptional") == 0) {
        return (weather_lottie_src_t){weather_lottie_extreme_start, weather_lottie_extreme_end};
    }

    /* Common aliases from integrations/providers outside the HA core condition set. */
    if (strcmp(key, "clear") == 0) {
        return (weather_lottie_src_t){weather_lottie_clear_day_start, weather_lottie_clear_day_end};
    }
    if (strcmp(key, "partly-cloudy") == 0) {
        return (weather_lottie_src_t){weather_lottie_partly_cloudy_day_start, weather_lottie_partly_cloudy_day_end};
    }

    if (weather_has_token(key, "lightning") || weather_has_token(key, "thunder")) {
        return (weather_lottie_src_t){weather_lottie_thunderstorms_start, weather_lottie_thunderstorms_end};
    }
    if (weather_has_token(key, "snow") || weather_has_token(key, "sleet")) {
        return (weather_lottie_src_t){weather_lottie_snow_start, weather_lottie_snow_end};
    }
    if (weather_has_token(key, "rain") || weather_has_token(key, "pouring") || weather_has_token(key, "drizzle")) {
        return (weather_lottie_src_t){weather_lottie_rain_start, weather_lottie_rain_end};
    }
    if (weather_has_token(key, "night") && weather_has_token(key, "partly")) {
        return (weather_lottie_src_t){weather_lottie_partly_cloudy_night_start, weather_lottie_partly_cloudy_night_end};
    }
    if (weather_has_token(key, "partly")) {
        return (weather_lottie_src_t){weather_lottie_partly_cloudy_day_start, weather_lottie_partly_cloudy_day_end};
    }
    if (weather_has_token(key, "night")) {
        return (weather_lottie_src_t){weather_lottie_clear_night_start, weather_lottie_clear_night_end};
    }
    if (weather_has_token(key, "cloud") || weather_has_token(key, "overcast") || weather_has_token(key, "fog") ||
        weather_has_token(key, "mist") || weather_has_token(key, "haze") || weather_has_token(key, "smoke") ||
        weather_has_token(key, "wind")) {
        return (weather_lottie_src_t){weather_lottie_overcast_day_start, weather_lottie_overcast_day_end};
    }
    if (weather_has_token(key, "sunny") || weather_has_token(key, "clear")) {
        return (weather_lottie_src_t){weather_lottie_clear_day_start, weather_lottie_clear_day_end};
    }
    return none;
}

bool weather_has_lottie_for_key(const char *key)
{
    weather_lottie_src_t src = weather_pick_lottie_src(key);
    return (src.start != NULL && src.end != NULL && src.end > src.start);
}

lv_coord_t weather_pick_lottie_size(lv_obj_t *card, const w_weather_tile_ctx_t *ctx)
{
    lv_coord_t min_dim = weather_effective_min_dim(card, ctx);
    lv_coord_t size = 88;
    if (ctx != NULL && ctx->show_forecast) {
        if (min_dim >= 320) {
            size = 132;
        } else if (min_dim >= 280) {
            size = 122;
        } else if (min_dim >= 240) {
            size = 110;
        } else {
            size = 96;
        }
        return weather_cap_lottie_size(size);
    }

    if (min_dim >= 320) {
        size = 132;
    } else if (min_dim >= 280) {
        size = 118;
    } else if (min_dim >= 240) {
        size = 104;
    }
    return weather_cap_lottie_size(size);
}

static void weather_lottie_free_buffer(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL || ctx->lottie_buf == NULL) {
        return;
    }
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    if (esp_ptr_external_ram(ctx->lottie_buf)) {
        heap_caps_free(ctx->lottie_buf);
    } else {
        lv_free(ctx->lottie_buf);
    }
#else
    lv_free(ctx->lottie_buf);
#endif
    ctx->lottie_buf = NULL;
    ctx->lottie_buf_size = 0U;
}

void weather_free_lottie(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    weather_lottie_free_buffer(ctx);
    ctx->lottie_size = 0;
    ctx->last_lottie_src = NULL;
    ctx->last_lottie_src_size = 0U;
}

static bool weather_prepare_lottie_buffer(w_weather_tile_ctx_t *ctx, lv_coord_t size)
{
    if (ctx == NULL || ctx->lottie_icon == NULL || size <= 0) {
        return false;
    }

    size_t pixel_count = (size_t)size * (size_t)size;
    if (pixel_count == 0U || pixel_count > (SIZE_MAX / 4U)) {
        return false;
    }
    size_t bytes = pixel_count * 4U;
    size_t alloc_size = bytes + (size_t)LV_DRAW_BUF_ALIGN;

    if (ctx->lottie_buf != NULL && ctx->lottie_buf_size >= alloc_size && ctx->lottie_size == size) {
        return true;
    }

    if (ctx->lottie_buf != NULL) {
        weather_lottie_free_buffer(ctx);
        ctx->lottie_size = 0;
        ctx->last_lottie_src = NULL;
        ctx->last_lottie_src_size = 0U;
    }

    ctx->lottie_buf = NULL;
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    ctx->lottie_buf = heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (ctx->lottie_buf == NULL) {
        ctx->lottie_buf = lv_malloc(alloc_size);
    }
    if (ctx->lottie_buf == NULL) {
        return false;
    }

    memset(ctx->lottie_buf, 0, alloc_size);
    ctx->lottie_buf_size = alloc_size;
    ctx->lottie_size = size;
    lv_lottie_set_buffer(ctx->lottie_icon, size, size, ctx->lottie_buf);
    return true;
}

void weather_hide_lottie(w_weather_tile_ctx_t *ctx)
{
    if (ctx == NULL || ctx->lottie_icon == NULL) {
        return;
    }
    lv_obj_set_hidden(ctx->lottie_icon, true);
}

bool weather_show_lottie(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const char *condition_key, lv_coord_t icon_x,
    lv_coord_t icon_y, lv_coord_t requested_size)
{
    if (card == NULL || ctx == NULL || ctx->lottie_icon == NULL) {
        weather_hide_lottie(ctx);
        return false;
    }

    weather_lottie_src_t src = weather_pick_lottie_src(condition_key);
    if (src.start == NULL || src.end == NULL || src.end <= src.start) {
        weather_hide_lottie(ctx);
        return false;
    }


    lv_coord_t lottie_size = (requested_size > 0) ? requested_size : weather_pick_lottie_size(card, ctx);
#if defined(WEATHER_LOTTIE_MIN_SIZE)
    if (lottie_size < WEATHER_LOTTIE_MIN_SIZE) {
        /* Canvas too small: ThorVG's RLE rasterizer produces degenerate
         * spans at sub-threshold sizes on this platform – refuse cleanly. */
        weather_hide_lottie(ctx);
        return false;
    }
#endif
    if (!weather_prepare_lottie_buffer(ctx, lottie_size)) {
        weather_hide_lottie(ctx);
        return false;
    }

    size_t src_size = (size_t)(src.end - src.start);
    if (ctx->last_lottie_src != src.start || ctx->last_lottie_src_size != src_size) {
        lv_lottie_set_src_data(ctx->lottie_icon, src.start, src_size);
        ctx->last_lottie_src = src.start;
        ctx->last_lottie_src_size = src_size;
    }

    lv_obj_set_pos(ctx->lottie_icon, icon_x, icon_y);
    lv_obj_set_hidden(ctx->lottie_icon, false);
    return true;
}
#else
bool weather_has_lottie_for_key(const char *key)
{
    LV_UNUSED(key);
    return false;
}

lv_coord_t weather_pick_lottie_size(lv_obj_t *card, const w_weather_tile_ctx_t *ctx)
{
    LV_UNUSED(card);
    LV_UNUSED(ctx);
    return 0;
}

void weather_hide_lottie(w_weather_tile_ctx_t *ctx)
{
    LV_UNUSED(ctx);
}

bool weather_show_lottie(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const char *condition_key, lv_coord_t icon_x,
    lv_coord_t icon_y, lv_coord_t requested_size)
{
    LV_UNUSED(card);
    LV_UNUSED(ctx);
    LV_UNUSED(condition_key);
    LV_UNUSED(icon_x);
    LV_UNUSED(icon_y);
    LV_UNUSED(requested_size);
    return false;
}

void weather_free_lottie(w_weather_tile_ctx_t *ctx)
{
    LV_UNUSED(ctx);
}
#endif
