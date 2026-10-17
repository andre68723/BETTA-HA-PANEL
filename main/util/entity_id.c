/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/entity_id.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "app_config.h"

bool entity_id_valid(const char *entity_id)
{
    if (entity_id == NULL) {
        return false;
    }
    const size_t len = strlen(entity_id);
    if (len < 3 || len >= APP_MAX_ENTITY_ID_LEN) {
        return false;
    }
    const char *dot = strchr(entity_id, '.');
    if (dot == NULL || dot == entity_id || *(dot + 1) == '\0' || strchr(dot + 1, '.') != NULL) {
        return false;
    }
    for (const char *p = entity_id; *p != '\0'; p++) {
        if (*p == '.') {
            continue;
        }
        if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_')) {
            return false;
        }
    }
    return true;
}

bool entity_id_in_domain(const char *entity_id, const char *domain)
{
    if (!entity_id_valid(entity_id) || domain == NULL) {
        return false;
    }
    const size_t n = strlen(domain);
    return strncmp(entity_id, domain, n) == 0 && entity_id[n] == '.';
}

bool entity_id_action_allowed(const char *entity_id)
{
    static const char *const domains[] = {"button", "input_button", "lock", "scene", "script", "switch", "light",
        "input_boolean", "fan"};
    for (size_t i = 0; i < sizeof(domains) / sizeof(domains[0]); i++) {
        if (entity_id_in_domain(entity_id, domains[i])) {
            return true;
        }
    }
    return false;
}
