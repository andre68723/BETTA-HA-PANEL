/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/coredump_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "util/sha256_soft.h"

#define META_MAX 2048U
#define PREFIX_SIZE 44U /* magic[8], metadata length LE32, metadata SHA256[32] */
#define PATH_MAX_LEN 176
static const char MAGIC[] = "BHCD0001";

bool coredump_store_id_valid(const char *id)
{
    if (!id || strlen(id) != 64) return false;
    for (size_t i = 0; i < 64; i++) {
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    }
    return true;
}

static void digest_hex(const uint8_t digest[32], char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = '\0';
}

bool coredump_store_hash(coredump_reader_t read, void *context, size_t size, char id[65])
{
    if (!read || !id || !size || size > COREDUMP_STORE_MAX_BYTES) return false;
    sha256_soft_t hash;
    sha256_soft_init(&hash);
    uint8_t chunk[1024];
    for (size_t offset = 0; offset < size;) {
        size_t n = size - offset < sizeof(chunk) ? size - offset : sizeof(chunk);
        if (!read(context, offset, chunk, n)) return false;
        sha256_soft_update(&hash, chunk, n);
        offset += n;
    }
    uint8_t digest[32];
    sha256_soft_final(&hash, digest);
    digest_hex(digest, id);
    return true;
}

static bool record_path(const coredump_store_t *store, const char *id, char path[PATH_MAX_LEN])
{
    if (!store || !coredump_store_id_valid(id)) return false;
    snprintf(path, PATH_MAX_LEN, "%s/%s.dump", store->directory, id);
    return true;
}

static cJSON *record_json(const coredump_record_t *r)
{
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
#define STRING(field) if (!cJSON_AddStringToObject(j, #field, r->field)) goto fail
#define NUMBER(field) if (!cJSON_AddNumberToObject(j, #field, (double)r->field)) goto fail
    STRING(id); STRING(elf_sha256); STRING(task); STRING(project); STRING(version);
    STRING(git_commit); STRING(build_date); STRING(build_time);
    STRING(panic_reason);
    NUMBER(pc); NUMBER(mcause); NUMBER(size); NUMBER(boot_seq); NUMBER(archive_seq); NUMBER(crashed_at);
    NUMBER(diagnostic.cause); NUMBER(diagnostic.width); NUMBER(diagnostic.height);
    NUMBER(diagnostic.input_x); NUMBER(diagnostic.input_y); NUMBER(diagnostic.rotation);
    NUMBER(diagnostic.input_buffer); NUMBER(diagnostic.output_buffer); NUMBER(diagnostic.output_capacity);
    NUMBER(diagnostic.wait_ms);
#undef STRING
#undef NUMBER
    return j;
fail:
    cJSON_Delete(j);
    return NULL;
}

static bool json_string(const cJSON *j, const char *key, char *out, size_t capacity)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= capacity) return false;
    memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
    return true;
}

static bool json_number(const cJSON *j, const char *key, double maximum, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= 0 && v->valuedouble <= maximum)) return false;
    *out = v->valuedouble;
    return *out == (double)(uint64_t)*out;
}

static bool parse_record(const char *meta, coredump_record_t *r)
{
    cJSON *j = cJSON_ParseWithOpts(meta, NULL, true);
    if (!j) return false;
    bool ok = true;
    memset(r, 0, sizeof(*r));
#define STRING(field) ok = json_string(j, #field, r->field, sizeof(r->field)) && ok
#define NUMBER(field, max) do { double n = 0; ok = json_number(j, #field, max, &n) && ok; r->field = n; } while (0)
    STRING(id); STRING(elf_sha256); STRING(task); STRING(project); STRING(version);
    STRING(git_commit); STRING(build_date); STRING(build_time);
    STRING(panic_reason);
    NUMBER(pc, UINT32_MAX); NUMBER(mcause, UINT32_MAX); NUMBER(size, COREDUMP_STORE_MAX_BYTES);
    NUMBER(boot_seq, UINT32_MAX); NUMBER(crashed_at, 9007199254740991.0);
    NUMBER(archive_seq, UINT32_MAX);
    NUMBER(diagnostic.cause, COREDUMP_CAUSE_RUNTIME_STALL); NUMBER(diagnostic.width, UINT32_MAX);
    NUMBER(diagnostic.height, UINT32_MAX); NUMBER(diagnostic.input_x, UINT32_MAX); NUMBER(diagnostic.input_y, UINT32_MAX);
    NUMBER(diagnostic.rotation, 270); NUMBER(diagnostic.input_buffer, UINT32_MAX);
    NUMBER(diagnostic.output_buffer, UINT32_MAX); NUMBER(diagnostic.output_capacity, UINT32_MAX);
    NUMBER(diagnostic.wait_ms, UINT32_MAX);
#undef STRING
#undef NUMBER
    cJSON_Delete(j);
    return ok && r->size > 0 && coredump_store_id_valid(r->id);
}

