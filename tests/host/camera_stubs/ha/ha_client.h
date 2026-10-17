/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once
#include "camera_test_platform.h"
typedef struct { char base_url[256], host_header[192], cert_common_name[128], bearer_token[512]; } ha_client_http_ctx_t;
bool ha_client_is_connected(void);
bool ha_client_heavy_gate_is_busy(void);
bool ha_client_get_http_context(ha_client_http_ctx_t *);
