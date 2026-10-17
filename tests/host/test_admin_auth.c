/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The admin password store (main/settings/admin_auth.c, included here so the test can reset its state like a reboot) against a fake NVS with
 * injected faults. S-01 of the security review: a credential that cannot be read must close the protected routes, never open them. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings/admin_auth.c" /* NOLINT: the production file, with the stubs of tests/host/auth_stubs */

static int g_checks, g_failed;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---------- stubs ---------- */

void host_log(const char *level, const char *tag, const char *fmt, ...)
{
    if (getenv("HOST_LOG") == NULL) return;
    va_list ap;
    va_start(ap, fmt);
    printf("%s %s: ", level, tag);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
void esp_fill_random(void *buf, size_t len)
{
    static unsigned seed = 12345;
    for (size_t i = 0; i < len; i++) { seed = seed * 1103515245u + 12345u; ((uint8_t *)buf)[i] = (uint8_t)(seed >> 16); }
}
psa_status_t psa_hash_compute(int alg, const uint8_t *in, size_t in_len, uint8_t *out, size_t out_size, size_t *out_len)
{
    (void)alg;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < in_len; i++) { h ^= in[i]; h *= 1099511628211ull; }
    for (size_t i = 0; i < out_size; i++) { h ^= (uint64_t)i * 0x9E3779B97F4A7C15ull; h *= 1099511628211ull; out[i] = (uint8_t)(h >> 24); }
    *out_len = out_size;
    return PSA_SUCCESS;
}

typedef struct { char key[16]; size_t len; uint8_t data[80]; bool used; } entry_t;
static struct {
    bool namespace_exists;
    entry_t e[6];
    esp_err_t open_err;                 /* nvs_open fails with this */
    esp_err_t get_err_key[3]; char get_key[3][16]; /* nvs_get_blob of this key fails */
    esp_err_t set_err;                  /* nvs_set_blob fails */
} g_nvs;

static entry_t *find(const char *key)
{
    for (int i = 0; i < 6; i++) if (g_nvs.e[i].used && strcmp(g_nvs.e[i].key, key) == 0) return &g_nvs.e[i];
    return NULL;
}
static void put(const char *key, const void *data, size_t len)
{
    entry_t *e = find(key);
    for (int i = 0; e == NULL && i < 6; i++) if (!g_nvs.e[i].used) e = &g_nvs.e[i];
    snprintf(e->key, sizeof(e->key), "%s", key);
    e->len = len; e->used = true;
    memcpy(e->data, data, len);
    g_nvs.namespace_exists = true;
}
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out)
{
    (void)name;
    *out = 1;
    if (g_nvs.open_err != ESP_OK) return g_nvs.open_err;
    if (mode == NVS_READONLY && !g_nvs.namespace_exists) return ESP_ERR_NVS_NOT_FOUND;
    if (mode == NVS_READWRITE) g_nvs.namespace_exists = true;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    for (int i = 0; i < 3; i++) if (g_nvs.get_err_key[i] != ESP_OK && strcmp(g_nvs.get_key[i], key) == 0) return g_nvs.get_err_key[i];
    entry_t *e = find(key);
    if (e == NULL) return ESP_ERR_NVS_NOT_FOUND;
    if (*len < e->len) { *len = e->len; return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out, e->data, e->len);
    *len = e->len;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t len)
{
    (void)h;
    if (g_nvs.set_err != ESP_OK) return g_nvs.set_err;
    put(key, value, len);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    (void)h;
    entry_t *e = find(key);
    if (e == NULL) return ESP_ERR_NVS_NOT_FOUND;
    e->used = false;
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t h) { (void)h; memset(g_nvs.e, 0, sizeof(g_nvs.e)); return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }

/* ---------- helpers ---------- */

static void fresh_storage(void) { memset(&g_nvs, 0, sizeof(g_nvs)); }
static void reboot(void)
{
    s_loaded = false;
    s_state = ADMIN_AUTH_UNSET;
    memset(s_salt, 0, sizeof(s_salt));
    memset(s_hash, 0, sizeof(s_hash));
}
static void fail_get(int slot, const char *key, esp_err_t err) { snprintf(g_nvs.get_key[slot], 16, "%s", key); g_nvs.get_err_key[slot] = err; }

