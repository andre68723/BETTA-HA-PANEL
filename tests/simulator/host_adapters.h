/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "ha/ha_model.h"
#include "ha/ha_energy_model.h"
#include "ha/ha_camera_stream.h"

unsigned sim_camera_active(void);
unsigned sim_camera_starts(void);
void sim_camera_status(ha_camera_stream_status_t value);
void sim_camera_sleep(bool value);

void sim_host_energy_set(const ha_energy_snapshot_t *snapshot);
void sim_host_state_add(const ha_state_t *state);
/* A fixture with "ha_online": true renders as a connected, synchronised panel (command buttons enabled). */
void sim_host_set_online(bool online);
typedef struct {
    unsigned count;
    char domain[32];
    char service[32];
    char data[256];
} sim_service_calls_t;
void sim_host_service_reset(esp_err_t result);
const sim_service_calls_t *sim_host_service_calls(void);
/* Values the slider widget handed to ui_bindings_set_slider_value(): how many, and the last one (-1 before any). */
unsigned sim_host_slider_sets(void);
int sim_host_slider_last(void);
