/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/api_routes.h"

#include "cJSON.h"
#include "esp_app_desc.h"

#include "build_stamp.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "ui/display_geometry.h"
#include "ui/text_font.h"
#include "ui/text_scale.h"
#include "drivers/display_orientation.h"
#include "drivers/ambient_camera.h"
#include "drivers/display_init.h"
#include "drivers/display_backlight.h"
#include "net/wifi_diag_store.h"
#include "net/wifi_mgr.h"
#include "settings/night_mode.h"
#include "ui/ui_display_protection.h"
#include "util/boot_guard.h"
#include "util/coredump_archive.h"
#include "ui/lvgl_alloc.h"
#include "ui/nav_layout.h"
#include "util/ota_health.h"
#include "util/ota_validate.h"
#include "esp_bootloader_desc.h"
#include "util/reboot_log_esp.h"
#include "esp_chip_info.h"
#include "util/net_watchdog_esp.h"
#include "util/ota_chip_rev.h"
#include "util/ui_watchdog_esp.h"
#include "component_versions.h"
#include "ha/ha_auth_state.h"

/* Add a "<key>": "<value>" field only when value is non-empty - components
 * absent from this variant's dependencies.lock (e.g. esp_lcd_jd9365 on
 * panel10) come back as "" from the generated header and are omitted
 * rather than reported misleadingly as an empty string. */
static void add_version_if_present(cJSON *obj, const char *key, const char *value)
{
    if (value != NULL && value[0] != '\0') {
        cJSON_AddStringToObject(obj, key, value);
    }
}

/* CPU load per core from the run time of the two idle tasks between two calls (needs the FreeRTOS
 * run time stats). Calls closer than 500 ms apart return the previous figure. */
#define CPU_CORES 2
static int s_cpu_load[CPU_CORES] = {-1, -1};

static void sample_cpu_load(void)
{
#if configGENERATE_RUN_TIME_STATS && configUSE_TRACE_FACILITY
    static uint32_t s_prev_idle[CPU_CORES];
    static uint32_t s_prev_total;
    static int64_t s_prev_us;
    const int64_t now_us = esp_timer_get_time();
    if (s_prev_us != 0 && now_us - s_prev_us < 500000) {
        return;
    }
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *tasks = malloc(n * sizeof(TaskStatus_t));
    if (tasks == NULL) {
        return;
    }
    uint32_t total = 0;
    n = uxTaskGetSystemState(tasks, n, &total);
    uint32_t idle[CPU_CORES] = {0, 0};
    for (UBaseType_t i = 0; i < n; i++) {
        if (strncmp(tasks[i].pcTaskName, "IDLE", 4) == 0) {
            int core = tasks[i].pcTaskName[4] - '0';
            if (core >= 0 && core < CPU_CORES) {
                idle[core] = tasks[i].ulRunTimeCounter;
            }
        }
    }
    free(tasks);
    if (s_prev_us != 0 && total > s_prev_total) {
        const uint32_t elapsed = total - s_prev_total;
        for (int c = 0; c < CPU_CORES; c++) {
            uint32_t idle_delta = idle[c] - s_prev_idle[c];
            int busy = 100 - (int)((uint64_t)idle_delta * 100 / elapsed);
            s_cpu_load[c] = busy < 0 ? 0 : (busy > 100 ? 100 : busy);
        }
    }
    memcpy(s_prev_idle, idle, sizeof(idle));
    s_prev_total = total;
    s_prev_us = now_us;
#endif
}

/* Why the panel last (re)started: tells a crash or watchdog from a power cycle or a requested restart. */
static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "power_glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
    default: return "unknown";
    }
}

static void api_version_add_wifi_diag(cJSON *obj, const wifi_mgr_diag_t *diag)
{
    if (obj == NULL || diag == NULL) {
        return;
    }
    cJSON_AddNumberToObject(obj, "disconnect_count", (double)diag->disconnect_count);
    cJSON_AddNumberToObject(obj, "last_disconnect_reason", (double)diag->last_disconnect_reason);
    cJSON_AddNumberToObject(obj, "last_disconnect_uptime_ms", (double)diag->last_disconnect_uptime_ms);
    cJSON_AddNumberToObject(obj, "reconnect_attempts", (double)diag->reconnect_attempts);
    cJSON_AddNumberToObject(obj, "hard_recover_count", (double)diag->hard_recover_count);
    cJSON_AddNumberToObject(obj, "last_hard_recover_err", (double)diag->last_hard_recover_err);
    cJSON_AddNumberToObject(obj, "last_hard_recover_uptime_ms", (double)diag->last_hard_recover_uptime_ms);
}

