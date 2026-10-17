/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
typedef struct { char version[32]; char project_name[32]; char date[16]; char time[16]; } esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
