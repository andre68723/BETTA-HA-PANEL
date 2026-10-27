/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/cover_request_lifetime.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    int media, camera;
    cover_request_lifetime_t *head = NULL;
    cover_request_lifetime_t queued, decoding, dispatched, other, replacement;
    cover_request_lifetime_add(&head, &queued, &media);
    cover_request_lifetime_add(&head, &decoding, &media);
    cover_request_lifetime_add(&head, &dispatched, &media);
    cover_request_lifetime_add(&head, &other, &camera);
    queued.camera = true;
    cover_request_lifetime_cancel_cameras(head);
    assert(queued.cancelled && !decoding.cancelled && !other.cancelled);
    cover_request_lifetime_cancel(head, &media);
    assert(queued.cancelled && decoding.cancelled && dispatched.cancelled && !other.cancelled);
    /* Destroying another widget must not forget earlier cancelled callbacks. */
    cover_request_lifetime_cancel(head, &camera);
    assert(dispatched.cancelled && other.cancelled);
    /* Allocator reuses the widget's address before the old callback runs. */
    cover_request_lifetime_add(&head, &replacement, &media);
    assert(!replacement.cancelled && dispatched.cancelled && decoding.cancelled);
    cover_request_lifetime_remove(&head, &decoding);
    cover_request_lifetime_remove(&head, &replacement);
    cover_request_lifetime_remove(&head, &queued);
    assert(head == &other && other.next == &dispatched);
    cover_request_lifetime_remove(&head, &other);
    cover_request_lifetime_remove(&head, &dispatched);
    assert(head == NULL);
    puts("cover request lifetime tests passed");
}