static void set_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

/* One entry per FreeRTOS task with its stack: where it lives (internal RAM or PSRAM), its allocated size and how
 * little of it was ever free (bytes). Read with GET /api/version?tasks=1 to size stacks from measurements. */
static void add_task_stacks(cJSON *root)
{
    const UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4U;
    TaskStatus_t *tasks = heap_caps_calloc(capacity, sizeof(*tasks), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    cJSON *list = cJSON_AddArrayToObject(root, "tasks");
    if (tasks == NULL || list == NULL) {
        heap_caps_free(tasks);
        return;
    }
    const UBaseType_t count = uxTaskGetSystemState(tasks, capacity, NULL);
    for (UBaseType_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            break;
        }
        cJSON_AddStringToObject(item, "name", tasks[i].pcTaskName);
        cJSON_AddNumberToObject(item, "priority", (double)tasks[i].uxCurrentPriority);
        cJSON_AddBoolToObject(item, "stack_internal", esp_ptr_internal(tasks[i].pxStackBase));
        cJSON_AddNumberToObject(item, "stack_size", (double)heap_caps_get_allocated_size(tasks[i].pxStackBase));
        cJSON_AddNumberToObject(item, "stack_free_min", (double)tasks[i].usStackHighWaterMark);
        cJSON_AddItemToArray(list, item);
    }
    heap_caps_free(tasks);
}

