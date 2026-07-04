/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "app_config.h"

typedef enum {
    EV_NONE = 0,
    EV_HA_CONNECTED,
    EV_HA_DISCONNECTED,
    EV_HA_STATE_CHANGED,
    EV_HA_ENERGY_CHANGED,
    EV_LAYOUT_UPDATED,
    EV_UI_NAVIGATE,
    EV_NOTIFICATION_SHOW,
} app_event_type_t;

/* Heap-allocated notification payload. Kept out of the event union by design:
 * the queue preallocates APP_EVENT_QUEUE_LENGTH * sizeof(app_event_t), so the
 * union must stay small. Ownership transfers to the UI task once the event is
 * published successfully; on publish failure the sender must free it. */
typedef struct {
    char title[APP_NOTIFICATION_MAX_TITLE_LEN];
    char message[APP_NOTIFICATION_MAX_MESSAGE_LEN];
    char message_id[APP_NOTIFICATION_MAX_ID_LEN];
    uint32_t timeout_sec; /* 0 = persistent until tapped */
} app_notification_t;

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
} app_event_state_changed_t;

typedef struct {
    char page_id[APP_MAX_PAGE_ID_LEN];
} app_event_navigate_t;

typedef struct {
    app_notification_t *notification;
} app_event_notification_t;

typedef struct {
    app_event_type_t type;
    union {
        app_event_state_changed_t ha_state_changed;
        app_event_navigate_t navigate;
        app_event_notification_t notification;
    } data;
} app_event_t;

esp_err_t app_events_init(void);
QueueHandle_t app_events_get_queue(void);
bool app_events_publish(const app_event_t *event, TickType_t timeout_ticks);
bool app_events_receive(app_event_t *event, TickType_t timeout_ticks);
