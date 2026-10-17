/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/notification_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "app_config.h"
#include "ui/notification_text.h"
#include "util/entity_id.h"

/* Identifiers and ids: cut at a UTF-8 character boundary. */
static void copy_utf8_bounded(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1U;
        while (len > 0U && ((unsigned char)src[len] & 0xC0U) == 0x80U) {
            len--; /* src[len] continues a character that began before it: leave that character out */
        }
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void copy_json_string(const cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        copy_utf8_bounded(dst, dst_size, item->valuestring);
    }
}

/* Display text (title, message, action label): cut before a whole emoji unit that does not fit, so a field never ends in half a sequence. */
static void copy_text_field(const cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        notif_text_copy(dst, dst_size, item->valuestring);
    }
}

void notification_sanitize_entities(app_notification_t *notification)
{
    if (notification == NULL) {
        return;
    }
    /* An action entity ends up in a service call: only a plain id in a domain an action may act on. */
    if (notification->action_entity_id[0] != '\0' && !entity_id_action_allowed(notification->action_entity_id)) {
        notification->action_entity_id[0] = '\0';
        notification->action_label[0] = '\0';
    }
    if (!entity_id_in_domain(notification->action_entity_id, "lock")) {
        notification->action_open = false;
    }
    if (notification->camera_entity_id[0] != '\0' && !entity_id_in_domain(notification->camera_entity_id, "camera")) {
        notification->camera_entity_id[0] = '\0';
    }
}

bool notification_parse_json(const char *json, size_t len, app_notification_t **out, const char **err_msg)
{
    (void)len;

    if (out == NULL) {
        return false;
    }
    *out = NULL;
    if (err_msg != NULL) {
        *err_msg = NULL;
    }

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        if (err_msg != NULL) {
            *err_msg = "Invalid JSON";
        }
        return false;
    }

    const cJSON *message_item = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!cJSON_IsString(message_item) || message_item->valuestring == NULL ||
        message_item->valuestring[0] == '\0') {
        cJSON_Delete(root);
        if (err_msg != NULL) {
            *err_msg = "Missing message";
        }
        return false;
    }

    app_notification_t *notification = calloc(1, sizeof(app_notification_t));
    if (notification == NULL) {
        cJSON_Delete(root);
        /* err_msg stays NULL: signals an internal/allocation failure rather
         * than a bad payload. */
        return false;
    }

    notification->remember = true; /* REST and MQTT only: see app_notification_t */
    notif_text_copy(notification->message, sizeof(notification->message), message_item->valuestring);
    copy_text_field(root, "title", notification->title, sizeof(notification->title));
    copy_json_string(root, "message_id", notification->message_id, sizeof(notification->message_id));
    copy_json_string(root, "action_entity_id", notification->action_entity_id, sizeof(notification->action_entity_id));
    copy_text_field(root, "action_label", notification->action_label, sizeof(notification->action_label));
    copy_json_string(root, "camera_entity_id", notification->camera_entity_id, sizeof(notification->camera_entity_id));
    /* "unlock" (default) or "open", for a lock action only. */
    const cJSON *service_item = cJSON_GetObjectItemCaseSensitive(root, "action_service");
    if (service_item != NULL) {
        const bool open = cJSON_IsString(service_item) && strcmp(service_item->valuestring, "open") == 0;
        const bool unlock = cJSON_IsString(service_item) && strcmp(service_item->valuestring, "unlock") == 0;
        if ((!open && !unlock) || !entity_id_in_domain(notification->action_entity_id, "lock")) {
            free(notification);
            cJSON_Delete(root);
            if (err_msg != NULL) {
                *err_msg = "action_service must be \"unlock\" or \"open\" and needs a lock.* action_entity_id";
            }
            return false;
        }
        notification->action_open = open;
    }
    const cJSON *sound = cJSON_GetObjectItemCaseSensitive(root, "sound");
    if (sound && (!cJSON_IsString(sound) || !chime_kind_parse(sound->valuestring, &notification->sound))) {
        free(notification);
        cJSON_Delete(root);
        if (err_msg) *err_msg = "sound must be chime, notify or none";
        return false;
    }
    notification_sanitize_entities(notification);

    const cJSON *timeout_item = cJSON_GetObjectItemCaseSensitive(root, "timeout_sec");
    if (cJSON_IsNumber(timeout_item)) {
        double timeout = timeout_item->valuedouble;
        if (timeout < 0.0) {
            timeout = 0.0;
        }
        if (timeout > (double)APP_NOTIFICATION_MAX_TIMEOUT_SEC) {
            timeout = (double)APP_NOTIFICATION_MAX_TIMEOUT_SEC;
        }
        notification->timeout_sec = (uint32_t)timeout;
    }
    /* "priority" is accepted but intentionally ignored in the first version. */
    cJSON_Delete(root);

    *out = notification;
    return true;
}
