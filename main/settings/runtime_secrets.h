/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdint.h>
#include "settings/runtime_settings.h"

/* Generation zero reads upstream's legacy keys, or its staged migration snapshot.
 * Later generations never fall back to an unrelated secret record. The settings
 * store serializes these operations and publishes a generation only after staging. */
esp_err_t runtime_secrets_load(uint32_t generation, runtime_settings_t *settings);
esp_err_t runtime_secrets_stage(uint32_t generation, const runtime_settings_t *settings);
/* Changed connections receive empty legacy values, making older firmware fail closed after a schema rollback,
 * including when its compiled defaults contain credentials. */
esp_err_t runtime_secrets_retire_legacy(const runtime_settings_t *previous, const runtime_settings_t *next);
