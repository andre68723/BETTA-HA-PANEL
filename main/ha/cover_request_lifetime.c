/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/cover_request_lifetime.h"
#include <stddef.h>

void cover_request_lifetime_add(cover_request_lifetime_t **head, cover_request_lifetime_t *node, void *user)
{
    *node = (cover_request_lifetime_t){.next = *head, .user = user};
    *head = node;
}
void cover_request_lifetime_cancel(cover_request_lifetime_t *head, void *user)
{
    for (; head; head = head->next)
        if (head->user == user) head->cancelled = true;
}
void cover_request_lifetime_cancel_cameras(cover_request_lifetime_t *head)
{
    for (; head; head = head->next)
        if (head->camera) head->cancelled = true;
}
void cover_request_lifetime_remove(cover_request_lifetime_t **head, cover_request_lifetime_t *node)
{
    for (; *head; head = &(*head)->next) {
        if (*head == node) { *head = node->next; node->next = NULL; return; }
    }
}
