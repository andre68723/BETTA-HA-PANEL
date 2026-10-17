/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_SPIRAM 1
#define CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM 1
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 0
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTP_EAGAIN 0x7007
typedef struct { uint32_t w, h; } lv_test_image_header_t;
typedef struct { lv_test_image_header_t header; const uint8_t *data; size_t data_size; } lv_image_dsc_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void *SemaphoreHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
void *heap_caps_malloc(size_t, unsigned);
void *heap_caps_calloc(size_t, size_t, unsigned);
void heap_caps_free(void *);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
void xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
BaseType_t xTaskCreatePinnedToCoreWithCaps(void (*)(void *), const char *, unsigned, void *, unsigned, void *, int, unsigned);
void vTaskDeleteWithCaps(void *);
void vTaskDelay(TickType_t);
int64_t esp_timer_get_time(void);
typedef void *esp_http_client_handle_t;
typedef struct { int event_id; void *user_data; const char *header_key, *header_value; } esp_http_client_event_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct {
    const char *url, *common_name;
    int timeout_ms, buffer_size, buffer_size_tx;
    bool disable_auto_redirect;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
void esp_http_client_close(esp_http_client_handle_t);
void esp_http_client_cleanup(esp_http_client_handle_t);
