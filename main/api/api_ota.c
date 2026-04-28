/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "api/api_routes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "bsp/display.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "ui/ui_ota_progress.h"
#include "util/log_tags.h"

#define OTA_APP_DESC_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))
#define OTA_HEADER_CHECK_LEN (OTA_APP_DESC_OFFSET + sizeof(esp_app_desc_t))
#define OTA_URL_MAX_REDIRECTS 8
#define TAG_API_OTA "api_ota"

typedef enum {
    API_OTA_STATE_IDLE = 0,
    API_OTA_STATE_URL,
    API_OTA_STATE_UPLOAD,
    API_OTA_STATE_SUCCESS,
    API_OTA_STATE_ERROR,
} api_ota_state_t;

typedef struct {
    api_ota_state_t state;
    bool running;
    bool rebooting;
    size_t written;
    size_t total;
    int64_t started_ms;
    int64_t updated_ms;
    char source[APP_OTA_URL_MAX_LEN];
    char error[160];
    char version[32];
    char project_name[32];
    char partition_label[16];
} api_ota_status_t;

typedef struct {
    const esp_partition_t *partition;
    esp_ota_handle_t handle;
    bool begun;
    bool header_checked;
    uint8_t header_buf[OTA_HEADER_CHECK_LEN];
    size_t header_len;
    size_t written;
    size_t total;
    char version[32];
    char project_name[32];
    char error[160];
} api_ota_stream_t;

static SemaphoreHandle_t s_ota_mutex = NULL;
static esp_timer_handle_t s_ota_restart_timer = NULL;
static api_ota_status_t s_ota_status = {
    .state = API_OTA_STATE_IDLE,
};

static int64_t ota_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static esp_err_t ota_ensure_mutex(void)
{
    if (s_ota_mutex != NULL) {
        return ESP_OK;
    }
    s_ota_mutex = xSemaphoreCreateMutex();
    return (s_ota_mutex != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

static void set_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", message != NULL ? message : "OTA request failed");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(req);
    }

    set_json_headers(req);
    if (status != NULL) {
        httpd_resp_set_status(req, status);
    }
    esp_err_t err = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return err;
}

static const char *ota_state_text(api_ota_state_t state)
{
    switch (state) {
    case API_OTA_STATE_URL:
        return "url";
    case API_OTA_STATE_UPLOAD:
        return "upload";
    case API_OTA_STATE_SUCCESS:
        return "success";
    case API_OTA_STATE_ERROR:
        return "error";
    case API_OTA_STATE_IDLE:
    default:
        return "idle";
    }
}

static void ota_status_reset_locked(void)
{
    memset(&s_ota_status, 0, sizeof(s_ota_status));
    s_ota_status.state = API_OTA_STATE_IDLE;
}

static bool ota_begin_status(api_ota_state_t state, const char *source, size_t total, const char *partition_label)
{
    if (ota_ensure_mutex() != ESP_OK) {
        return false;
    }

    bool started = false;
    int64_t now_ms = ota_now_ms();
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    if (!s_ota_status.running && !s_ota_status.rebooting) {
        ota_status_reset_locked();
        s_ota_status.state = state;
        s_ota_status.running = true;
        s_ota_status.total = total;
        s_ota_status.started_ms = now_ms;
        s_ota_status.updated_ms = now_ms;
        if (source != NULL) {
            strlcpy(s_ota_status.source, source, sizeof(s_ota_status.source));
        }
        if (partition_label != NULL) {
            strlcpy(s_ota_status.partition_label, partition_label, sizeof(s_ota_status.partition_label));
        }
        started = true;
    }
    xSemaphoreGive(s_ota_mutex);
    if (started) {
        const char *status_text = (state == API_OTA_STATE_URL) ? "Downloading firmware..." : "Receiving firmware...";
        ui_ota_progress_begin(status_text, total);
    }
    return started;
}