esp_err_t api_version_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    const char *version = (desc != NULL && desc->version[0] != '\0') ? desc->version : "unknown";
    const char *project_name = (desc != NULL && desc->project_name[0] != '\0') ? desc->project_name : APP_NAME;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "name", APP_NAME);
    cJSON_AddStringToObject(root, "project", project_name);
    cJSON_AddStringToObject(root, "version", version);
    cJSON_AddStringToObject(root, "board", APP_BOARD_DESCRIPTION);
    /* Panel geometry for the web editor canvas (px) - lets the same
     * the editor scale itself for both the 4" 720x720 and 10.1" 1280x800
     * variants without a separate build. */
    cJSON_AddNumberToObject(root, "screen_w", APP_VIEW_WIDTH);
    cJSON_AddNumberToObject(root, "screen_h", APP_VIEW_HEIGHT);
    cJSON_AddNumberToObject(root, "canvas_w", APP_VIEW_CONTENT_WIDTH);
    cJSON_AddNumberToObject(root, "canvas_h", APP_VIEW_CONTENT_HEIGHT);
    /* Font manager counters (ui/text_font.h): live instances of UI_FONT_MAX_INSTANCES, requests answered by a substitute because the
     * budget was reached, and creations that failed. Non-zero hits or failures mean a text was drawn in a nearby size. */
    {
        const ui_fonts_stats_t stats = ui_fonts_stats();
        cJSON *fonts = cJSON_AddObjectToObject(root, "fonts");
        if (fonts != NULL) {
            cJSON_AddNumberToObject(fonts, "instances", stats.instances);
            cJSON_AddNumberToObject(fonts, "max_instances", UI_FONT_MAX_INSTANCES);
            cJSON_AddNumberToObject(fonts, "budget_hits", stats.budget_hits);
            cJSON_AddNumberToObject(fonts, "create_failures", stats.create_failures);
        }
    }
    /* Text sizes of this board (ui/text_scale.h): px per role (meta, label, value, key, hero) for the user steps 0..2, so the editor
     * previews the panel's sizes without a table of its own. "step" is the step in use now. */
    {
        static const char *const role_names[] = {"meta", "label", "value", "key", "hero"};
        cJSON *text = cJSON_AddObjectToObject(root, "text");
        cJSON *roles = text != NULL ? cJSON_AddArrayToObject(text, "roles") : NULL;
        cJSON *sizes = text != NULL ? cJSON_AddArrayToObject(text, "sizes") : NULL;
        if (text != NULL && roles != NULL && sizes != NULL) {
            const text_board_t board = text_scale_build_board();
            cJSON_AddNumberToObject(text, "step", ui_fonts_user_step());
            cJSON_AddNumberToObject(text, "floor", text_scale_floor_px(board));
            for (size_t r = 0; r < sizeof(role_names) / sizeof(role_names[0]); r++) {
                cJSON_AddItemToArray(roles, cJSON_CreateString(role_names[r]));
            }
            for (int step = 0; step <= TEXT_USER_STEP_MAX; step++) {
                cJSON *row = cJSON_CreateArray();
                for (int r = UI_TEXT_META; r <= UI_TEXT_HERO && row != NULL; r++) {
                    cJSON_AddItemToArray(row, cJSON_CreateNumber(text_scale_px(board, (ui_text_role_t)r, step)));
                }
                if (row != NULL) cJSON_AddItemToArray(sizes, row);
            }
        }
    }

    /* Build provenance: compile date/time come from the app descriptor
     * (always present); the git commit is baked into component_versions.h
     * at configure time - see main/CMakeLists.txt. */
    /* "Sep 20 2026 10:42:11": date is the first 11 characters. */
    char build_date_buf[16] = {0};
    const char *build_time = "";
    if (strlen(app_build_stamp) > 12) {
        memcpy(build_date_buf, app_build_stamp, 11);
        build_time = app_build_stamp + 12;
    }
    const char *build_date = build_date_buf;
    (void)desc;
    add_version_if_present(root, "build_date", build_date);
    add_version_if_present(root, "build_time", build_time);
    add_version_if_present(root, "git_commit", APP_GIT_COMMIT_SHA);

    /* Build toolchain / component versions, for support & diagnostics
     * (e.g. confirming which esp_lvgl_adapter/esp_lcd_jd9365 version a build
     * actually resolved). idf_ver comes from the running binary's app
     * descriptor; the rest are baked in at configure time from this
     * variant's dependencies.lock - see tools/generate_component_versions.py. */
    cJSON *components = cJSON_AddObjectToObject(root, "components");
    if (components != NULL) {
        const char *idf_ver = (desc != NULL && desc->idf_ver[0] != '\0') ? desc->idf_ver : "";
        add_version_if_present(components, "esp_idf", idf_ver);
        add_version_if_present(components, "lvgl", APP_COMPONENT_VERSION_LVGL);
        add_version_if_present(components, "esp_lvgl_adapter", APP_COMPONENT_VERSION_ESP_LVGL_ADAPTER);
        /* Some BSPs still compile the old port as a transitive dependency. */
        add_version_if_present(components, "esp_lvgl_port", APP_COMPONENT_VERSION_ESP_LVGL_PORT);
        add_version_if_present(components, "esp_lcd_jd9365", APP_COMPONENT_VERSION_ESP_LCD_JD9365);
        add_version_if_present(components, "esp_lcd_touch", APP_COMPONENT_VERSION_ESP_LCD_TOUCH);
        add_version_if_present(components, "esp_hosted", APP_COMPONENT_VERSION_ESP_HOSTED);
        add_version_if_present(components, "esp_wifi_remote", APP_COMPONENT_VERSION_ESP_WIFI_REMOTE);
        add_version_if_present(components, "esp_websocket_client", APP_COMPONENT_VERSION_ESP_WEBSOCKET_CLIENT);
        add_version_if_present(components, "littlefs", APP_COMPONENT_VERSION_LITTLEFS);
        add_version_if_present(components, "mqtt", APP_COMPONENT_VERSION_MQTT);
        add_version_if_present(components, "cjson", APP_COMPONENT_VERSION_CJSON);
        add_version_if_present(components, "esp_serial_slave_link", APP_COMPONENT_VERSION_ESP_SERIAL_SLAVE_LINK);
        add_version_if_present(components, "waveshare_bsp", APP_COMPONENT_VERSION_WAVESHARE_BSP);
        add_version_if_present(components, "m5stack_bsp", APP_COMPONENT_VERSION_M5STACK_BSP);
        add_version_if_present(components, "esp_lcd_st7703", APP_COMPONENT_VERSION_ESP_LCD_ST7703);
        add_version_if_present(components, "esp_lcd_touch_gt911", APP_COMPONENT_VERSION_ESP_LCD_TOUCH_GT911);
        add_version_if_present(components, "esp_video", APP_COMPONENT_VERSION_ESP_VIDEO);
        add_version_if_present(components, "esp_ipa", APP_COMPONENT_VERSION_ESP_IPA);
        add_version_if_present(components, "esp_sccb_intf", APP_COMPONENT_VERSION_ESP_SCCB_INTF);
        add_version_if_present(components, "esp_cam_sensor", APP_COMPONENT_VERSION_ESP_CAM_SENSOR);
#if APP_HAVE_HOSTED_C6_FW_IMAGE
        /* Version of the ESP32-C6 firmware image embedded in this build (it matches esp_hosted). */
        add_version_if_present(components, "esp32_c6_firmware", APP_HOSTED_C6_EXPECTED_VERSION);
#endif
    }

    /* Live memory snapshot, for spotting leaks/regressions across firmware
     * updates without a serial connection. Reported as internal RAM vs.
     * PSRAM separately (matching the ha_client "mem[...]" diagnostic log
     * lines' int_free/psram_free convention) rather than a combined
     * esp_get_free_heap_size() figure - on this chip PSRAM is folded into
     * the general allocator, so a combined number and a separate PSRAM
     * number would overlap and look like two distinct pools that add up,
     * when the PSRAM figure is actually a subset of the combined one.
     * PSRAM fields are omitted on variants without PSRAM (total size 0). */
    cJSON *memory = cJSON_AddObjectToObject(root, "memory");
    if (memory != NULL) {
        cJSON_AddNumberToObject(memory, "internal_total", (double)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
        cJSON_AddNumberToObject(memory, "free_heap_internal", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        cJSON_AddNumberToObject(
            memory, "min_free_heap_internal", (double)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        cJSON_AddNumberToObject(
            memory, "largest_free_block_internal", (double)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        if (psram_total > 0) {
            cJSON_AddNumberToObject(memory, "psram_total", (double)psram_total);
            cJSON_AddNumberToObject(memory, "free_psram", (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }

    char query[24] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char flag[4] = {0};
        if (httpd_query_key_value(query, "backlight", flag, sizeof(flag)) == ESP_OK && flag[0] == '1') {
            const bool locked = display_lock(100);
            display_backlight_diagnostics_t sample = {0};
            const bool available = locked && display_backlight_diagnostics(&sample);
            if (locked) display_unlock();
            cJSON *backlight = cJSON_AddObjectToObject(root, "backlight");
            if (backlight != NULL) {
                cJSON_AddBoolToObject(backlight, "available", available);
                cJSON_AddBoolToObject(backlight, "busy", !locked);
                if (available) {
                    cJSON_AddNumberToObject(backlight, "gpio", sample.gpio_pin);
                    cJSON_AddNumberToObject(backlight, "requested_percent", sample.requested_percent);
                    cJSON_AddNumberToObject(backlight, "frequency_hz", sample.frequency_hz);
                    cJSON_AddNumberToObject(backlight, "duty", sample.duty);
                    cJSON_AddNumberToObject(backlight, "max_duty", sample.max_duty);
                    cJSON_AddNumberToObject(backlight, "samples", sample.samples);
                    cJSON_AddNumberToObject(backlight, "high_samples", sample.high_samples);
                    cJSON_AddNumberToObject(backlight, "elapsed_us", sample.elapsed_us);
                    cJSON_AddNumberToObject(backlight, "gpio_function", sample.gpio_function);
                    cJSON_AddNumberToObject(backlight, "gpio_signal", sample.gpio_signal);
                    cJSON_AddBoolToObject(backlight, "input_enabled", sample.gpio_input);
                    cJSON_AddBoolToObject(backlight, "output_enabled", sample.gpio_output);
                    cJSON_AddBoolToObject(backlight, "peripheral_output", sample.gpio_peripheral_output);
                    cJSON_AddBoolToObject(backlight, "open_drain", sample.gpio_open_drain);
                    cJSON_AddBoolToObject(backlight, "stable", sample.stable);
                }
            }
        }
        if (httpd_query_key_value(query, "tasks", flag, sizeof(flag)) == ESP_OK && flag[0] == '1') {
            add_task_stacks(root);
            lvgl_alloc_stats_t lvgl;
            lvgl_alloc_get_stats(&lvgl);
            cJSON *lvgl_mem = cJSON_AddObjectToObject(root, "lvgl_mem");
            if (lvgl_mem != NULL) {
                cJSON_AddBoolToObject(lvgl_mem, "counting", lvgl.active);
                cJSON_AddBoolToObject(lvgl_mem, "psram_first", lvgl.psram_first);
                cJSON_AddNumberToObject(lvgl_mem, "internal_bytes", (double)lvgl.internal_bytes);
                cJSON_AddNumberToObject(lvgl_mem, "psram_bytes", (double)lvgl.psram_bytes);
                cJSON_AddNumberToObject(lvgl_mem, "internal_peak", (double)lvgl.internal_peak);
                cJSON_AddNumberToObject(lvgl_mem, "psram_peak", (double)lvgl.psram_peak);
                cJSON_AddNumberToObject(lvgl_mem, "live_allocations", (double)lvgl.live_allocations);
                cJSON_AddNumberToObject(lvgl_mem, "internal_allocations_total", (double)lvgl.internal_allocations);
                cJSON_AddNumberToObject(lvgl_mem, "psram_allocations_total", (double)lvgl.psram_allocations);
            }
        }
    }

    /* Backlight now and why: night mode, dimmed (idle), or active; "room_light_percent" is the
     * camera factor (100 = no scaling). */
    cJSON *disp = cJSON_AddObjectToObject(root, "display");
    cJSON_AddNumberToObject(disp, "brightness_percent", display_get_brightness_percent());
    cJSON_AddStringToObject(disp, "state", ui_display_brightness_test_percent() >= 0 ? "test" : ui_display_protection_is_active()
        ? (ui_display_protection_is_off() ? "off" :
            (ui_display_protection_is_pages() ? "pages" : "weather"))
        : (night_mode_active() ? "night" : (display_is_dimmed() ? "dimmed" : "active")));
    cJSON_AddNumberToObject(disp, "room_light_percent", display_get_ambient_limit_percent());

    /* Presence sensor status (Settings > Display > Camera): absent on variants without the
     * built-in camera. motion_pct only updates while the display is dimmed or a test is
     * running (POST /api/camera/test), see ambient_camera.c. */
    if (ambient_camera_available()) {
        ambient_camera_reading_t reading = {0};
        (void)ambient_camera_get(&reading);
        int test_remaining_s = 0;
        bool test_active = ambient_camera_test_active(&test_remaining_s);
        int threshold_pct = ambient_camera_get_presence_threshold_pct();
        cJSON *camera = cJSON_AddObjectToObject(root, "camera");
        if (camera != NULL) {
            cJSON_AddNumberToObject(camera, "presence_motion_pct", reading.motion_pct);
            cJSON_AddNumberToObject(camera, "presence_threshold_pct", threshold_pct);
            cJSON_AddBoolToObject(camera, "presence_detected", reading.motion_pct >= threshold_pct);
            cJSON_AddBoolToObject(camera, "test_active", test_active);
            cJSON_AddNumberToObject(camera, "test_remaining_s", test_remaining_s);
        }
    }

    if (display_orientation_supported()) {
        display_orientation_status_t status;
        display_orientation_get_status(&status);
        cJSON *rotation = cJSON_AddObjectToObject(root, "rotation");
        cJSON_AddBoolToObject(rotation, "enabled", status.enabled);
        cJSON_AddBoolToObject(rotation, "sensor_ready", status.sensor_ready);
        cJSON_AddNumberToObject(rotation, "degrees", display_geometry_rotation());
        cJSON_AddNumberToObject(rotation, "boot_degrees", status.boot_degrees);
        cJSON_AddNumberToObject(rotation, "saved_degrees", status.saved_degrees);
        cJSON_AddStringToObject(rotation, "orientation", display_geometry_portrait() ? "portrait" : "landscape");
        cJSON_AddNumberToObject(rotation, "x_g", status.x_g);
        cJSON_AddNumberToObject(rotation, "y_g", status.y_g);
        cJSON_AddNumberToObject(rotation, "z_g", status.z_g);
    }
    cJSON_AddStringToObject(root, "reset_reason", reset_reason_name(esp_reset_reason()));
    cJSON_AddBoolToObject(root, "safe_mode", boot_guard_safe_mode()); /* started with the built-in layout after a crash loop */
    /* The silicon this chip is and the revisions this image was built for (see util/ota_chip_rev.h): a chip outside
     * the range cannot boot the image, and OTA refuses an image whose range excludes the chip. */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON *silicon = cJSON_AddObjectToObject(root, "silicon");
    if (silicon != NULL) {
        char rev_text[12];
        (void)ota_rev_format(chip_info.revision, rev_text, sizeof(rev_text));
        cJSON_AddNumberToObject(silicon, "chip_revision", (double)chip_info.revision);
        cJSON_AddStringToObject(silicon, "chip_revision_text", rev_text);
        cJSON_AddNumberToObject(silicon, "image_min_revision", (double)CONFIG_ESP_REV_MIN_FULL);
        cJSON_AddNumberToObject(silicon, "image_max_revision", (double)CONFIG_ESP_REV_MAX_FULL);
    }
    net_watchdog_add_json(root);
    ui_watchdog_add_json(root);
    cJSON_AddNumberToObject(root, "max_pages", (double)APP_MAX_PAGES);
    cJSON_AddNumberToObject(root, "full_nav_pages", (double)nav_layout_full_pages(APP_VIEW_WIDTH, 90)); /* beyond it the bar shows icons */
    cJSON_AddNumberToObject(root, "layout_max_bytes", (double)APP_LAYOUT_MAX_JSON_LEN); /* the editor checks its size against it */
    cJSON_AddStringToObject(root, "last_reboot", reboot_log_last_label()); /* how the previous run ended, see GET /api/reboot-log */
    cJSON_AddStringToObject(root, "ota_image_state", ota_validate_image_state()); /* "pending_verify" until the rollback is cancelled */
    uint32_t bootloader_version = 0;
    char bootloader_built[sizeof(((esp_bootloader_desc_t *)0)->date_time) + 1] = {0};
    cJSON *bootloader = cJSON_AddObjectToObject(root, "bootloader"); /* the one in flash: only USB updates it, see docs/development.md */
    if (bootloader != NULL && ota_validate_bootloader_info(&bootloader_version, bootloader_built, sizeof(bootloader_built))) {
        cJSON_AddNumberToObject(bootloader, "version", (double)bootloader_version);
        cJSON_AddStringToObject(bootloader, "built", bootloader_built);
        cJSON_AddBoolToObject(bootloader, "rollback", ota_bootloader_has_rollback(bootloader_version));
    }
    cJSON_AddStringToObject(root, "ha_auth", ha_auth_state_name()); /* "ok" or why HA requests stopped */

    /* The last two reports, with a legacy summary of the newest for older editors. */
    coredump_archive_add_json(root);

    /* Wi-Fi/C6 connectivity diagnostics (net/wifi_diag_store.h): "current" is this boot's live
     * counters (wifi_mgr.c), all zero/absent in the normal case; "prev_boot" is whatever was
     * persisted before the most recent reset, the only way to see what a real outage looked like
     * when it was severe enough that a forced reset was the only way out (HTTP unreachable, and
     * reading serial to look would have reset the board and lost the same information). */
    cJSON *wifi_diag_obj = cJSON_AddObjectToObject(root, "wifi_diag");
    wifi_mgr_diag_t wifi_diag_current;
    wifi_mgr_get_diag(&wifi_diag_current);
    api_version_add_wifi_diag(cJSON_AddObjectToObject(wifi_diag_obj, "current"), &wifi_diag_current);
    wifi_mgr_diag_t wifi_diag_prev;
    if (wifi_diag_store_load(&wifi_diag_prev)) {
        api_version_add_wifi_diag(cJSON_AddObjectToObject(wifi_diag_obj, "prev_boot"), &wifi_diag_prev);
    }

    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    sample_cpu_load();
    if (s_cpu_load[0] >= 0) {
        cJSON *cpu = cJSON_AddObjectToObject(root, "cpu");
        cJSON *load = cJSON_AddArrayToObject(cpu, "load_percent");
        for (int c = 0; c < CPU_CORES; c++) {
            cJSON_AddItemToArray(load, cJSON_CreateNumber(s_cpu_load[c]));
        }
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}
