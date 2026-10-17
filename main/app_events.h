/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "util/chime_synth.h"

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
    EV_DISPLAY_ROTATION,
    EV_DISPLAY_PROTECTION_UPDATED,
    EV_UI_NAVIGATE,
    EV_UI_TAP,
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
    chime_kind_t sound; /* optional; existing payloads stay silent */
    /* Optional action button (for example "Open door"): the entity is pressed / unlocked / turned on on tap. */
    char action_entity_id[APP_MAX_ENTITY_ID_LEN];
    char action_label[APP_NOTIFICATION_MAX_ACTION_LABEL_LEN];
    /* A lock action unlatches the door (lock.open) instead of retracting the bolt (lock.unlock):
     * "action_service": "open" in the payload, for locks that support it (Nuki and the like). */
    bool action_open;
    /* Optional camera: its snapshot is shown in the card and refreshed while the card is open. */
    char camera_entity_id[APP_MAX_ENTITY_ID_LEN];
    /* Set by the REST/MQTT parser: the popup keeps the last such message so a double tap on the clock can show it
     * again. Alerts and system notices leave it false. */
    bool remember;
} app_notification_t;

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
} app_event_state_changed_t;

typedef struct {
    char page_id[APP_MAX_PAGE_ID_LEN];
} app_event_navigate_t;

typedef struct {
    char widget_id[APP_MAX_WIDGET_ID_LEN]; /* empty with close: dismiss the popup the last tap opened */
    bool close;
} app_event_tap_t;

typedef struct {
    app_notification_t *notification;
} app_event_notification_t;

typedef struct {
    app_event_type_t type;
    union {
        app_event_state_changed_t ha_state_changed;
        app_event_navigate_t navigate;
        app_event_tap_t tap;
        int rotation_degrees;
        app_event_notification_t notification;
    } data;
} app_event_t;

esp_err_t app_events_init(void);
QueueHandle_t app_events_get_queue(void);
bool app_events_publish(const app_event_t *event, TickType_t timeout_ticks);
/* Successful layout publishes coalesce into a revision, including before queue initialization. */
uint32_t app_events_layout_revision(void);
bool app_events_receive(app_event_t *event, TickType_t timeout_ticks);
