/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_entity_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    /* Export the exact production query for HA integration validation. */
    if (argc > 1) {
        if (strcmp(argv[1], "--name-template") == 0) {
            printf(HA_DISCOVERY_DISPLAY_NAME);
            return 0;
        }
        char *query = ha_entity_source_template(argv[1], "\"\"", 0, 25);
        assert(query != NULL);
        puts(query);
        free(query);
        return 0;
    }
    const char *domains[] = {"sensor", "media_player", "light", "any", "room"};
    for (size_t i = 0; i < sizeof(domains) / sizeof(domains[0]); i++) {
        char *query = ha_entity_source_template(domains[i], "\"quote\\\"\\n\"", 25, 50);
        assert(query != NULL && strlen(query) < 4096);
        assert(strstr(query, "{% set offset = 25 %}") != NULL);
        assert(strstr(query, "integration_entities('easy_stock')") != NULL);
        assert(strstr(query, "xf=") != NULL && strstr(query, "ft=") != NULL);
        assert(strstr(query, "s.name") == NULL);
        assert(strstr(query, "entity_name(s.entity_id)") != NULL);
        free(query);
    }
    cJSON *raw = cJSON_Parse("{\"xf\":129,\"ft\":4,\"dm\":true}");
    ha_entity_search_meta_t meta;
    ha_entity_search_parse(raw, &meta);
    assert(meta.flags == 129 && meta.features_known && meta.features == 4 && meta.dimming);
    cJSON *output = cJSON_CreateObject();
    ha_entity_search_json(output, &meta);
    assert(cJSON_GetObjectItemCaseSensitive(output, "search_flags")->valueint == 129);
    assert(cJSON_GetObjectItemCaseSensitive(output, "supported_features")->valueint == 4);
    cJSON_Delete(raw); cJSON_Delete(output);
    raw = cJSON_Parse("{\"xf\":-1,\"ft\":4294967296}");
    ha_entity_search_parse(raw, &meta);
    assert(meta.flags == 0 && !meta.features_known && !meta.dimming);
    output = cJSON_CreateObject(); ha_entity_search_json(output, &meta);
    assert(cJSON_GetObjectItemCaseSensitive(output, "supported_features") == NULL);
    cJSON_Delete(raw); cJSON_Delete(output);
    puts("entity search: query bounds, metadata round-trip and invalid features passed");
    return 0;
}
