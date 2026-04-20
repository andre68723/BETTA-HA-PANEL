/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ha/ha_energy_model.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_energy_mutex = NULL;
static ha_energy_snapshot_t s_energy_snapshot = {0};
static uint32_t s_energy_revision = 0;

esp_err_t ha_energy_model_init(void)
{
    if (s_energy_mutex != NULL) {
        return ESP_OK;
    }
    s_energy_mutex = xSemaphoreCreateMutex();
    if (s_energy_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_energy_snapshot, 0, sizeof(s_energy_snapshot));
    s_energy_revision = 0;
    return ESP_OK;
}

void ha_energy_model_reset(void)
{
    if (s_energy_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_energy_mutex, portMAX_DELAY);
    memset(&s_energy_snapshot, 0, sizeof(s_energy_snapshot));
    s_energy_revision++;
    xSemaphoreGive(s_energy_mutex);
}

void ha_energy_model_update(const ha_energy_snapshot_t *snapshot)
{
    if (s_energy_mutex == NULL || snapshot == NULL) {
        return;
    }
    xSemaphoreTake(s_energy_mutex, portMAX_DELAY);
    s_energy_snapshot = *snapshot;
    s_energy_revision++;
    xSemaphoreGive(s_energy_mutex);
}

bool ha_energy_model_get_snapshot(ha_energy_snapshot_t *out_snapshot)
{
    if (s_energy_mutex == NULL || out_snapshot == NULL) {
        return false;
    }
    xSemaphoreTake(s_energy_mutex, portMAX_DELAY);
    *out_snapshot = s_energy_snapshot;
    bool available = s_energy_snapshot.available;
    xSemaphoreGive(s_energy_mutex);
    return available;
}

uint32_t ha_energy_model_revision(void)
{
    if (s_energy_mutex == NULL) {
        return 0;
    }
    xSemaphoreTake(s_energy_mutex, portMAX_DELAY);
    uint32_t revision = s_energy_revision;
    xSemaphoreGive(s_energy_mutex);
    return revision;
}
