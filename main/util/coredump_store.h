/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "util/coredump_diagnostics.h"

#define COREDUMP_STORE_LIMIT 2
#define COREDUMP_STORE_MAX_BYTES (256U * 1024U)

typedef struct {
    char id[65]; /* SHA256 of the raw dump, independent of the boot which imports it */
    char elf_sha256[65]; /* crashing ELF's hash/prefix, not necessarily the currently running ELF */
    char task[16];
    char project[33];
    char version[33];
    char git_commit[33];
    char build_date[17];
    char build_time[17];
    char panic_reason[161];
    coredump_diagnostic_t diagnostic;
    uint32_t pc;
    uint32_t mcause;
    uint32_t size;
    uint32_t boot_seq;
    uint32_t archive_seq; /* independent of wall clock/NVS reset; assigned only to a new import */
    int64_t crashed_at; /* last pre-crash reboot breadcrumb; zero if unknown */
} coredump_record_t;

typedef struct {
    char directory[96];
    coredump_record_t records[COREDUMP_STORE_LIMIT]; /* newest first */
    size_t count;
} coredump_store_t;

typedef bool (*coredump_reader_t)(void *context, size_t offset, void *buffer, size_t size);

/* Filesystem-only archive, also exercised on the host. Each record and raw dump share one
 * checksummed file committed by rename. Interrupted imports cannot replace either older dump.
 * Call before starting HTTP; subsequent reads/deletions belong to the HTTP task. */
bool coredump_store_init(coredump_store_t *store, const char *directory);
bool coredump_store_id_valid(const char *id);
bool coredump_store_hash(coredump_reader_t read, void *context, size_t size, char id[65]);
bool coredump_store_put(coredump_store_t *store, const coredump_record_t *record,
                        coredump_reader_t read, void *context);
bool coredump_store_read(const coredump_store_t *store, const char *id, size_t offset, void *buffer, size_t size);
bool coredump_store_delete(coredump_store_t *store, const char *id);