static bool read_header(FILE *f, coredump_record_t *record, size_t *raw_offset)
{
    uint8_t prefix[PREFIX_SIZE];
    if (fread(prefix, 1, sizeof(prefix), f) != sizeof(prefix) || memcmp(prefix, MAGIC, 8)) return false;
    uint32_t length = (uint32_t)prefix[8] | (uint32_t)prefix[9] << 8 |
                      (uint32_t)prefix[10] << 16 | (uint32_t)prefix[11] << 24;
    if (!length || length > META_MAX) return false;
    char meta[META_MAX + 1];
    if (fread(meta, 1, length, f) != length) return false;
    meta[length] = '\0';
    sha256_soft_t hash;
    uint8_t digest[32];
    sha256_soft_init(&hash);
    sha256_soft_update(&hash, (const uint8_t *)meta, length);
    sha256_soft_final(&hash, digest);
    if (memcmp(digest, prefix + 12, sizeof(digest)) || !parse_record(meta, record)) return false;
    *raw_offset = PREFIX_SIZE + length;
    return true;
}

typedef struct { FILE *file; size_t start; } file_reader_t;
static bool read_file(void *context, size_t offset, void *buffer, size_t size)
{
    file_reader_t *r = context;
    return fseek(r->file, (long)(r->start + offset), SEEK_SET) == 0 && fread(buffer, 1, size, r->file) == size;
}

static bool validate_file(const char *path, coredump_record_t *record)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t start = 0;
    bool ok = read_header(f, record, &start);
    struct stat st;
    ok = ok && fstat(fileno(f), &st) == 0 && (size_t)st.st_size == start + record->size;
    char id[65];
    file_reader_t reader = {f, start};
    ok = ok && coredump_store_hash(read_file, &reader, record->size, id) && !strcmp(id, record->id);
    fclose(f);
    return ok;
}

static bool newer(const coredump_record_t *a, const coredump_record_t *b)
{
    if (a->archive_seq != b->archive_seq) return a->archive_seq > b->archive_seq;
    return strcmp(a->id, b->id) > 0;
}

static void retain(coredump_store_t *store, const coredump_record_t *record)
{
    size_t at = 0;
    while (at < store->count && !newer(record, &store->records[at])) at++;
    if (at >= COREDUMP_STORE_LIMIT) return;
    size_t end = store->count < COREDUMP_STORE_LIMIT ? store->count++ : COREDUMP_STORE_LIMIT - 1;
    for (size_t i = end; i > at; i--) store->records[i] = store->records[i - 1];
    store->records[at] = *record;
}

static bool prune(coredump_store_t *store)
{
    DIR *dir = opendir(store->directory);
    if (!dir) return false;
    struct dirent *entry;
    bool ok = true;
    while ((entry = readdir(dir))) {
        if (strlen(entry->d_name) != 69 || strcmp(entry->d_name + 64, ".dump")) continue;
        char id[65];
        memcpy(id, entry->d_name, 64); id[64] = '\0';
        if (!coredump_store_id_valid(id)) continue;
        bool keep = false;
        for (size_t i = 0; i < store->count; i++) keep |= !strcmp(id, store->records[i].id);
        char path[PATH_MAX_LEN];
        coredump_record_t record;
        if (!keep && record_path(store, id, path) && validate_file(path, &record) && remove(path) != 0) ok = false;
    }
    closedir(dir);
    return ok;
}

