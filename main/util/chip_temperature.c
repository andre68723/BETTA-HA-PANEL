/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 * Cached P4 die temperature; no LVGL, network or persistent writes.
 */
#include "util/chip_temperature.h"
#include <math.h>
#include "driver/temperature_sensor.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static temperature_sensor_handle_t s_sensor;
static esp_timer_handle_t s_timer;
static chip_temperature_snapshot_t s_sample = {.error = ESP_ERR_INVALID_STATE};

static void sample(void *arg)
{
    (void)arg;
    float celsius = 0;
    esp_err_t err = temperature_sensor_get_celsius(s_sensor, &celsius);
    if (err == ESP_OK && !isfinite(celsius)) err = ESP_FAIL;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    s_sample.available = err == ESP_OK;
    s_sample.error = err;
    if (err == ESP_OK) {
        s_sample.celsius = celsius;
        s_sample.sampled_us = now;
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t chip_temperature_init(void)
{
    if (s_sensor) return ESP_ERR_INVALID_STATE;
    const temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    esp_err_t err = temperature_sensor_install(&config, &s_sensor);
    bool enabled = false;
    if (err == ESP_OK) {
        err = temperature_sensor_enable(s_sensor);
        enabled = err == ESP_OK;
    }
    if (err == ESP_OK) {
        const esp_timer_create_args_t timer = {.callback = sample, .name = "chip_temp", .skip_unhandled_events = true};
        err = esp_timer_create(&timer, &s_timer);
    }
    if (err == ESP_OK) err = esp_timer_start_periodic(s_timer, 10000000);
    if (err != ESP_OK) {
        if (s_timer) { esp_timer_delete(s_timer); s_timer = NULL; }
        if (enabled) temperature_sensor_disable(s_sensor);
        if (s_sensor) { temperature_sensor_uninstall(s_sensor); s_sensor = NULL; }
        portENTER_CRITICAL(&s_lock);
        s_sample.error = err;
        portEXIT_CRITICAL(&s_lock);
        return err;
    }
    sample(NULL);
    return ESP_OK;
}

chip_temperature_snapshot_t chip_temperature_snapshot(void)
{
    portENTER_CRITICAL(&s_lock);
    chip_temperature_snapshot_t result = s_sample;
    portEXIT_CRITICAL(&s_lock);
    return result;
}
