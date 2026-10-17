/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/display_orientation.h"
#include "ui/display_geometry.h"
#include "sdkconfig.h"
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
#include "app_events.h"
#include "drivers/orientation_policy.h"
#include "util/flash_gate.h"
#include "util/flash_trace.h"
#include "bsp/esp-bsp.h"
#include "bmi270.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include <stdatomic.h>

static const char *TAG = "orientation";
static atomic_bool s_enabled = true;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static display_orientation_status_t s_status = {.boot_degrees = 90, .saved_degrees = -1};
static TaskHandle_t s_task;
/* Boot owns the sensor until start(); afterwards only the orientation task uses it. */
static bmi270_handle_t *s_imu;
static int s_saved_rotation = -1;
bool display_orientation_supported(void) { return true; }
void display_orientation_configure(bool enabled) { atomic_store(&s_enabled, enabled); }
void display_orientation_get_status(display_orientation_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
    out->enabled = atomic_load(&s_enabled);
}
bool display_orientation_apply(int degrees)
{
    if (!orientation_rotation_valid(degrees)) return false;
    lv_display_set_rotation(lv_display_get_default(), (lv_display_rotation_t)(degrees / 90));
    display_geometry_set_rotation(degrees);
    ESP_LOGI(TAG, "Display rotated to %d degrees (%dx%d)", degrees, APP_VIEW_WIDTH, APP_VIEW_HEIGHT);
    return true;
}
static esp_err_t sensor_init(void)
{
    if (s_imu) return ESP_OK;
    bmi270_driver_config_t driver = {.addr = BMI270_I2C_ADDRESS_L,
        .interface = BMI270_USE_I2C, .i2c_bus = bsp_i2c_get_handle()};
    if (!driver.i2c_bus) return ESP_ERR_INVALID_STATE;
    bmi270_config_t config = {.acce_odr = BMI270_ACC_ODR_25_HZ,
        .acce_range = BMI270_ACC_RANGE_2_G, .gyro_odr = BMI270_GYR_ODR_25_HZ,
        .gyro_range = BMI270_GYR_RANGE_250_DPS};
    esp_err_t err = bmi270_create(&driver, &s_imu);
    if (err == ESP_OK) err = bmi270_start(s_imu, &config);
    if (err != ESP_OK) {
        if (s_imu) bmi270_delete(s_imu);
        s_imu = NULL;
    } else ESP_LOGI(TAG, "BMI270 automatic rotation ready");
    return err;
}
static esp_err_t sensor_read(float *x, float *y, float *z)
{
    esp_err_t err = bmi270_get_acce_data(s_imu, x, y, z);
    portENTER_CRITICAL(&s_mux);
    s_status.sensor_ready = err == ESP_OK;
    if (err == ESP_OK) { s_status.x_g = *x; s_status.y_g = *y; s_status.z_g = *z; }
    portEXIT_CRITICAL(&s_mux);
    return err;
}
static void restore_rotation(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("display_pose", NVS_READONLY, &handle);
    if (err != ESP_OK) return;
    int32_t saved;
    err = nvs_get_i32(handle, "rotation", &saved);
    nvs_close(handle);
    if (err == ESP_OK && orientation_rotation_valid(saved)) s_saved_rotation = saved;
}
int display_orientation_prepare_boot(void)
{
    restore_rotation();
    const bool enabled = atomic_load(&s_enabled);
    int rotation = orientation_boot_fallback(enabled, s_saved_rotation);
    if (enabled) {
        esp_err_t err = sensor_init();
        if (err == ESP_OK) {
            orientation_boot_policy_t policy = {.candidate = -1};
            /* 25 Hz samples, at most 500 ms after initialization; I2C reads also have a bounded timeout. */
            const int64_t deadline = esp_timer_get_time() + 500000;
            while (esp_timer_get_time() < deadline) {
                vTaskDelay(pdMS_TO_TICKS(40));
                float x, y, z;
                if (sensor_read(&x, &y, &z) != ESP_OK) break;
                if (orientation_boot_update(&policy, x, y, z, &rotation)) break;
            }
        } else ESP_LOGW(TAG, "Boot IMU unavailable (%s); using %d degrees", esp_err_to_name(err), rotation);
    }
    display_geometry_set_rotation(rotation);
    portENTER_CRITICAL(&s_mux);
    s_status.boot_degrees = rotation;
    s_status.saved_degrees = s_saved_rotation;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Boot orientation %d degrees (saved %d, automatic %d)", rotation, s_saved_rotation, enabled);
    return rotation;
}
static esp_err_t save_rotation(int rotation)
{
    /* Background NVS writes must not race OTA verification; never hold the display lock here. */
    if (!flash_gate_enter(200)) return ESP_ERR_TIMEOUT;
    FLASH_TRACE_BEGIN(trace_start);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("display_pose", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "rotation", rotation);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    FLASH_TRACE_END(trace_start, "display orientation (nvs)", sizeof(int32_t));
    flash_gate_leave();
    if (err == ESP_OK) {
        s_saved_rotation = rotation;
        portENTER_CRITICAL(&s_mux);
        s_status.saved_degrees = rotation;
        portEXIT_CRITICAL(&s_mux);
    }
    return err;
}
static void sensor_task(void *arg)
{
    (void)arg;
    orientation_policy_t policy = {.candidate = -1};
    orientation_memory_policy_t memory = {.candidate = -1};
    uint64_t next_init = 0, last_request = 0;
    while (true) {
        uint64_t now = esp_timer_get_time() / 1000;
        bool enabled = atomic_load(&s_enabled);
        int requested = 90;
        bool change = !enabled && display_geometry_rotation() != 90;
        if (enabled && !s_imu && now >= next_init) {
            esp_err_t err = sensor_init();
            if (err != ESP_OK) {
                next_init = now + 30000;
                ESP_LOGW(TAG, "IMU unavailable (%s); retrying in 30 s", esp_err_to_name(err));
            }
        }
        bool sensor_ready = false;
        if (enabled && s_imu) {
            float x, y, z;
            esp_err_t err = sensor_read(&x, &y, &z);
            sensor_ready = err == ESP_OK;
            if (err == ESP_OK)
                change = orientation_policy_update(&policy, x, y, z, now, display_geometry_rotation(), &requested);
            else policy.candidate = -1;
        } else policy.candidate = -1;
        if (change && now - last_request >= 1000) {
            app_event_t ev = {.type = EV_DISPLAY_ROTATION, .data.rotation_degrees = requested};
            app_events_publish(&ev, pdMS_TO_TICKS(20));
            last_request = now;
        }
        /* Save the applied geometry, not an event that a popup/layout error might defer or reject. */
        const int current = display_geometry_rotation();
        if (orientation_memory_should_save(&memory, enabled && sensor_ready, current, s_saved_rotation, now)) {
            esp_err_t err = save_rotation(current);
            orientation_memory_save_result(&memory, now, err == ESP_OK);
            if (err != ESP_OK) ESP_LOGW(TAG, "Could not remember orientation (%s); retrying in 30 s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
esp_err_t display_orientation_start(void)
{
    if (s_task) return ESP_OK;
    return xTaskCreate(sensor_task, "orientation", 6144, NULL, 3, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
#else
bool display_orientation_supported(void) { return false; }
void display_orientation_configure(bool enabled) { (void)enabled; }
int display_orientation_prepare_boot(void) { return 0; }
esp_err_t display_orientation_start(void) { return ESP_OK; }
void display_orientation_get_status(display_orientation_status_t *out)
{ if (out) *out = (display_orientation_status_t){0}; }
bool display_orientation_apply(int degrees) { (void)degrees; return false; }
#endif
