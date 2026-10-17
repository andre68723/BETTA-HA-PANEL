/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/http_guard.h"
#include "settings/admin_auth.h"

#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "util/log_tags.h"
#include "util/user_activity.h"

#define HTTP_GUARD_MAX_ACTIVE_REQUESTS 4
#define HTTP_GUARD_MAX_CLIENTS 16
#define HTTP_GUARD_RATE_PER_SEC 12
#define HTTP_GUARD_BURST 24
#define HTTP_GUARD_MAX_INFLIGHT_PER_CLIENT_GET 3
#define HTTP_GUARD_MAX_INFLIGHT_PER_CLIENT_NON_GET 1

typedef struct {
    bool used;
    uint32_t key;
    int32_t tokens_milli;
    int64_t last_refill_ms;
    int64_t last_seen_ms;
} http_guard_bucket_t;

typedef struct {
    bool used;
    uint32_t key;
    uint8_t in_flight;
    int64_t last_seen_ms;
} http_guard_active_t;

static SemaphoreHandle_t s_active_sem = NULL;
static SemaphoreHandle_t s_guard_lock = NULL;
static bool s_inited = false;
static http_guard_bucket_t s_buckets[HTTP_GUARD_MAX_CLIENTS];
static http_guard_active_t s_active_clients[HTTP_GUARD_MAX_CLIENTS];

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t req_client_key(httpd_req_t *req)
{
    if (req == NULL) {
        return 0;
    }

    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) {
        return 0;
    }

    struct sockaddr_storage addr = {0};
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) != 0) {
        return 0;
    }

    if (addr.ss_family == AF_INET && addr_len >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *sa = (const struct sockaddr_in *)&addr;
        return sa->sin_addr.s_addr;
    }

#if LWIP_IPV6
    if (addr.ss_family == AF_INET6 && addr_len >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *)&addr;
        const uint8_t *a = (const uint8_t *)&sa6->sin6_addr;
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < 16; i++) {
            h ^= a[i];
            h *= 16777619u;
        }
        return h;
    }
#endif

    return 0;
}

static bool is_api_request(const httpd_req_t *req)
{
    if (req == NULL || req->uri[0] == '\0') {
        return false;
    }
    return strncmp(req->uri, "/api/", 5) == 0;
}

