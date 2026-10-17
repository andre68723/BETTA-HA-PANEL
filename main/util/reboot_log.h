/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Reboot history: why the panel restarted, kept across restarts. esp_reset_reason() only describes the boot you are in
 * and a deliberate restart reports as a bare "software reset", so a panel that restarts unexplained leaves nothing behind.
 *
 * Two halves, after SonosESP's reboot log: a breadcrumb in RTC memory (util/reboot_log_esp.c) survives a software reset, a
 * panic and a watchdog reset (not a power cut) and is refreshed every few seconds without a flash write; once per boot the
 * previous run's breadcrumb and this boot's reset reason become one entry of an 8-entry ring kept in NVS.
 * Everything here is pure (host tested); the ESP glue keeps the RTC variable, the NVS blob and the timer.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REBOOT_LOG_SLOTS 8
#define REBOOT_CRUMB_MAGIC 0x5EB007C8u
#define REBOOT_RING_MAGIC 0x5EB0A11Bu

/* Which deliberate restart fired. Stored in NVS: append only, never renumber. */
typedef enum {
    REBOOT_CAUSE_NONE = 0, /* not recorded: a crash, a power cut or an untagged path */
    REBOOT_CAUSE_SETTINGS, /* Save + Reboot in the editor */
    REBOOT_CAUSE_OTA,      /* a firmware update finished */
    REBOOT_CAUSE_FACTORY_RESET,
    REBOOT_CAUSE_C6_UPDATE, /* the ESP32-C6 coprocessor firmware was updated */
    REBOOT_CAUSE_NET_WATCHDOG, /* the network watchdog gave up on a dead link or transport (util/net_watchdog.h) */
    REBOOT_CAUSE_HTTP_WATCHDOG, /* the device's own HTTP server stopped answering while the network was up */
    REBOOT_CAUSE_UI_WATCHDOG,   /* the UI stopped running (util/ui_watchdog.h); the restart is an abort() with a core dump */
    REBOOT_CAUSE_MANUAL,        /* "Restart panel" in the editor (POST /api/restart) */
    REBOOT_CAUSE_COUNT
} reboot_cause_t;

/* The state of the run that ends, kept in RTC memory. */
typedef struct {
    uint32_t magic;
    uint32_t uptime_s;          /* seconds the run had lasted at the last refresh */
    uint32_t min_free_internal; /* lowest internal free heap seen by then */
    uint32_t largest_internal;  /* largest internal free block at the last refresh */
    int64_t wall_epoch;         /* wall clock at the last refresh, 0 while the clock was not set */
    uint8_t cause;              /* a reboot_cause_t, set just before a deliberate restart */
    uint8_t reserved[3];
    uint32_t crc;
} reboot_crumb_t;

/* One line of the history. */
typedef struct {
    uint32_t seq;               /* boot counter of the boot that recorded the entry */
    uint8_t reset_reason;       /* esp_reset_reason_t of that boot (how the previous run ended) */
    uint8_t cause;              /* reboot_cause_t, or 0 */
    uint8_t had_crumb;          /* the previous run's breadcrumb was intact */
    uint8_t reserved;
    uint32_t prev_uptime_s;     /* how long the previous run lasted (0 when unknown) */
    uint32_t min_free_internal;
    uint32_t largest_internal;
    int64_t prev_wall_epoch;    /* wall time of the previous run's last refresh (0 when unknown) */
} reboot_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t next_seq;
    uint8_t head;  /* slot the next entry goes to */
    uint8_t count; /* entries kept, at most REBOOT_LOG_SLOTS */
    uint8_t reserved[2];
    reboot_entry_t slot[REBOOT_LOG_SLOTS];
} reboot_ring_t;

/* The breadcrumb's checksum and validity (magic plus CRC-32 of the other fields). */
void reboot_crumb_seal(reboot_crumb_t *crumb);
bool reboot_crumb_valid(const reboot_crumb_t *crumb);

/* A ring that is empty or whose magic does not match starts over. */
void reboot_ring_init_if_invalid(reboot_ring_t *ring);
/* The entry for this boot: built from the previous run's breadcrumb (used only when valid) and this boot's reset reason.
 * `crumb` may be NULL. A power-on or brownout reset means the RTC memory holds nothing trustworthy. */
void reboot_entry_build(reboot_entry_t *out, uint32_t seq, uint8_t reset_reason, const reboot_crumb_t *crumb);
/* Appends an entry, overwriting the oldest once the ring is full; assigns the ring's next sequence number to it. */
void reboot_ring_push(reboot_ring_t *ring, reboot_entry_t *entry);
/* Newest first: index 0 is the latest entry. False when there is none at that index. */
bool reboot_ring_get(const reboot_ring_t *ring, int index, reboot_entry_t *out);

/* Names (static strings) for esp_reset_reason_t values and causes, and one label for a whole entry such as "settings restart",
 * "panic", "task watchdog", "power-on", "software restart (untagged)". */
const char *reboot_reason_name(uint8_t esp_reset_reason);
const char *reboot_cause_name(uint8_t cause);
const char *reboot_entry_label(const reboot_entry_t *entry);
/* True when the run that ended did not end by choice: a panic, a watchdog reset, or the network/HTTP watchdog restarting a panel that no longer
 * worked. A power cut, a reset button, a settings restart or an update are not failures. */
bool reboot_entry_is_failure(const reboot_entry_t *entry);