static void basic_header(char *out, size_t cap, const char *user, const char *pass)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char raw[128];
    snprintf(raw, sizeof(raw), "%s:%s", user, pass);
    size_t n = strlen(raw), o = 0;
    snprintf(out, cap, "Basic ");
    o = strlen(out);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)(uint8_t)raw[i] << 16 | (i + 1 < n ? (uint32_t)(uint8_t)raw[i + 1] << 8 : 0) | (i + 2 < n ? (uint8_t)raw[i + 2] : 0);
        out[o++] = tbl[(v >> 18) & 63]; out[o++] = tbl[(v >> 12) & 63];
        out[o++] = i + 1 < n ? tbl[(v >> 6) & 63] : '='; out[o++] = i + 2 < n ? tbl[v & 63] : '=';
    }
    out[o] = '\0';
}
static bool login(const char *user, const char *pass)
{
    char header[200];
    basic_header(header, sizeof(header), user, pass);
    return admin_auth_check_basic(header);
}

/* ---------- tests ---------- */

static void test_unset_set_clear(void)
{
    fresh_storage(); reboot();
    CHECK(admin_auth_init() == ESP_OK);
    CHECK(admin_auth_state() == ADMIN_AUTH_UNSET && !admin_auth_enabled());
    CHECK(admin_auth_check_basic(NULL));                                  /* no password: open */
    CHECK(admin_auth_decide(ADMIN_AUTH_UNSET, false) == ADMIN_AUTH_ALLOW);

    CHECK(admin_auth_set_password("short") == ESP_ERR_INVALID_SIZE);       /* too short: nothing changes */
    CHECK(admin_auth_state() == ADMIN_AUTH_UNSET);

    CHECK(admin_auth_set_password("secret1") == ESP_OK);
    CHECK(admin_auth_state() == ADMIN_AUTH_SET && admin_auth_enabled());
    CHECK(login("admin", "secret1"));
    CHECK(!login("admin", "secret2") && !login("root", "secret1") && !login("admin", ""));
    CHECK(!admin_auth_check_basic(NULL) && !admin_auth_check_basic("Bearer abc") && !admin_auth_check_basic("Basic ***"));
    CHECK(admin_auth_decide(ADMIN_AUTH_SET, false) == ADMIN_AUTH_CHECK);
    CHECK(admin_auth_decide(ADMIN_AUTH_SET, true) == ADMIN_AUTH_ALLOW);   /* the health check stays reachable */
    CHECK(find("cred") != NULL && find("cred")->len == AUTH_CRED_LEN && find("cred")->data[0] == AUTH_CRED_VERSION);
    CHECK(find("salt") == NULL && find("hash") == NULL);

    reboot();                                                             /* survives a restart */
    CHECK(admin_auth_state() == ADMIN_AUTH_SET && login("admin", "secret1") && !login("admin", "x"));

    CHECK(admin_auth_set_password("") == ESP_OK);                         /* cleared on purpose: open again */
    CHECK(admin_auth_state() == ADMIN_AUTH_UNSET && admin_auth_check_basic(NULL));
    reboot();
    CHECK(admin_auth_state() == ADMIN_AUTH_UNSET);                        /* the namespace exists but is empty: still "never set" */
}

static void test_open_errors_fail_closed_and_heal(void)
{
    const esp_err_t errors[] = {ESP_FAIL, ESP_ERR_NVS_INVALID_STATE, ESP_ERR_NO_MEM};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        fresh_storage(); reboot();
        CHECK(admin_auth_set_password("secret1") == ESP_OK);
        reboot();
        g_nvs.open_err = errors[i];
        CHECK(admin_auth_init() == ESP_OK);                                /* the boot does not abort */
        CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
        CHECK(admin_auth_enabled());                                       /* protection stays on */
        CHECK(!login("admin", "secret1") && !admin_auth_check_basic(NULL)); /* nobody gets in, not even with the right password */
        CHECK(admin_auth_decide(admin_auth_state(), false) == ADMIN_AUTH_DENY_UNAVAILABLE);
        CHECK(admin_auth_decide(admin_auth_state(), true) == ADMIN_AUTH_ALLOW);
        g_nvs.open_err = ESP_OK;                                          /* the storage heals: the next call reads it again */
        CHECK(admin_auth_state() == ADMIN_AUTH_SET && login("admin", "secret1"));
    }
    /* the very first failure must not stick as "no password" (the old code marked itself loaded before reading) */
    fresh_storage(); reboot();
    g_nvs.open_err = ESP_FAIL;
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    g_nvs.open_err = ESP_OK;
    CHECK(admin_auth_state() == ADMIN_AUTH_UNSET);
}

