/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_discovery_domains.h"

#include <stdio.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Domains the editor may request directly (entity pickers per widget type). */
static const char *const s_domains[] = {
    "light", "sensor", "switch", "weather", "climate", "todo", "media_player", "vacuum", "cover", "fan", "camera",
    "number", "input_boolean", "binary_sensor", "select", "input_select", "input_datetime", "time", "date", "datetime", "image", "scene", "script", "button", "event", "lock", "alarm_control_panel", "input_button",
    "room", /* pseudo domain: the entities of a room card, per area */
    "any",  /* pseudo domain: every entity the tiles can show (add from an entity) */
};

/* "any": the domains of the widget types, for adding a widget from an entity. */
static const char *const s_any_domains[] = {
    "sensor", "binary_sensor", "light", "cover", "fan", "climate", "media_player", "lock", "alarm_control_panel", "switch",
    "input_boolean", "weather", "vacuum", "camera", "image", "event", "scene", "script", "button", "input_button", "todo",
    "select", "input_select", "input_datetime", "time", "date", "datetime",
};

/* "room": what a room card can show. The template also narrows sensors and binary sensors to
 * the device classes a room card uses. */
static const char *const s_room_domains[] = {
    "sensor", "binary_sensor", "light", "cover", "fan", "climate", "media_player", "lock", "switch", "vacuum",
};

static bool in_list(const char *const *list, size_t count, const char *domain)
{
    for (size_t i = 0; domain != NULL && i < count; i++) {
        if (strcmp(list[i], domain) == 0) {
            return true;
        }
    }
    return false;
}

static bool entity_in_domain(const char *entity_id, const char *domain)
{
    const size_t n = strlen(domain);
    return strncmp(entity_id, domain, n) == 0 && entity_id[n] == '.';
}

static bool entity_in_list(const char *entity_id, const char *const *list, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (entity_in_domain(entity_id, list[i])) {
            return true;
        }
    }
    return false;
}

bool ha_discovery_domain_supported(const char *domain)
{
    return in_list(s_domains, COUNT(s_domains), domain);
}

const char *ha_discovery_domain_or_default(const char *domain)
{
    return ha_discovery_domain_supported(domain) ? domain : "light";
}

bool ha_discovery_entity_matches(const char *entity_id, const char *domain)
{
    const char *checked = ha_discovery_domain_or_default(domain);
    if (entity_id == NULL || entity_id[0] == '\0') {
        return false;
    }
    if (strcmp(checked, "any") == 0) {
        return entity_in_list(entity_id, s_any_domains, COUNT(s_any_domains));
    }
    if (strcmp(checked, "room") == 0) {
        return entity_in_list(entity_id, s_room_domains, COUNT(s_room_domains));
    }
    return entity_in_domain(entity_id, checked);
}

bool ha_discovery_domains_jinja(const char *pseudo, char *out, size_t out_len)
{
    const char *const *list = NULL;
    size_t count = 0;
    if (pseudo != NULL && strcmp(pseudo, "any") == 0) {
        list = s_any_domains;
        count = COUNT(s_any_domains);
    } else if (pseudo != NULL && strcmp(pseudo, "room") == 0) {
        list = s_room_domains;
        count = COUNT(s_room_domains);
    }
    if (list == NULL || out == NULL || out_len == 0) {
        return false;
    }
    size_t len = 0;
    for (size_t i = 0; i < count; i++) {
        const int n = snprintf(out + len, out_len - len, "%s'%s'", i == 0 ? "[" : ",", list[i]);
        if (n < 0 || (size_t)n >= out_len - len) {
            return false;
        }
        len += (size_t)n;
    }
    const int n = snprintf(out + len, out_len - len, "]");
    return n >= 0 && (size_t)n < out_len - len;
}
