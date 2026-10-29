/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(ha_model_init() == ESP_OK);
    ha_entity_info_t entity = {0}, listed = {0};
    snprintf(entity.id, sizeof(entity.id), "sensor.temperature");
    snprintf(entity.domain, sizeof(entity.domain), "sensor");
    snprintf(entity.name, sizeof(entity.name), "Legacy name");
    snprintf(entity.unit, sizeof(entity.unit), "°C");
    assert(ha_model_upsert_entity(&entity) == ESP_OK);
    char name[APP_MAX_NAME_LEN] = "unchanged";
    assert(!ha_model_get_entity_name("sensor.missing", name, sizeof(name)));
    assert(strcmp(name, "unchanged") == 0);
    assert(ha_model_get_entity_name(entity.id, name, sizeof(name)));
    assert(strcmp(name, "Legacy name") == 0);
    uint32_t revision = ha_model_state_revision();
    assert(ha_model_set_entity_name(entity.id, "Thermostat Temperature") == ESP_OK);
    assert(ha_model_state_revision() != revision);
    revision = ha_model_state_revision();
    assert(ha_model_set_entity_name(entity.id, "Thermostat Temperature") == ESP_OK);
    assert(ha_model_state_revision() == revision);
    /* Missing legacy names must not erase the discovery name; metadata still refreshes. */
    snprintf(entity.name, sizeof(entity.name), "%.*s", (int)sizeof(entity.name) - 1, entity.id);
    entity.supported_features = 42;
    assert(ha_model_upsert_entity(&entity) == ESP_OK);
    size_t total;
    assert(ha_model_list_entities_page("sensor", "thermostat", 0, &listed, 1, &total) == 1);
    assert(total == 1 && listed.supported_features == 42);
    assert(strcmp(listed.unit, "°C") == 0);
    assert(strcmp(listed.name, "Thermostat Temperature") == 0);
    assert(ha_model_set_entity_name(entity.id, "Renamed thermostat") == ESP_OK);
    assert(ha_model_get_entity_name(entity.id, name, sizeof(name)));
    assert(strcmp(name, "Renamed thermostat") == 0);
    /* While HA supplies friendly_name, real HA renames continue to apply live. */
    snprintf(entity.name, sizeof(entity.name), "Renamed in HA");
    assert(ha_model_upsert_entity(&entity) == ESP_OK);
    assert(ha_model_get_entity_name(entity.id, name, sizeof(name)));
    assert(strcmp(name, "Renamed in HA") == 0);
    /* Discovery before the first state retains its name when that state arrives. */
    assert(ha_model_set_entity_name("light.ceiling", "Ceiling") == ESP_OK);
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "light.ceiling");
    snprintf(state.state, sizeof(state.state), "on");
    assert(ha_model_upsert_state(&state) == ESP_OK);
    assert(ha_model_get_entity_name(state.entity_id, name, sizeof(name)));
    assert(strcmp(name, "Ceiling") == 0);
    assert(ha_model_set_entity_name(entity.id, "") == ESP_ERR_INVALID_ARG);
    ha_model_reset();
    assert(!ha_model_get_entity_name(entity.id, name, sizeof(name)));
    puts("entity names: fallback, discovery/state ordering, metadata, rename and reset passed");
    return 0;
}
