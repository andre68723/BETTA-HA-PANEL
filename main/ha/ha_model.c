/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "util/log_tags.h"

static SemaphoreHandle_t s_model_mutex = NULL;
static ha_entity_info_t *s_entities = NULL;
static size_t s_entity_count = 0;
static ha_state_t *s_states = NULL;
static size_t s_state_count = 0;
static uint32_t s_state_revision = 0;

static void ha_model_free_buffers(void)
{
    if (s_entities != NULL) {
        heap_caps_free(s_entities);
        s_entities = NULL;
    }
    if (s_states != NULL) {
        heap_caps_free(s_states);
        s_states = NULL;
    }
    s_entity_count = 0;
    s_state_count = 0;
}

static void safe_copy_cstr(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, dst_size - 1U);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool contains_case_insensitive(const char *haystack, const char *needle)
{
    if (needle == NULL || needle[0] == '\0') {
        return true;
    }
    if (haystack == NULL) {
        return false;
    }

    size_t h_len = strlen(haystack);
    size_t n_len = strlen(needle);
    if (n_len > h_len) {
        return false;
    }

    for (size_t i = 0; i <= (h_len - n_len); i++) {
        bool matches = true;
        for (size_t j = 0; j < n_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }

    return false;
}

static void fill_domain(const char *entity_id, char *domain, size_t domain_len)
{
    if (entity_id == NULL || domain == NULL || domain_len == 0) {
        return;
    }
    const char *dot = strchr(entity_id, '.');
    if (dot == NULL) {
        snprintf(domain, domain_len, "unknown");
        return;
    }
    size_t len = (size_t)(dot - entity_id);
    if (len >= domain_len) {
        len = domain_len - 1U;
    }
    memcpy(domain, entity_id, len);
    domain[len] = '\0';
}

static bool ha_state_equals(const ha_state_t *lhs, const ha_state_t *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }

    return strncmp(lhs->entity_id, rhs->entity_id, sizeof(lhs->entity_id)) == 0 &&
           strncmp(lhs->state, rhs->state, sizeof(lhs->state)) == 0 &&
           strncmp(lhs->attributes_json, rhs->attributes_json, sizeof(lhs->attributes_json)) == 0 &&
           lhs->last_changed_unix_ms == rhs->last_changed_unix_ms;
}

