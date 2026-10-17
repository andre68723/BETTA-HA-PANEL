/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Optional admin password for the web editor and the HTTP API (HTTP Basic, user "admin").
 * Off by default. The password is stored as a salted, iterated SHA-256 hash in NVS, as one record ("cred": version, salt, hash).
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define ADMIN_AUTH_USER "admin"
#define ADMIN_AUTH_MIN_LEN 6
#define ADMIN_AUTH_MAX_LEN 64

/* What the stored credentials are. A password that was never set (or was cleared) is a different thing from credentials that cannot
 * be read: the first leaves the editor open, the second must not. */
typedef enum {
    ADMIN_AUTH_UNSET = 0,      /* no password: the editor and the API are open */
    ADMIN_AUTH_SET,            /* a valid password is stored */
    ADMIN_AUTH_UNAVAILABLE,    /* storage error or an inconsistent record: protected requests are refused */
} admin_auth_state_t;

typedef enum {
    ADMIN_AUTH_ALLOW = 0,      /* serve the request */
    ADMIN_AUTH_CHECK,          /* serve it only with correct credentials */
    ADMIN_AUTH_DENY_UNAVAILABLE, /* refuse: the credentials cannot be read (recovery: erase the settings over USB) */
} admin_auth_decision_t;

/* Load the stored credentials (call at start-up; safe to call again). Always ESP_OK: an unreadable record is a state, not a reason to
 * abort the boot, and it keeps the protected routes closed. */
esp_err_t admin_auth_init(void);

/* The state; while it is ADMIN_AUTH_UNAVAILABLE every call reads the storage again, so a transient fault heals by itself. */
admin_auth_state_t admin_auth_state(void);

/* True while protection is on: a password is set, or the credentials cannot be read (fail closed). */
bool admin_auth_enabled(void);

/* What the HTTP guard does with a request; public routes (the health check) stay reachable in every state. */
admin_auth_decision_t admin_auth_decide(admin_auth_state_t state, bool public_route);

/* Set a new password, or disable the protection with an empty string. */
esp_err_t admin_auth_set_password(const char *password);

/* True when the value of an "Authorization: Basic ..." header carries the right user and password. With no password set it is true; when
 * the credentials cannot be read it is false whatever the header says. */
bool admin_auth_check_basic(const char *header_value);