static void ota_update_progress(size_t written, size_t total)
{
    if (s_ota_mutex == NULL) {
        return;
    }
    size_t effective_total = total;
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    s_ota_status.written = written;
    if (total > 0) {
        s_ota_status.total = total;
    }
    effective_total = s_ota_status.total;
    s_ota_status.updated_ms = ota_now_ms();
    xSemaphoreGive(s_ota_mutex);
    ui_ota_progress_update(written, effective_total);
}

static void ota_set_image_info(const char *version, const char *project_name)
{
    if (s_ota_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    if (version != NULL) {
        strlcpy(s_ota_status.version, version, sizeof(s_ota_status.version));
    }
    if (project_name != NULL) {
        strlcpy(s_ota_status.project_name, project_name, sizeof(s_ota_status.project_name));
    }
    s_ota_status.updated_ms = ota_now_ms();
    xSemaphoreGive(s_ota_mutex);

    char status_text[64] = {0};
    if (version != NULL && version[0] != '\0') {
        snprintf(status_text, sizeof(status_text), "Writing firmware %s", version);
    } else {
        strlcpy(status_text, "Writing firmware", sizeof(status_text));
    }
    ui_ota_progress_set_status(status_text);
}

static void ota_finish_status_error(const char *message)
{
    if (s_ota_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    s_ota_status.state = API_OTA_STATE_ERROR;
    s_ota_status.running = false;
    s_ota_status.rebooting = false;
    s_ota_status.updated_ms = ota_now_ms();
    strlcpy(s_ota_status.error, message != NULL ? message : "OTA failed", sizeof(s_ota_status.error));
    xSemaphoreGive(s_ota_mutex);
    ui_ota_progress_error(message);
}

static void ota_finish_status_success(void)
{
    if (s_ota_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    s_ota_status.state = API_OTA_STATE_SUCCESS;
    s_ota_status.running = false;
    s_ota_status.rebooting = true;
    s_ota_status.updated_ms = ota_now_ms();
    xSemaphoreGive(s_ota_mutex);
    ui_ota_progress_success("Update written. Restarting...");
}

static void ota_restart_timer_cb(void *arg)
{
    (void)arg;
    (void)bsp_display_backlight_off();
    esp_restart();
}

static void ota_schedule_restart(void)
{
    if (s_ota_restart_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = ota_restart_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ota_restart",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&timer_args, &s_ota_restart_timer) != ESP_OK) {
            esp_restart();
            return;
        }
    }

    if (esp_timer_is_active(s_ota_restart_timer)) {
        (void)esp_timer_stop(s_ota_restart_timer);
    }
    if (esp_timer_start_once(s_ota_restart_timer, 1800ULL * 1000ULL) != ESP_OK) {
        esp_restart();
    }
}

static bool ota_url_is_https(const char *url)
{
    return url != NULL && strncmp(url, "https://", 8) == 0;
}

static bool ota_http_status_is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool ota_copy_url_without_query(const char *src, char *dst, size_t dst_len)
{
    if (src == NULL || dst == NULL || dst_len == 0) {
        return false;
    }
    size_t written = 0;
    while (src[written] != '\0' && src[written] != '?' && src[written] != '#') {
        if (written + 1U >= dst_len) {
            return false;
        }
        dst[written] = src[written];
        written++;
    }
    dst[written] = '\0';
    return written > 0;
}

static bool ota_normalize_url(const char *input, char *out, size_t out_len)
{
    if (input == NULL || out == NULL || out_len == 0) {
        return false;
    }

    char url[APP_OTA_URL_MAX_LEN] = {0};
    while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') {
        input++;
    }
    if (!ota_copy_url_without_query(input, url, sizeof(url))) {
        return false;
    }

    const char *github_prefix = "https://github.com/";
    if (strncmp(url, github_prefix, strlen(github_prefix)) != 0) {
        if (!ota_url_is_https(url)) {
            return false;
        }
        strlcpy(out, url, out_len);
        return true;
    }

    const char *path = url + strlen(github_prefix);
    const char *owner_end = strchr(path, '/');
    if (owner_end == NULL) {
        return false;
    }
    const char *repo = owner_end + 1;
    const char *repo_end = strchr(repo, '/');
    if (repo_end == NULL) {
        return false;
    }
    const char *marker = repo_end;
    const char *blob = "/blob/";
    const char *raw = "/raw/";
    const char *ref = NULL;
    if (strncmp(marker, blob, strlen(blob)) == 0) {
        ref = marker + strlen(blob);
    } else if (strncmp(marker, raw, strlen(raw)) == 0) {
        ref = marker + strlen(raw);
    } else {
        strlcpy(out, url, out_len);
        return ota_url_is_https(out);
    }

    const char *ref_end = strchr(ref, '/');
    if (ref_end == NULL || ref_end[1] == '\0') {
        return false;
    }

    int written = snprintf(out, out_len, "https://raw.githubusercontent.com/%.*s/%.*s/%.*s/%s",
        (int)(owner_end - path), path,
        (int)(repo_end - repo), repo,
        (int)(ref_end - ref), ref,
        ref_end + 1);
    return written > 0 && (size_t)written < out_len && ota_url_is_https(out);
}

static void ota_stream_set_error(api_ota_stream_t *stream, const char *message)
{
    if (stream == NULL) {
        return;
    }
    strlcpy(stream->error, message != NULL ? message : "OTA stream failed", sizeof(stream->error));
}

static esp_err_t ota_stream_init(api_ota_stream_t *stream, size_t total)
{
    if (stream == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(stream, 0, sizeof(*stream));
    stream->total = total;

    stream->partition = esp_ota_get_next_update_partition(NULL);
    if (stream->partition == NULL) {
        ota_stream_set_error(stream, "No OTA partition found. Flash the new OTA-capable factory image first.");
        return ESP_ERR_NOT_FOUND;
    }
    if (total > 0 && total > stream->partition->size) {
        ota_stream_set_error(stream, "OTA image is larger than the target partition");
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t ota_stream_validate_header(api_ota_stream_t *stream)
{
    if (stream == NULL || stream->header_len < OTA_HEADER_CHECK_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_image_header_t *image_header = (const esp_image_header_t *)stream->header_buf;
    if (image_header->magic != ESP_IMAGE_HEADER_MAGIC) {
        ota_stream_set_error(stream, "Selected file is not an ESP app image");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const esp_app_desc_t *new_app = (const esp_app_desc_t *)(stream->header_buf + OTA_APP_DESC_OFFSET);
    const esp_app_desc_t *running_app = esp_app_get_description();
    if (running_app != NULL &&
        strncmp(new_app->project_name, running_app->project_name, sizeof(new_app->project_name)) != 0) {
        ota_stream_set_error(stream, "OTA image project name does not match this firmware");
        return ESP_ERR_INVALID_RESPONSE;
    }

    strlcpy(stream->version, new_app->version, sizeof(stream->version));
    strlcpy(stream->project_name, new_app->project_name, sizeof(stream->project_name));
    ota_set_image_info(stream->version, stream->project_name);
    stream->header_checked = true;
    return ESP_OK;
}

static esp_err_t ota_stream_begin(api_ota_stream_t *stream)
{
    if (stream == NULL || stream->partition == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (stream->begun) {
        return ESP_OK;
    }

    esp_err_t err = esp_ota_begin(stream->partition, stream->total > 0 ? stream->total : OTA_SIZE_UNKNOWN, &stream->handle);
    if (err != ESP_OK) {
        ota_stream_set_error(stream, "Failed to begin OTA write");
        return err;
    }
    stream->begun = true;
    return ESP_OK;
}

static esp_err_t ota_stream_write_checked(api_ota_stream_t *stream, const uint8_t *data, size_t len)
{
    if (stream == NULL || data == NULL || len == 0) {
        return ESP_OK;
    }

    if (!stream->header_checked) {
        size_t copied = 0;
        if (stream->header_len < OTA_HEADER_CHECK_LEN) {
            size_t need = OTA_HEADER_CHECK_LEN - stream->header_len;
            copied = (len < need) ? len : need;
            memcpy(stream->header_buf + stream->header_len, data, copied);
            stream->header_len += copied;
        }
        if (stream->header_len < OTA_HEADER_CHECK_LEN) {
            return ESP_OK;
        }

        esp_err_t err = ota_stream_validate_header(stream);
        if (err != ESP_OK) {
            return err;
        }
        err = ota_stream_begin(stream);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_ota_write(stream->handle, stream->header_buf, stream->header_len);
        if (err != ESP_OK) {
            ota_stream_set_error(stream, "Failed to write OTA header");
            return err;
        }
        stream->written += stream->header_len;
        ota_update_progress(stream->written, stream->total);

        if (copied >= len) {
            return ESP_OK;
        }
        data += copied;
        len -= copied;
    }

    esp_err_t err = ota_stream_begin(stream);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_ota_write(stream->handle, data, len);
    if (err != ESP_OK) {
        ota_stream_set_error(stream, "Failed to write OTA data");
        return err;
    }
    stream->written += len;
    ota_update_progress(stream->written, stream->total);
    return ESP_OK;
}

static esp_err_t ota_stream_finish(api_ota_stream_t *stream)
{
    if (stream == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!stream->header_checked) {
        ota_stream_set_error(stream, "OTA image is too small or incomplete");
        return ESP_ERR_INVALID_SIZE;
    }
    if (stream->total > 0 && stream->written != stream->total) {
        ota_stream_set_error(stream, "OTA transfer ended before all bytes were received");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_end(stream->handle);
    stream->begun = false;
    if (err != ESP_OK) {
        ota_stream_set_error(stream, "OTA image validation failed");
        return err;
    }

    err = esp_ota_set_boot_partition(stream->partition);
    if (err != ESP_OK) {
        ota_stream_set_error(stream, "Failed to select OTA boot partition");
        return err;
    }
    return ESP_OK;
}

static void ota_stream_abort(api_ota_stream_t *stream)
{
    if (stream != NULL && stream->begun) {
        (void)esp_ota_abort(stream->handle);
        stream->begun = false;
    }
}

static char *ota_read_request_body(httpd_req_t *req, size_t max_len)
{
    if (req == NULL || req->content_len <= 0 || (size_t)req->content_len > max_len) {
        return NULL;
    }
    char *buf = calloc((size_t)req->content_len + 1U, sizeof(char));
    if (buf == NULL) {
        return NULL;
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        received += r;
    }
    buf[received] = '\0';
    return buf;
}

static esp_err_t ota_send_status_json(httpd_req_t *req)
{
    if (ota_ensure_mutex() != ESP_OK) {
        return httpd_resp_send_500(req);
    }

    api_ota_status_t status = {0};
    xSemaphoreTake(s_ota_mutex, portMAX_DELAY);
    status = s_ota_status;
    xSemaphoreGive(s_ota_mutex);

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running = esp_ota_get_running_partition();

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "state", ota_state_text(status.state));
    cJSON_AddBoolToObject(root, "running", status.running);
    cJSON_AddBoolToObject(root, "rebooting", status.rebooting);
    cJSON_AddNumberToObject(root, "written", (double)status.written);
    cJSON_AddNumberToObject(root, "total", (double)status.total);
    double progress = 0.0;
    if (status.total > 0) {
        progress = ((double)status.written * 100.0) / (double)status.total;
        if (progress > 100.0) {
            progress = 100.0;
        }
    }
    cJSON_AddNumberToObject(root, "progress", progress);
    cJSON_AddStringToObject(root, "source", status.source);
    cJSON_AddStringToObject(root, "error", status.error);
    cJSON_AddStringToObject(root, "version", status.version);
    cJSON_AddStringToObject(root, "project_name", status.project_name);
    cJSON_AddStringToObject(root, "partition", status.partition_label);
    cJSON_AddBoolToObject(root, "available", next != NULL);
    cJSON_AddStringToObject(root, "next_partition", next != NULL ? next->label : "");
    cJSON_AddStringToObject(root, "running_partition", running != NULL ? running->label : "");
    cJSON_AddNumberToObject(root, "slot_size", next != NULL ? (double)next->size : 0.0);
    cJSON_AddNumberToObject(root, "updated_ms", (double)status.updated_ms);

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

esp_err_t api_ota_status_get_handler(httpd_req_t *req)
{
    return ota_send_status_json(req);
}

typedef struct {
    char url[APP_OTA_URL_MAX_LEN];
} ota_url_task_arg_t;

static esp_err_t ota_http_open_follow_redirects(esp_http_client_handle_t client,
                                                int64_t *out_content_length,
                                                int *out_status)
{
    if (client == NULL || out_content_length == NULL || out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_content_length = 0;
    *out_status = 0;

    for (int redirects = 0; redirects <= OTA_URL_MAX_REDIRECTS; redirects++) {
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            return err;
        }

        int64_t content_length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        *out_content_length = content_length;
        *out_status = status;

        if (status == 200) {
            return ESP_OK;
        }
        if (!ota_http_status_is_redirect(status)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (redirects >= OTA_URL_MAX_REDIRECTS) {
            return ESP_ERR_HTTP_MAX_REDIRECT;
        }

        ESP_LOGI(TAG_API_OTA, "OTA URL redirect HTTP %d (%d/%d)",
                 status, redirects + 1, OTA_URL_MAX_REDIRECTS);
        err = esp_http_client_set_redirection(client);
        esp_http_client_close(client);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_ERR_HTTP_MAX_REDIRECT;
}

static void ota_url_task(void *arg)
{
    ota_url_task_arg_t *task_arg = (ota_url_task_arg_t *)arg;
    char url[APP_OTA_URL_MAX_LEN] = {0};
    if (task_arg != NULL) {
        strlcpy(url, task_arg->url, sizeof(url));
        free(task_arg);
    }

    api_ota_stream_t stream = {0};
    esp_http_client_handle_t client = NULL;
    char *chunk = NULL;
    esp_err_t err = ESP_OK;

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = APP_OTA_CHUNK_SIZE,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
        .max_redirection_count = OTA_URL_MAX_REDIRECTS,
    };
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ota_finish_status_error("Failed to initialize HTTPS client");
        goto done;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "User-Agent", "BETTA-HA-PANEL-OTA");

    int64_t content_length = 0;
    int status = 0;
    err = ota_http_open_follow_redirects(client, &content_length, &status);
    if (err != ESP_OK) {
        if (status > 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "OTA URL returned HTTP %d", status);
            ota_finish_status_error(msg);
        } else {
            ota_finish_status_error("Failed to open OTA URL");
        }
        goto done;
    }
    if (status != 200) {
        char msg[96];
        snprintf(msg, sizeof(msg), "OTA URL returned HTTP %d", status);
        ota_finish_status_error(msg);
        goto done;
    }

    size_t total = (content_length > 0) ? (size_t)content_length : 0U;
    err = ota_stream_init(&stream, total);
    if (err != ESP_OK) {
        ota_finish_status_error(stream.error);
        goto done;
    }

    chunk = (char *)heap_caps_malloc(APP_OTA_CHUNK_SIZE, MALLOC_CAP_8BIT);
    if (chunk == NULL) {
        ota_finish_status_error("Failed to allocate OTA download buffer");
        goto done;
    }

    bool read_failed = false;
    while (true) {
        int read = esp_http_client_read(client, chunk, APP_OTA_CHUNK_SIZE);
        if (read < 0) {
            read_failed = true;
            break;
        }
        if (read == 0) {
            break;
        }
        err = ota_stream_write_checked(&stream, (const uint8_t *)chunk, (size_t)read);
        if (err != ESP_OK) {
            break;
        }
    }

    if (read_failed) {
        ota_stream_abort(&stream);
        ota_finish_status_error("OTA URL read failed");
        goto done;
    }
    if (err != ESP_OK) {
        ota_stream_abort(&stream);
        ota_finish_status_error(stream.error);
        goto done;
    }
    err = ota_stream_finish(&stream);
    if (err != ESP_OK) {
        ota_stream_abort(&stream);
        ota_finish_status_error(stream.error);
        goto done;
    }

    ota_finish_status_success();
    ota_schedule_restart();

done:
    if (chunk != NULL) {
        heap_caps_free(chunk);
    }
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    vTaskDelete(NULL);
}

esp_err_t api_ota_url_post_handler(httpd_req_t *req)
{
    char *body = ota_read_request_body(req, APP_OTA_JSON_MAX_LEN);
    if (body == NULL) {
        return send_json_error(req, "400 Bad Request", "Invalid OTA URL payload");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_json_error(req, "400 Bad Request", "Invalid JSON");
    }

    cJSON *url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
    if (!cJSON_IsString(url_item) || url_item->valuestring == NULL || url_item->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return send_json_error(req, "400 Bad Request", "Missing url");
    }

    char url[APP_OTA_URL_MAX_LEN] = {0};
    bool valid_url = ota_normalize_url(url_item->valuestring, url, sizeof(url));
    cJSON_Delete(root);
    if (!valid_url) {
        return send_json_error(req, "400 Bad Request", "OTA URL must be an https:// URL or a supported GitHub blob/raw URL");
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        return send_json_error(req, "409 Conflict", "No OTA partition found. Flash the OTA-capable factory image first.");
    }
    if (!ota_begin_status(API_OTA_STATE_URL, url, 0, partition->label)) {
        return send_json_error(req, "409 Conflict", "OTA already running or reboot pending");
    }

    ota_url_task_arg_t *task_arg = calloc(1, sizeof(*task_arg));
    if (task_arg == NULL) {
        ota_finish_status_error("Failed to allocate OTA task");
        return httpd_resp_send_500(req);
    }
    strlcpy(task_arg->url, url, sizeof(task_arg->url));

    BaseType_t created = xTaskCreate(ota_url_task, "ota_url", APP_OTA_TASK_STACK, task_arg, APP_OTA_TASK_PRIO, NULL);
    if (created != pdPASS) {
        free(task_arg);
        ota_finish_status_error("Failed to start OTA task");
        return httpd_resp_send_500(req);
    }

    return ota_send_status_json(req);
}

esp_err_t api_ota_upload_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        return send_json_error(req, "400 Bad Request", "Missing OTA binary body");
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        return send_json_error(req, "409 Conflict", "No OTA partition found. Flash the OTA-capable factory image first.");
    }
    if ((size_t)req->content_len > partition->size) {
        return send_json_error(req, "400 Bad Request", "OTA image is larger than the target partition");
    }
    if (!ota_begin_status(API_OTA_STATE_UPLOAD, "browser-upload", (size_t)req->content_len, partition->label)) {
        return send_json_error(req, "409 Conflict", "OTA already running or reboot pending");
    }

    api_ota_stream_t stream = {0};
    esp_err_t err = ota_stream_init(&stream, (size_t)req->content_len);
    if (err != ESP_OK) {
        ota_finish_status_error(stream.error);
        return send_json_error(req, "400 Bad Request", stream.error);
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(APP_OTA_CHUNK_SIZE, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        ota_finish_status_error("Failed to allocate OTA upload buffer");
        return httpd_resp_send_500(req);
    }

    int received = 0;
    while (received < req->content_len) {
        int remaining = req->content_len - received;
        int to_read = remaining > APP_OTA_CHUNK_SIZE ? APP_OTA_CHUNK_SIZE : remaining;
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            ota_stream_abort(&stream);
            heap_caps_free(buf);
            ota_finish_status_error("OTA upload aborted");
            return send_json_error(req, "400 Bad Request", "OTA upload aborted");
        }

        err = ota_stream_write_checked(&stream, buf, (size_t)r);
        if (err != ESP_OK) {
            ota_stream_abort(&stream);
            heap_caps_free(buf);
            ota_finish_status_error(stream.error);
            return send_json_error(req, "400 Bad Request", stream.error);
        }
        received += r;
    }
    heap_caps_free(buf);

    err = ota_stream_finish(&stream);
    if (err != ESP_OK) {
        ota_stream_abort(&stream);
        ota_finish_status_error(stream.error);
        return send_json_error(req, "400 Bad Request", stream.error);
    }

    ota_finish_status_success();
    esp_err_t send_err = ota_send_status_json(req);
    ota_schedule_restart();
    return send_err;
}