static void test_record_faults(void)
{
    uint8_t blob[AUTH_CRED_LEN];
    fresh_storage(); reboot();
    CHECK(admin_auth_set_password("secret1") == ESP_OK);
    memcpy(blob, find("cred")->data, sizeof(blob));

    struct { const char *label; size_t len; uint8_t version; } bad[] = {
        {"short record", 10, AUTH_CRED_VERSION}, {"wrong version", AUTH_CRED_LEN, 2}, {"version 0", AUTH_CRED_LEN, 0}};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        fresh_storage(); reboot();
        uint8_t b[80];
        memcpy(b, blob, sizeof(blob));
        b[0] = bad[i].version;
        put("cred", b, bad[i].len);
        CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
        CHECK(!login("admin", "secret1"));
    }
    fresh_storage(); reboot();                                           /* a record that is too long for the buffer */
    uint8_t big[60] = {AUTH_CRED_VERSION};
    put("cred", big, sizeof(big));
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);

    fresh_storage(); reboot();                                           /* a read error of the record */
    put("cred", blob, sizeof(blob));
    fail_get(0, "cred", ESP_FAIL);
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    fail_get(0, "cred", ESP_OK);
    CHECK(admin_auth_state() == ADMIN_AUTH_SET);                         /* and heals */
}

static void test_legacy_pair(void)
{
    uint8_t salt[AUTH_SALT_LEN], hash[AUTH_HASH_LEN];
    esp_fill_random(salt, sizeof(salt));
    CHECK(auth_derive("legacy1", salt, hash));

    fresh_storage(); reboot();                                           /* a complete legacy pair still works */
    put("salt", salt, sizeof(salt)); put("hash", hash, sizeof(hash));
    CHECK(admin_auth_state() == ADMIN_AUTH_SET && login("admin", "legacy1") && !login("admin", "legacy2"));

    CHECK(admin_auth_set_password("modern1") == ESP_OK);                 /* setting it again migrates to the single record */
    CHECK(find("cred") != NULL && find("salt") == NULL && find("hash") == NULL);
    reboot();
    CHECK(admin_auth_state() == ADMIN_AUTH_SET && login("admin", "modern1") && !login("admin", "legacy1"));

    /* half a pair: unreadable, not "no password" */
    fresh_storage(); reboot();
    put("salt", salt, sizeof(salt));
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    fresh_storage(); reboot();
    put("hash", hash, sizeof(hash));
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    fresh_storage(); reboot();                                           /* a salt of the wrong length */
    put("salt", salt, 8); put("hash", hash, sizeof(hash));
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    fresh_storage(); reboot();                                           /* the hash read fails */
    put("salt", salt, sizeof(salt)); put("hash", hash, sizeof(hash));
    fail_get(0, "hash", ESP_FAIL);
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    fresh_storage(); reboot();                                           /* the salt read fails */
    put("salt", salt, sizeof(salt)); put("hash", hash, sizeof(hash));
    fail_get(0, "salt", ESP_FAIL);
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    fresh_storage(); reboot();                                           /* an empty but existing namespace: never set */
    g_nvs.namespace_exists = true;
    CHECK(admin_auth_state() == ADMIN_AUTH_UNSET && admin_auth_check_basic(NULL));
}

static void test_write_failures(void)
{
    fresh_storage(); reboot();
    CHECK(admin_auth_set_password("secret1") == ESP_OK);
    g_nvs.set_err = ESP_FAIL;                                            /* a failed write keeps the old password */
    CHECK(admin_auth_set_password("secret2") == ESP_FAIL);
    CHECK(admin_auth_state() == ADMIN_AUTH_SET && login("admin", "secret1") && !login("admin", "secret2"));
    reboot();
    g_nvs.set_err = ESP_OK;
    CHECK(login("admin", "secret1"));                                    /* and so does the storage */

    g_nvs.open_err = ESP_FAIL;                                           /* no write access at all */
    CHECK(admin_auth_set_password("secret3") == ESP_FAIL);
    CHECK(admin_auth_set_password("") == ESP_FAIL);
    g_nvs.open_err = ESP_OK;
    CHECK(admin_auth_state() == ADMIN_AUTH_SET && login("admin", "secret1"));

    /* recovery through the API while the storage was unreadable: a new password replaces the unreadable record */
    fresh_storage(); reboot();
    uint8_t junk[5] = {9, 9, 9, 9, 9};
    put("cred", junk, sizeof(junk));
    CHECK(admin_auth_state() == ADMIN_AUTH_UNAVAILABLE);
    CHECK(admin_auth_set_password("recover1") == ESP_OK);
    CHECK(admin_auth_state() == ADMIN_AUTH_SET && login("admin", "recover1"));
}

int main(void)
{
    test_unset_set_clear();
    test_open_errors_fail_closed_and_heal();
    test_record_faults();
    test_legacy_pair();
    test_write_failures();
    printf("%s: %d checks, %d failed\n", g_failed == 0 ? "PASS admin authentication" : "FAIL admin authentication", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