static bool rate_limit_allow(uint32_t client_key)
{
    const int32_t burst_milli = HTTP_GUARD_BURST * 1000;
    const int64_t t_now = now_ms();
    int idx = -1;
    int free_idx = -1;
    int oldest_idx = -1;
    int64_t oldest_seen = INT64_MAX;

    for (int i = 0; i < HTTP_GUARD_MAX_CLIENTS; i++) {
        if (s_buckets[i].used) {
            if (s_buckets[i].key == client_key) {
                idx = i;
                break;
            }
            if (s_buckets[i].last_seen_ms < oldest_seen) {
                oldest_seen = s_buckets[i].last_seen_ms;
                oldest_idx = i;
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }

    if (idx < 0) {
        idx = (free_idx >= 0) ? free_idx : oldest_idx;
        if (idx < 0) {
            return false;
        }
        s_buckets[idx].used = true;
        s_buckets[idx].key = client_key;
        s_buckets[idx].tokens_milli = burst_milli;
        s_buckets[idx].last_refill_ms = t_now;
        s_buckets[idx].last_seen_ms = t_now;
    }

    http_guard_bucket_t *b = &s_buckets[idx];
    int64_t elapsed_ms = t_now - b->last_refill_ms;
    if (elapsed_ms > 0) {
        int64_t refill_milli = elapsed_ms * HTTP_GUARD_RATE_PER_SEC;
        int64_t tokens = (int64_t)b->tokens_milli + refill_milli;
        if (tokens > burst_milli) {
            tokens = burst_milli;
        }
        b->tokens_milli = (int32_t)tokens;
        b->last_refill_ms = t_now;
    }
    b->last_seen_ms = t_now;

    if (b->tokens_milli < 1000) {
        return false;
    }

    b->tokens_milli -= 1000;
    return true;
}

static bool active_try_acquire(uint32_t client_key, uint8_t in_flight_limit)
{
    const int64_t t_now = now_ms();
    int idx = -1;
    int free_idx = -1;
    int oldest_idx = -1;
    int64_t oldest_seen = INT64_MAX;

    for (int i = 0; i < HTTP_GUARD_MAX_CLIENTS; i++) {
        if (s_active_clients[i].used) {
            if (s_active_clients[i].key == client_key) {
                idx = i;
                break;
            }
            if (s_active_clients[i].in_flight == 0 && s_active_clients[i].last_seen_ms < oldest_seen) {
                oldest_seen = s_active_clients[i].last_seen_ms;
                oldest_idx = i;
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }

    if (idx < 0) {
        idx = (free_idx >= 0) ? free_idx : oldest_idx;
        if (idx < 0) {
            return false;
        }
        s_active_clients[idx].used = true;
        s_active_clients[idx].key = client_key;
        s_active_clients[idx].in_flight = 0;
    }

    if (s_active_clients[idx].in_flight >= in_flight_limit) {
        s_active_clients[idx].last_seen_ms = t_now;
        return false;
    }

    s_active_clients[idx].in_flight++;
    s_active_clients[idx].last_seen_ms = t_now;
    return true;
}

static void active_release(uint32_t client_key)
{
    const int64_t t_now = now_ms();
    for (int i = 0; i < HTTP_GUARD_MAX_CLIENTS; i++) {
        if (!s_active_clients[i].used || s_active_clients[i].key != client_key) {
            continue;
        }
        if (s_active_clients[i].in_flight > 0) {
            s_active_clients[i].in_flight--;
        }
        s_active_clients[i].last_seen_ms = t_now;
        return;
    }
}

static esp_err_t send_busy(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Retry-After", "1");
    return httpd_resp_sendstr(req, message);
}

esp_err_t http_guard_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    if (s_guard_lock == NULL) {
        s_guard_lock = xSemaphoreCreateMutex();
    }
    if (s_guard_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_guard_lock, portMAX_DELAY);
    if (!s_inited) {
        if (s_active_sem == NULL) {
            s_active_sem = xSemaphoreCreateCounting(HTTP_GUARD_MAX_ACTIVE_REQUESTS, HTTP_GUARD_MAX_ACTIVE_REQUESTS);
        }
        if (s_active_sem == NULL) {
            xSemaphoreGive(s_guard_lock);
            return ESP_ERR_NO_MEM;
        }
        for (int i = 0; i < HTTP_GUARD_MAX_CLIENTS; i++) {
            s_buckets[i].used = false;
            s_active_clients[i].used = false;
        }
        s_inited = true;
    }
    xSemaphoreGive(s_guard_lock);
    return ESP_OK;
}

/* Failed logins per client: 6 wrong passwords lock that client out for 60 s. */
#define HTTP_GUARD_AUTH_MAX_FAILS 6
#define HTTP_GUARD_AUTH_LOCK_MS 60000
#define HTTP_GUARD_AUTH_SLOTS 8
typedef struct {
    uint32_t key;
    uint8_t fails;
    int64_t locked_until_ms;
} http_guard_auth_slot_t;
static http_guard_auth_slot_t s_auth_slots[HTTP_GUARD_AUTH_SLOTS];

static http_guard_auth_slot_t *auth_slot(uint32_t key)
{
    http_guard_auth_slot_t *free_slot = NULL;
    for (int i = 0; i < HTTP_GUARD_AUTH_SLOTS; i++) {
        if (s_auth_slots[i].key == key && (s_auth_slots[i].fails > 0 || s_auth_slots[i].locked_until_ms > 0)) {
            return &s_auth_slots[i];
        }
        if (free_slot == NULL && s_auth_slots[i].fails == 0 && s_auth_slots[i].locked_until_ms == 0) {
            free_slot = &s_auth_slots[i];
        }
    }
    if (free_slot == NULL) {
        free_slot = &s_auth_slots[0];
    }
    free_slot->key = key;
    free_slot->fails = 0;
    free_slot->locked_until_ms = 0;
    return free_slot;
}

static bool request_is_public(const httpd_req_t *req)
{
    /* Health check for tools that wait for a reboot; nothing sensitive. */
    return req->method == HTTP_GET && strcmp(req->uri, "/api/version") == 0;
}

static bool request_content_type_ok(httpd_req_t *req)
{
    if (req->method != HTTP_POST && req->method != HTTP_PUT) {
        return true;
    }
    /* Empty POSTs also need a non-simple media type: /api/coredump/erase and
     * /api/camera/test have no body, but still change device state. */
    char type[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK) {
        return false;
    }
    const char *want = strcmp(req->uri, "/api/ota/upload") == 0 ? "application/octet-stream" : "application/json";
    size_t want_len = strlen(want);
    return strncasecmp(type, want, want_len) == 0 && (type[want_len] == '\0' || type[want_len] == ';');
}

esp_err_t http_guard_handle(httpd_req_t *req, http_guard_handler_t next_handler)
{
    if (req == NULL || next_handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t init_err = http_guard_init();
    if (init_err != ESP_OK) {
        ESP_LOGW(TAG_HTTP, "HTTP guard init failed: %s", esp_err_to_name(init_err));
        return send_busy(req, "503 Service Unavailable", "Service unavailable");
    }

    uint32_t key = req_client_key(req);
    bool allow_rate = true;
    bool allow_concurrency = true;
    bool enforce_api_limits = is_api_request(req);
    /* Status polling (monitors, the editor's device page) is not somebody using the panel. */
    if (strncmp(req->uri, "/api/version", 12) != 0) {
        user_activity_note((uint32_t)(esp_timer_get_time() / 1000000));
    }
    uint8_t in_flight_limit = HTTP_GUARD_MAX_INFLIGHT_PER_CLIENT_NON_GET;
    if (req->method == HTTP_GET) {
        in_flight_limit = HTTP_GUARD_MAX_INFLIGHT_PER_CLIENT_GET;
    }

    xSemaphoreTake(s_guard_lock, portMAX_DELAY);
    if (enforce_api_limits) {
        allow_rate = rate_limit_allow(key);
        if (allow_rate) {
            allow_concurrency = active_try_acquire(key, in_flight_limit);
        }
    }
    xSemaphoreGive(s_guard_lock);

    if (!allow_rate) {
        return send_busy(req, "429 Too Many Requests", "Too many requests");
    }
    if (!allow_concurrency) {
        return send_busy(req, "429 Too Many Requests", "Too many concurrent requests");
    }

    if (xSemaphoreTake(s_active_sem, 0) != pdTRUE) {
        if (enforce_api_limits) {
            xSemaphoreTake(s_guard_lock, portMAX_DELAY);
            active_release(key);
            xSemaphoreGive(s_guard_lock);
        }
        return send_busy(req, "503 Service Unavailable", "Server busy");
    }

    /* Defence in depth for every response: no framing (clickjacking of the editor), no MIME sniffing, no referrer. */
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");

    esp_err_t err;
    bool auth_ok = true;
    bool locked = false;
    bool content_ok = request_content_type_ok(req);
    const admin_auth_decision_t auth_decision = admin_auth_decide(admin_auth_state(), request_is_public(req));
    if (content_ok && auth_decision == ADMIN_AUTH_CHECK) {
        char auth[160] = {0};
        (void)httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth));
        xSemaphoreTake(s_guard_lock, portMAX_DELAY);
        http_guard_auth_slot_t *slot = auth_slot(key);
        if (slot->locked_until_ms > now_ms()) {
            locked = true;
        } else if (slot->locked_until_ms > 0) {
            slot->locked_until_ms = 0;
            slot->fails = 0;
        }
        xSemaphoreGive(s_guard_lock);
        if (!locked) {
            auth_ok = auth[0] != '\0' && admin_auth_check_basic(auth);
            if (auth[0] != '\0') {
                xSemaphoreTake(s_guard_lock, portMAX_DELAY);
                slot = auth_slot(key);
                if (auth_ok) {
                    slot->fails = 0;
                } else if (++slot->fails >= HTTP_GUARD_AUTH_MAX_FAILS) {
                    slot->locked_until_ms = now_ms() + HTTP_GUARD_AUTH_LOCK_MS;
                }
                xSemaphoreGive(s_guard_lock);
            }
        }
    }
    if (!content_ok) {
        httpd_resp_set_status(req, "415 Unsupported Media Type");
        httpd_resp_set_type(req, "text/plain");
        err = httpd_resp_sendstr(req, "Unsupported content type");
    } else if (auth_decision == ADMIN_AUTH_DENY_UNAVAILABLE) {
        /* The stored credentials cannot be read: fail closed, never open. Recovery is a local one (docs/SECURITY.md). */
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_sendstr(req, "Admin credentials cannot be read; protected routes are closed. Erase the settings over USB to recover.");
    } else if (locked) {
        err = send_busy(req, "429 Too Many Requests", "Too many failed logins, try again later");
    } else if (!auth_ok) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"BETTA HA Panel\", charset=\"UTF-8\"");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_sendstr(req, "Authentication required");
    } else {
        err = next_handler(req);
    }
    xSemaphoreGive(s_active_sem);
    if (enforce_api_limits) {
        xSemaphoreTake(s_guard_lock, portMAX_DELAY);
        active_release(key);
        xSemaphoreGive(s_guard_lock);
    }
    return err;
}
