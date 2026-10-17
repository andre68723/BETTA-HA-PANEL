/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "layout/bambu_printer_slots.h"

#include <string.h>

const bambu_printer_binding_t k_bambu_printer_bindings[] = {
    {"entity_id", BAMBU_SLOT_PRIMARY, "sensor", false, "print_status"},
    {"nozzle_temp_entity_id", BAMBU_SLOT_SECONDARY, "sensor", false, "nozzle_temp"},
    {"bed_temp_entity_id", BAMBU_SLOT_TERTIARY, "sensor", false, "bed_temp"},
    {"progress_entity_id", UI_BAMBU_EXTRA_PROGRESS, "sensor", false, "print_progress"},
    {"remaining_time_entity_id", UI_BAMBU_EXTRA_REMAINING, "sensor", false, "remaining_time"},
    {"pause_entity_id", UI_BAMBU_EXTRA_PAUSE, "button", true, "pause"},
    {"resume_entity_id", UI_BAMBU_EXTRA_RESUME, "button", true, "resume"},
    {"stop_entity_id", UI_BAMBU_EXTRA_STOP, "button", true, "stop"},
    {"stage_entity_id", UI_BAMBU_EXTRA_STAGE, "sensor", false, "stage"},
    {"job_name_entity_id", UI_BAMBU_EXTRA_JOB, "sensor", false, "subtask_name"},
    {"camera_entity_id", UI_BAMBU_EXTRA_CAMERA, "camera", false, "camera"},
};
const size_t k_bambu_printer_binding_count = sizeof(k_bambu_printer_bindings) / sizeof(k_bambu_printer_bindings[0]);

void bambu_printer_slots_from_json(const cJSON *widget, char *primary, char *secondary, char *tertiary,
                                   char (*extra)[APP_MAX_ENTITY_ID_LEN], size_t extra_count)
{
    if (widget == NULL) {
        return;
    }
    for (size_t i = 0; i < k_bambu_printer_binding_count; i++) {
        const bambu_printer_binding_t *binding = &k_bambu_printer_bindings[i];
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(widget, binding->key);
        if (!cJSON_IsString(item) || item->valuestring == NULL || strlen(item->valuestring) >= APP_MAX_ENTITY_ID_LEN) {
            continue;
        }
        char *dst = NULL;
        if (binding->slot == BAMBU_SLOT_PRIMARY) {
            dst = primary;
        } else if (binding->slot == BAMBU_SLOT_SECONDARY) {
            dst = secondary;
        } else if (binding->slot == BAMBU_SLOT_TERTIARY) {
            dst = tertiary;
        } else if (binding->slot >= 0 && (size_t)binding->slot < extra_count && extra != NULL) {
            dst = extra[binding->slot];
        }
        if (dst != NULL) {
            memcpy(dst, item->valuestring, strlen(item->valuestring) + 1U);
        }
    }
}
