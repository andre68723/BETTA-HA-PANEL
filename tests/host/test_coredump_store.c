/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/coredump_store.h"
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(expr) do { checks++; assert(expr); } while (0)
typedef struct { const uint8_t *bytes; size_t size; bool fail; } source_t;
static bool read_source(void *context, size_t offset, void *buffer, size_t size)
{
    source_t *s = context;
    if (s->fail || offset > s->size || size > s->size - offset) return false;
    memcpy(buffer, s->bytes + offset, size);
    return true;
}

static coredump_record_t record(source_t *source, uint32_t seq, int64_t time)
{
    coredump_record_t r = {.size = (uint32_t)source->size, .boot_seq = seq, .crashed_at = time,
        .pc = 0x4ff0e298, .mcause = 2, .diagnostic = {.cause = COREDUMP_CAUSE_PPA_TIMEOUT,
        .width = 352, .height = 68, .input_x = 15, .input_y = 421, .rotation = 90,
        .input_buffer = 0x48d8ca80, .output_buffer = 0x48e54b00, .output_capacity = 409600, .wait_ms = 1000}};
    strcpy(r.task, "taskLVGL"); strcpy(r.project, "betta-ha-panel-jc");
    strcpy(r.elf_sha256, "943dc7109"); strcpy(r.git_commit, "90790a9e-dirty");
    strcpy(r.version, "v0.9.0-rc5"); strcpy(r.build_date, "Oct  7 2026"); strcpy(r.build_time, "10:16:26");
    strcpy(r.panic_reason, "abort() was called at PC 0x480b1271 on core 1");
    CHECK(coredump_store_hash(read_source, source, source->size, r.id));
    return r;
}

static void cleanup(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[PATH_MAX + 300]; snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        CHECK(unlink(path) == 0);
    }
    closedir(d);
    CHECK(rmdir(dir) == 0);
}

static void test_archive(void)
{
    char dir[] = "/tmp/betta-coredump-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    coredump_store_t store, reload;
    CHECK(coredump_store_init(&store, dir)); CHECK(store.count == 0);
    uint8_t bytes[4097];
    for (size_t i = 0; i < sizeof(bytes); i++) bytes[i] = (uint8_t)i;
    source_t source = {bytes, sizeof(bytes), false};
    coredump_record_t a = record(&source, 140, 1791380000);
    CHECK(coredump_store_put(&store, &a, read_source, &source)); CHECK(store.count == 1);
    uint8_t returned[4097];
    CHECK(coredump_store_read(&store, a.id, 0, returned, sizeof(returned)));
    CHECK(!memcmp(returned, bytes, sizeof(bytes))); /* raw bytes, no archive header */
    CHECK(coredump_store_read(&store, a.id, 4096, returned, 1)); CHECK(returned[0] == bytes[4096]);
    CHECK(!coredump_store_read(&store, a.id, 4097, returned, 1));
    CHECK(!coredump_store_read(&store, a.id, SIZE_MAX, returned, 1));
    CHECK(coredump_store_init(&reload, dir)); CHECK(reload.count == 1);
    a.archive_seq = 1;
    CHECK(!memcmp(&reload.records[0], &a, sizeof(a))); /* metadata + diagnostic round-trip */

    /* Re-import after power loss between archive commit and raw partition erase. */
    coredump_record_t duplicate = a;
    duplicate.boot_seq = 999; duplicate.crashed_at = 1791389999;
    CHECK(coredump_store_put(&reload, &duplicate, read_source, &source));
    CHECK(reload.count == 1 && reload.records[0].boot_seq == a.boot_seq);

    bytes[0]++;
    coredump_record_t b = record(&source, 141, 1791381000);
    CHECK(coredump_store_put(&reload, &b, read_source, &source));
    CHECK(reload.count == 2 && !strcmp(reload.records[0].id, b.id));
    bytes[0]++;
    coredump_record_t c = record(&source, 142, 0); /* crash before NTP: sequence still orders it */
    source.fail = true;
    CHECK(!coredump_store_put(&reload, &c, read_source, &source));
    CHECK(coredump_store_init(&store, dir)); CHECK(store.count == 2);
    CHECK(!strcmp(store.records[0].id, b.id) && !strcmp(store.records[1].id, a.id));
    source.fail = false;
    coredump_record_t bad_id = c; bad_id.id[0] = bad_id.id[0] == '0' ? '1' : '0';
    CHECK(!coredump_store_put(&store, &bad_id, read_source, &source)); /* reject changed source/ID mismatch */
    CHECK(store.count == 2);
    CHECK(coredump_store_put(&store, &c, read_source, &source));
    CHECK(store.count == 2 && !strcmp(store.records[0].id, c.id) && !strcmp(store.records[1].id, b.id));
    CHECK(!coredump_store_read(&store, a.id, 0, returned, 1)); /* third report evicts oldest only after commit */
    CHECK(coredump_store_init(&reload, dir)); CHECK(reload.count == 2);
    CHECK(!strcmp(reload.records[0].id, c.id));
    CHECK(!coredump_store_delete(&reload, a.id)); /* a stale ID cannot delete a different report */
    CHECK(coredump_store_delete(&reload, b.id));
    CHECK(reload.count == 1 && !strcmp(reload.records[0].id, c.id));
    CHECK(coredump_store_init(&store, dir)); CHECK(store.count == 1);
    CHECK(coredump_store_delete(&store, c.id));
    CHECK(coredump_store_init(&reload, dir)); CHECK(reload.count == 0);
    CHECK(coredump_store_put(&reload, &c, read_source, &source));

    char path[256]; snprintf(path, sizeof(path), "%s/%s.dump", dir, c.id);
    FILE *file = fopen(path, "r+b"); CHECK(file != NULL);
    CHECK(fseek(file, -1, SEEK_END) == 0); CHECK(fputc(0x55, file) != EOF); CHECK(fclose(file) == 0);
    CHECK(coredump_store_init(&store, dir)); CHECK(store.count == 0); /* damaged payload is never listed */
    cleanup(dir);
}

static void test_marker(void)
{
    coredump_diagnostic_marker_t marker;
    coredump_diagnostic_t in = {.cause = COREDUMP_CAUSE_PPA_TIMEOUT, .width = 352, .height = 68, .rotation = 90}, out;
    coredump_diagnostic_seal(&marker, &in);
    CHECK(coredump_diagnostic_take(&marker, true, &out)); CHECK(!memcmp(&in, &out, sizeof(in)));
    CHECK(!coredump_diagnostic_take(&marker, true, &out)); CHECK(out.cause == COREDUMP_CAUSE_NONE);
    coredump_diagnostic_seal(&marker, &in);
    CHECK(!coredump_diagnostic_take(&marker, false, &out)); /* power cut/OTA must not reuse a stale cause */
    coredump_diagnostic_seal(&marker, &in); marker.data.width++;
    CHECK(!coredump_diagnostic_take(&marker, true, &out));
    in.cause = 999; coredump_diagnostic_seal(&marker, &in);
    CHECK(!coredump_diagnostic_take(&marker, true, &out));
}

int main(void)
{
    CHECK(!coredump_store_id_valid("../../settings")); CHECK(!coredump_store_id_valid(NULL));
    CHECK(!coredump_store_id_valid(""));
    char id[65]; source_t empty = {NULL, 0, false};
    CHECK(!coredump_store_hash(read_source, &empty, 0, id));
    CHECK(!coredump_store_hash(read_source, &empty, COREDUMP_STORE_MAX_BYTES + 1, id));
    test_archive(); test_marker();
    printf("coredump archive: %u checks passed\n", checks);
    return 0;
}