bool coredump_store_init(coredump_store_t *store, const char *directory)
{
    if (!store || !directory || strlen(directory) >= sizeof(store->directory)) return false;
    memset(store, 0, sizeof(*store));
    snprintf(store->directory, sizeof(store->directory), "%s", directory);
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return false;
    DIR *dir = opendir(directory);
    if (!dir) return false;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strlen(entry->d_name) != 69 || strcmp(entry->d_name + 64, ".dump")) continue;
        char id[65], path[PATH_MAX_LEN];
        memcpy(id, entry->d_name, 64); id[64] = '\0';
        coredump_record_t record;
        if (record_path(store, id, path) && validate_file(path, &record) && !strcmp(id, record.id)) retain(store, &record);
    }
    closedir(dir);
    return prune(store);
}

bool coredump_store_put(coredump_store_t *store, const coredump_record_t *record,
                        coredump_reader_t read, void *context)
{
    if (!store || !record || !read || !record->size || record->size > COREDUMP_STORE_MAX_BYTES) return false;
    char path[PATH_MAX_LEN];
    if (!record_path(store, record->id, path)) return false;
    for (size_t i = 0; i < store->count; i++) {
        if (!strcmp(record->id, store->records[i].id)) return validate_file(path, &(coredump_record_t){0}) && prune(store);
    }
    coredump_record_t imported = *record;
    uint32_t latest = store->count ? store->records[0].archive_seq : 0;
    if (latest == UINT32_MAX) return false;
    imported.archive_seq = latest + 1;
    record = &imported;
    cJSON *json = record_json(record);
    char *meta = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!meta) return false;
    size_t length = strlen(meta);
    if (!length || length > META_MAX) { free(meta); return false; }
    uint8_t prefix[PREFIX_SIZE] = {0};
    memcpy(prefix, MAGIC, 8);
    for (size_t i = 0; i < 4; i++) prefix[8 + i] = (uint8_t)(length >> (i * 8));
    sha256_soft_t hash;
    sha256_soft_init(&hash);
    sha256_soft_update(&hash, (const uint8_t *)meta, length);
    sha256_soft_final(&hash, prefix + 12);
    char temporary[PATH_MAX_LEN];
    snprintf(temporary, sizeof(temporary), "%s/incoming.tmp", store->directory);
    FILE *f = fopen(temporary, "wb");
    if (!f) { free(meta); return false; }
    bool ok = fwrite(prefix, 1, sizeof(prefix), f) == sizeof(prefix) && fwrite(meta, 1, length, f) == length;
    free(meta);
    uint8_t chunk[1024];
    for (size_t offset = 0; ok && offset < record->size;) {
        size_t n = record->size - offset < sizeof(chunk) ? record->size - offset : sizeof(chunk);
        ok = read(context, offset, chunk, n) && fwrite(chunk, 1, n, f) == n;
        offset += n;
    }
    ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = fclose(f) == 0 && ok;
    coredump_record_t verified;
    ok = ok && validate_file(temporary, &verified) && rename(temporary, path) == 0;
    if (!ok) { remove(temporary); return false; }
    retain(store, record);
    return prune(store);
}

bool coredump_store_read(const coredump_store_t *store, const char *id, size_t offset, void *buffer, size_t size)
{
    char path[PATH_MAX_LEN];
    if (!buffer || !record_path(store, id, path)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    coredump_record_t r;
    size_t start = 0;
    bool ok = read_header(f, &r, &start) && !strcmp(id, r.id) && offset <= r.size && size <= r.size - offset;
    file_reader_t reader = {f, start};
    ok = ok && read_file(&reader, offset, buffer, size);
    fclose(f);
    return ok;
}

bool coredump_store_delete(coredump_store_t *store, const char *id)
{
    char path[PATH_MAX_LEN];
    if (!record_path(store, id, path)) return false;
    for (size_t i = 0; i < store->count; i++) {
        if (strcmp(store->records[i].id, id)) continue;
        if (remove(path) != 0) return false;
        memmove(&store->records[i], &store->records[i + 1], (store->count - i - 1) * sizeof(store->records[0]));
        store->count--;
        return true;
    }
    return false;
}