esp_err_t ha_model_init(void)
{
    if (s_model_mutex != NULL) {
        return ESP_OK;
    }
    s_model_mutex = xSemaphoreCreateMutex();
    if (s_model_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_entities = (ha_entity_info_t *)heap_caps_calloc(
        APP_HA_MAX_ENTITIES, sizeof(ha_entity_info_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_entities == NULL) {
        s_entities =
            (ha_entity_info_t *)heap_caps_calloc(APP_HA_MAX_ENTITIES, sizeof(ha_entity_info_t), MALLOC_CAP_8BIT);
    }

    s_states = (ha_state_t *)heap_caps_calloc(APP_HA_MAX_STATES, sizeof(ha_state_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_states == NULL) {
        s_states = (ha_state_t *)heap_caps_calloc(APP_HA_MAX_STATES, sizeof(ha_state_t), MALLOC_CAP_8BIT);
    }

    if (s_entities == NULL || s_states == NULL) {
        ESP_LOGE(TAG_HA_MODEL, "Failed to allocate HA model buffers");
        ha_model_free_buffers();
        vSemaphoreDelete(s_model_mutex);
        s_model_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG_HA_MODEL, "Model buffers ready (%u entities, %u states)",
        (unsigned)APP_HA_MAX_ENTITIES, (unsigned)APP_HA_MAX_STATES);
    return ESP_OK;
}

void ha_model_reset(void)
{
    if (s_model_mutex == NULL || s_entities == NULL || s_states == NULL) {
        return;
    }
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    memset(s_entities, 0, sizeof(ha_entity_info_t) * APP_HA_MAX_ENTITIES);
    memset(s_states, 0, sizeof(ha_state_t) * APP_HA_MAX_STATES);
    s_entity_count = 0;
    s_state_count = 0;
    s_state_revision++;
    xSemaphoreGive(s_model_mutex);
}

static int find_entity_index(const char *entity_id)
{
    for (size_t i = 0; i < s_entity_count; i++) {
        if (strncmp(s_entities[i].id, entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_state_index(const char *entity_id)
{
    for (size_t i = 0; i < s_state_count; i++) {
        if (strncmp(s_states[i].entity_id, entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

esp_err_t ha_model_upsert_entity(const ha_entity_info_t *entity)
{
    if (s_model_mutex == NULL || s_entities == NULL || entity == NULL || entity->id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    int idx = find_entity_index(entity->id);
    if (idx >= 0) {
        char resolved_name[APP_MAX_NAME_LEN];
        bool keep_name = entity->name[0] == '\0' || strcmp(entity->name, entity->id) == 0;
        if (keep_name) safe_copy_cstr(resolved_name, sizeof(resolved_name), s_entities[idx].name);
        s_entities[idx] = *entity;
        if (keep_name) {
            safe_copy_cstr(s_entities[idx].name, sizeof(s_entities[idx].name), resolved_name);
        }
        xSemaphoreGive(s_model_mutex);
        return ESP_OK;
    }

    if (s_entity_count >= APP_HA_MAX_ENTITIES) {
        xSemaphoreGive(s_model_mutex);
        return ESP_ERR_NO_MEM;
    }
    s_entities[s_entity_count++] = *entity;
    xSemaphoreGive(s_model_mutex);
    return ESP_OK;
}

esp_err_t ha_model_set_entity_name(const char *entity_id, const char *name)
{
    if (s_model_mutex == NULL || s_entities == NULL || entity_id == NULL || entity_id[0] == '\0' ||
        name == NULL || name[0] == '\0') return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    int idx = find_entity_index(entity_id);
    if (idx < 0 && s_entity_count < APP_HA_MAX_ENTITIES) {
        idx = (int)s_entity_count++;
        safe_copy_cstr(s_entities[idx].id, sizeof(s_entities[idx].id), entity_id);
        fill_domain(entity_id, s_entities[idx].domain, sizeof(s_entities[idx].domain));
    }
    if (idx >= 0) {
        if (strcmp(s_entities[idx].name, name) != 0) s_state_revision++;
        safe_copy_cstr(s_entities[idx].name, sizeof(s_entities[idx].name), name);
    }
    xSemaphoreGive(s_model_mutex);
    return idx >= 0 ? ESP_OK : ESP_ERR_NO_MEM;
}

bool ha_model_get_entity_name(const char *entity_id, char *out, size_t out_len)
{
    if (s_model_mutex == NULL || s_entities == NULL || entity_id == NULL || out == NULL || out_len == 0) return false;
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    int idx = find_entity_index(entity_id);
    bool found = idx >= 0 && s_entities[idx].name[0] != '\0' && strcmp(s_entities[idx].name, entity_id) != 0;
    if (found) safe_copy_cstr(out, out_len, s_entities[idx].name);
    xSemaphoreGive(s_model_mutex);
    return found;
}

esp_err_t ha_model_upsert_state(const ha_state_t *state)
{
    if (s_model_mutex == NULL || s_entities == NULL || s_states == NULL || state == NULL || state->entity_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    int idx = find_state_index(state->entity_id);
    bool state_changed = false;
    if (idx >= 0) {
        if (ha_state_equals(&s_states[idx], state)) {
            xSemaphoreGive(s_model_mutex);
            return ESP_OK;
        }
        s_states[idx] = *state;
        state_changed = true;
    } else {
        if (s_state_count >= APP_HA_MAX_STATES) {
            xSemaphoreGive(s_model_mutex);
            return ESP_ERR_NO_MEM;
        }
        s_states[s_state_count++] = *state;
        state_changed = true;
    }

    if (find_entity_index(state->entity_id) < 0 && s_entity_count < APP_HA_MAX_ENTITIES) {
        ha_entity_info_t entity = {0};
        safe_copy_cstr(entity.id, sizeof(entity.id), state->entity_id);
        safe_copy_cstr(entity.name, sizeof(entity.name), state->entity_id);
        fill_domain(state->entity_id, entity.domain, sizeof(entity.domain));
        s_entities[s_entity_count++] = entity;
    }

    if (state_changed) {
        s_state_revision++;
    }

    xSemaphoreGive(s_model_mutex);
    return ESP_OK;
}

bool ha_model_get_state(const char *entity_id, ha_state_t *out_state)
{
    if (s_model_mutex == NULL || s_states == NULL || entity_id == NULL || out_state == NULL) {
        return false;
    }
    bool found = false;
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    int idx = find_state_index(entity_id);
    if (idx >= 0) {
        *out_state = s_states[idx];
        found = true;
    }
    xSemaphoreGive(s_model_mutex);
    return found;
}

size_t ha_model_list_entities(
    const char *domain_filter, const char *search, ha_entity_info_t *out_entities, size_t max_out)
{
    return ha_model_list_entities_page(domain_filter, search, 0, out_entities, max_out, NULL);
}

size_t ha_model_list_entities_page(const char *domain_filter, const char *search, size_t offset,
    ha_entity_info_t *out_entities, size_t max_out, size_t *total)
{
    if (total != NULL) {
        *total = 0;
    }
    if (s_model_mutex == NULL || s_entities == NULL || out_entities == NULL || max_out == 0) {
        return 0;
    }
    size_t written = 0;
    size_t matches = 0;

    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    for (size_t i = 0; i < s_entity_count; i++) {
        const ha_entity_info_t *entity = &s_entities[i];
        if (domain_filter != NULL && domain_filter[0] != '\0' &&
            strncmp(domain_filter, entity->domain, sizeof(entity->domain)) != 0) {
            continue;
        }
        if (!contains_case_insensitive(entity->id, search) && !contains_case_insensitive(entity->name, search)) {
            continue;
        }
        if (matches++ >= offset && written < max_out) {
            out_entities[written++] = *entity;
        } else if (total == NULL && written >= max_out) {
            break;
        }
    }
    xSemaphoreGive(s_model_mutex);
    if (total != NULL) {
        *total = matches;
    }
    return written;
}

size_t ha_model_list_states(ha_state_t *out_states, size_t max_out)
{
    return ha_model_list_states_page(0, out_states, max_out, NULL);
}

size_t ha_model_list_states_page(size_t offset, ha_state_t *out_states, size_t max_out, size_t *total)
{
    if (total != NULL) {
        *total = 0;
    }
    if (s_model_mutex == NULL || s_states == NULL || out_states == NULL || max_out == 0) {
        return 0;
    }

    size_t written = 0;
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    for (size_t i = offset; i < s_state_count && written < max_out; i++) {
        out_states[written++] = s_states[i];
    }
    if (total != NULL) {
        *total = s_state_count;
    }
    xSemaphoreGive(s_model_mutex);

    return written;
}

uint32_t ha_model_state_revision(void)
{
    if (s_model_mutex == NULL) {
        return 0;
    }

    uint32_t revision = 0;
    xSemaphoreTake(s_model_mutex, portMAX_DELAY);
    revision = s_state_revision;
    xSemaphoreGive(s_model_mutex);
    return revision;
}
