/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * When the panel may install a firmware update by itself (the optional "Install updates automatically" setting).
 * Pure decision over what the caller measured, so the host tests cover every reason to wait. The install itself and
 * the setting are separate (docs/development.md, "OTA rollback and verification").
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The install window is 03:00 to 04:00 local time; each panel starts at its own minute in it (auto_update_start_offset_min). */
#define AUTO_UPDATE_WINDOW_START_MIN 180
#define AUTO_UPDATE_WINDOW_END_MIN 240
/* Nobody touched the panel or used the editor for this long. */
#define AUTO_UPDATE_IDLE_MIN_S 600
/* Weaker than this the download would drag on and the update would fail half way. */
#define AUTO_UPDATE_MIN_RSSI_DBM (-75)
/* Free memory a TLS download needs on top of what the running dashboard uses (measured: about 320 KB free internal after boot). */
#define AUTO_UPDATE_MIN_FREE_INTERNAL (120U * 1024U)
#define AUTO_UPDATE_MIN_LARGEST_INTERNAL (60U * 1024U)
#define AUTO_UPDATE_MIN_FREE_PSRAM (4U * 1024U * 1024U)

typedef enum {
    AUTO_UPDATE_GO = 0,
    AUTO_UPDATE_WAIT_DISABLED,        /* the setting is off */
    AUTO_UPDATE_WAIT_BUSY,            /* another update is running */
    AUTO_UPDATE_WAIT_SAFE_MODE,       /* the panel started with the built-in layout after a crash loop */
    AUTO_UPDATE_WAIT_NO_UPDATE,       /* the update check found nothing newer */
    AUTO_UPDATE_WAIT_BLOCKED_VERSION, /* this version was installed before and rolled back */
    AUTO_UPDATE_WAIT_TRIED_TODAY,     /* an install was tried today; a failed one is tried again tomorrow at the earliest */
    AUTO_UPDATE_WAIT_NO_SHA256,       /* the release carries no SHA-256 digest to verify the download */
    AUTO_UPDATE_WAIT_NO_ROLLBACK,     /* the bootloader cannot roll back: a bad image would leave the panel stuck */
    AUTO_UPDATE_WAIT_TIME_UNKNOWN,    /* the clock is not set */
    AUTO_UPDATE_WAIT_OUTSIDE_WINDOW,  /* not between 03:00 and 04:00 */
    AUTO_UPDATE_WAIT_START_MINUTE,    /* in the window, but before this panel's own start minute */
    AUTO_UPDATE_WAIT_ACTIVITY,        /* somebody used the panel or the editor recently */
    AUTO_UPDATE_WAIT_WIFI,            /* no Wi-Fi, or a weak signal */
    AUTO_UPDATE_WAIT_STORAGE,         /* persistent attempt state unavailable */
    AUTO_UPDATE_WAIT_MEMORY           /* too little free RAM for the download */
} auto_update_decision_t;

typedef struct {
    bool enabled;               /* the "Install updates automatically" setting */
    bool update_available;      /* the update check found a newer release (it already follows "Include pre-releases") */
    bool sha256_known;          /* that release has a SHA-256 digest */
    bool bootloader_rollback;   /* /api/version bootloader.rollback */
    bool tried_today;           /* the last attempt was on today's date */
    bool busy;                  /* an OTA install is running */
    bool safe_mode;
    bool time_valid;
    int local_minute_of_day;    /* 0..1439 in the panel's time zone */
    int start_offset_min;       /* 0..59, auto_update_start_offset_min() */
    int64_t idle_s;             /* seconds since the last touch or editor request */
    bool wifi_connected;
    int rssi_dbm;               /* negative; only read when wifi_connected */
    size_t free_internal;
    size_t largest_internal;
    size_t free_psram;
    const char *release_tag;    /* the release found, "v0.9.0-rc4" */
    const char *blocked_tag;    /* a version that was installed and rolled back, "" for none */
} auto_update_input_t;

auto_update_decision_t auto_update_decide(const auto_update_input_t *in);

/* Short stable name for logs and /api/ota/status ("go", "wait_activity", ...). */
const char *auto_update_decision_name(auto_update_decision_t decision);

/* A panel's own start minute (0..59) in the window on a given day: a hash of the device salt (for example the MAC) and the day number,
 * so panels do not all hit GitHub in the same minute and one panel does not always go first. */
int auto_update_start_offset_min(uint32_t device_salt, int32_t day_number);
