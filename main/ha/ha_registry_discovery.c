/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_registry_discovery.h"
#include <stdio.h>
#include <string.h>

static const char *string(const cJSON *object, const char *key)
{
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
    return value != NULL ? value : "";
}

static const ha_registry_device_t *find(const ha_registry_device_t *devices, size_t count, const char *id)
{
    for (size_t i = 0; devices != NULL && id != NULL && id[0] != '\0' && i < count; i++) {
        if (strcmp(devices[i].id, id) == 0) return &devices[i];
    }
    return NULL;
}

bool ha_registry_device_parse(const cJSON *entry, ha_registry_device_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    const char *id = string(entry, "id");
    if (id[0] == '\0' || strlen(id) >= sizeof(out->id)) return false;
    snprintf(out->id, sizeof(out->id), "%s", id);
    snprintf(out->area_id, sizeof(out->area_id), "%s", string(entry, "area_id"));
    snprintf(out->parent_id, sizeof(out->parent_id), "%s", string(entry, "parent_device_id"));
    const char *name = string(entry, "name_by_user");
    snprintf(out->name, sizeof(out->name), "%s", name[0] != '\0' ? name : string(entry, "name"));
    return true;
}

const char *ha_registry_device_area(const ha_registry_device_t *devices, size_t count, const char *id)
{
    /* At most count links: malformed cycles never block discovery. */
    for (size_t links = 0; links < count; links++) {
        const ha_registry_device_t *device = find(devices, count, id);
        if (device == NULL) break;
        if (device->area_id[0] != '\0') return device->area_id;
        id = device->parent_id;
    }
    return "";
}

void ha_registry_name_parse(const cJSON *entry, bool display, ha_registry_name_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, display ? "en" : "name");
    if (display) {
        out->prefix_device = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "hn")) ||
            strcmp(string(entry, "np"), "device") == 0 || string(entry, "en")[0] == '\0';
    } else {
        /* An explicit empty override means device name only, not original_name. */
        out->prefix_device = !cJSON_IsString(name) || string(entry, "name")[0] == '\0';
        if (!cJSON_IsString(name)) {
            name = cJSON_GetObjectItemCaseSensitive(entry, "original_name_unprefixed");
            if (!cJSON_IsString(name)) name = cJSON_GetObjectItemCaseSensitive(entry, "original_name");
        }
    }
    const char *text = cJSON_GetStringValue(name);
    snprintf(out->text, sizeof(out->text), "%s", text != NULL ? text : "");
}

void ha_registry_name_resolve(const ha_registry_name_t *name, const char *entity_id, const char *device_id,
    const ha_registry_device_t *devices, size_t count, char *out, size_t out_len)
{
    if (name == NULL || out == NULL || out_len == 0 || entity_id == NULL) return;
    const ha_registry_device_t *device = find(devices, count, device_id);
    const char *prefix = name->prefix_device && device != NULL ? device->name : "";
    const char *text = name->text;
    const size_t prefix_len = strlen(prefix);
    /* Legacy original names may already contain the device prefix. */
    if (prefix_len > 0 && strncmp(text, prefix, prefix_len) == 0 &&
        (text[prefix_len] == ' ' || text[prefix_len] == '\0')) prefix = "";
    snprintf(out, out_len, "%s%s%s", prefix, prefix[0] != '\0' && text[0] != '\0' ? " " : "", text);
    if (out_len > 0 && out[0] == '\0') snprintf(out, out_len, "%s", entity_id);
}
