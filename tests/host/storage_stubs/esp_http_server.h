/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stddef.h>
typedef struct { int content_len; } httpd_req_t;
#define HTTPD_SOCK_ERR_TIMEOUT -3
int httpd_req_recv(httpd_req_t *, char *, size_t);
