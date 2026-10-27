/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>

/* Caller serializes this list. Each outstanding request owns a distinct node,
 * retained through async dispatch even if the consumer address is reused. */
typedef struct cover_request_lifetime {
    struct cover_request_lifetime *next;
    void *user;
    bool cancelled;
    bool camera;
} cover_request_lifetime_t;
void cover_request_lifetime_add(cover_request_lifetime_t **head, cover_request_lifetime_t *node, void *user);
void cover_request_lifetime_cancel(cover_request_lifetime_t *head, void *user);
void cover_request_lifetime_cancel_cameras(cover_request_lifetime_t *head);
void cover_request_lifetime_remove(cover_request_lifetime_t **head, cover_request_lifetime_t *node);
