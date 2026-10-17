/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/admin_auth.h"

#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "psa/crypto.h"

#define AUTH_NVS_NAMESPACE "betta_auth"
#define AUTH_SALT_LEN 16
#define AUTH_HASH_LEN 32
#define AUTH_ROUNDS 5000

static const char *TAG_AUTH = "admin_auth";

#define AUTH_CRED_VERSION 1
#define AUTH_CRED_LEN (1 + AUTH_SALT_LEN + AUTH_HASH_LEN)

static admin_auth_state_t s_state = ADMIN_AUTH_UNSET;
static bool s_loaded = false;      /* a load ended in UNSET or SET; UNAVAILABLE is retried on the next call */
static uint8_t s_salt[AUTH_SALT_LEN];
static uint8_t s_hash[AUTH_HASH_LEN];

/* SHA-256(salt || password), then AUTH_ROUNDS times SHA-256(previous || salt). */
static bool auth_derive(const char *password, const uint8_t *salt, uint8_t out[AUTH_HASH_LEN])
{
    uint8_t buf[AUTH_SALT_LEN + ADMIN_AUTH_MAX_LEN];
    const size_t plen = strlen(password);
    if (plen > ADMIN_AUTH_MAX_LEN) {
        return false;
    }
    memcpy(buf, salt, AUTH_SALT_LEN);
    memcpy(buf + AUTH_SALT_LEN, password, plen);
    size_t got = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, buf, AUTH_SALT_LEN + plen, out, AUTH_HASH_LEN, &got) != PSA_SUCCESS) {
        return false;
    }
    uint8_t round_in[AUTH_HASH_LEN + AUTH_SALT_LEN];
    for (int i = 0; i < AUTH_ROUNDS; i++) {
        memcpy(round_in, out, AUTH_HASH_LEN);
        memcpy(round_in + AUTH_HASH_LEN, salt, AUTH_SALT_LEN);
        if (psa_hash_compute(PSA_ALG_SHA_256, round_in, sizeof(round_in), out, AUTH_HASH_LEN, &got) != PSA_SUCCESS) {
            return false;
        }
    }
    return true;
}

static bool auth_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

/* One read of the credentials. The namespace or both keys missing means "never set"; anything else that is not a complete, exactly sized record is
 * unreadable. Legacy devices keep their separate salt and hash keys until the password is set again. */
static admin_auth_state_t auth_load(void)
{
    nvs_handle_t h;
    const esp_err_t open_err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READONLY, &h);
    if (open_err == ESP_ERR_NVS_NOT_FOUND) {
        return ADMIN_AUTH_UNSET; /* the namespace does not exist: no password was ever set */
    }
    if (open_err != ESP_OK) {
        ESP_LOGE(TAG_AUTH, "credential storage cannot be opened (%s)", esp_err_to_name(open_err));
        return ADMIN_AUTH_UNAVAILABLE;
    }
    uint8_t cred[AUTH_CRED_LEN];
    size_t cred_len = sizeof(cred);
    const esp_err_t cred_err = nvs_get_blob(h, "cred", cred, &cred_len);
    admin_auth_state_t result = ADMIN_AUTH_UNAVAILABLE;
    if (cred_err == ESP_OK) {
        if (cred_len == AUTH_CRED_LEN && cred[0] == AUTH_CRED_VERSION) {
            memcpy(s_salt, cred + 1, AUTH_SALT_LEN);
            memcpy(s_hash, cred + 1 + AUTH_SALT_LEN, AUTH_HASH_LEN);
            result = ADMIN_AUTH_SET;
        } else {
            ESP_LOGE(TAG_AUTH, "credential record has size %u / version %u: unreadable", (unsigned)cred_len, (unsigned)cred[0]);
        }
    } else if (cred_err == ESP_ERR_NVS_NOT_FOUND) {
        size_t salt_len = sizeof(s_salt);
        size_t hash_len = sizeof(s_hash);
        const esp_err_t salt_err = nvs_get_blob(h, "salt", s_salt, &salt_len);
        const esp_err_t hash_err = nvs_get_blob(h, "hash", s_hash, &hash_len);
        if (salt_err == ESP_OK && hash_err == ESP_OK && salt_len == AUTH_SALT_LEN && hash_len == AUTH_HASH_LEN) {
            result = ADMIN_AUTH_SET;
        } else if (salt_err == ESP_ERR_NVS_NOT_FOUND && hash_err == ESP_ERR_NVS_NOT_FOUND) {
            result = ADMIN_AUTH_UNSET; /* the namespace exists but holds no password: it was cleared */
        } else {
            ESP_LOGE(TAG_AUTH, "legacy credentials incomplete (salt %s, hash %s): unreadable", esp_err_to_name(salt_err),
                     esp_err_to_name(hash_err));
        }
    } else {
        ESP_LOGE(TAG_AUTH, "credential record cannot be read (%s)", esp_err_to_name(cred_err));
    }
    nvs_close(h);
    return result;
}

esp_err_t admin_auth_init(void)
{
    if (s_loaded) {
        return ESP_OK;
    }
    const admin_auth_state_t before = s_state;
    const bool first = !s_loaded && before == ADMIN_AUTH_UNSET && s_salt[0] == 0 && s_hash[0] == 0;
    s_state = auth_load();
    s_loaded = s_state != ADMIN_AUTH_UNAVAILABLE;
    if (s_state == ADMIN_AUTH_SET && (first || before != ADMIN_AUTH_SET)) {
        ESP_LOGI(TAG_AUTH, "admin password is set, the web editor and the API require it");
    } else if (s_state == ADMIN_AUTH_UNAVAILABLE && before != ADMIN_AUTH_UNAVAILABLE) {
        ESP_LOGE(TAG_AUTH, "admin credentials unreadable: protected requests are refused until the settings are erased over USB");
    } else if (s_state != ADMIN_AUTH_UNAVAILABLE && before == ADMIN_AUTH_UNAVAILABLE) {
        ESP_LOGW(TAG_AUTH, "admin credentials readable again");
    }
    return ESP_OK;
}

admin_auth_state_t admin_auth_state(void)
{
    (void)admin_auth_init();
    return s_state;
}

bool admin_auth_enabled(void)
{
    return admin_auth_state() != ADMIN_AUTH_UNSET;
}

admin_auth_decision_t admin_auth_decide(admin_auth_state_t state, bool public_route)
{
    if (public_route || state == ADMIN_AUTH_UNSET) {
        return ADMIN_AUTH_ALLOW;
    }
    return state == ADMIN_AUTH_SET ? ADMIN_AUTH_CHECK : ADMIN_AUTH_DENY_UNAVAILABLE;
}

esp_err_t admin_auth_set_password(const char *password)
{
    (void)admin_auth_init();
    nvs_handle_t h;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (password == NULL || password[0] == '\0') {
        err = nvs_erase_all(h);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
        if (err == ESP_OK) {
            s_state = ADMIN_AUTH_UNSET;
            s_loaded = true;
        }
        return err;
    }
    if (strlen(password) < ADMIN_AUTH_MIN_LEN || strlen(password) > ADMIN_AUTH_MAX_LEN) {
        nvs_close(h);
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t salt[AUTH_SALT_LEN];
    uint8_t hash[AUTH_HASH_LEN];
    esp_fill_random(salt, sizeof(salt));
    if (!auth_derive(password, salt, hash)) {
        nvs_close(h);
        return ESP_FAIL;
    }
    /* One record written in one step: a power cut leaves the old record or the new one, never a salt without its hash. */
    uint8_t cred[AUTH_CRED_LEN];
    cred[0] = AUTH_CRED_VERSION;
    memcpy(cred + 1, salt, sizeof(salt));
    memcpy(cred + 1 + AUTH_SALT_LEN, hash, sizeof(hash));
    err = nvs_set_blob(h, "cred", cred, sizeof(cred));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    if (err == ESP_OK) { /* legacy keys of an older firmware are obsolete now; a failure here leaves harmless leftovers */
        (void)nvs_erase_key(h, "salt");
        (void)nvs_erase_key(h, "hash");
        (void)nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        memcpy(s_salt, salt, sizeof(salt));
        memcpy(s_hash, hash, sizeof(hash));
        s_state = ADMIN_AUTH_SET;
        s_loaded = true;
    }
    return err;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decode standard base64 into out (NUL terminated); false on bad input or overflow. */
static bool b64_decode(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (const char *p = in; *p != '\0' && *p != '='; p++) {
        int v = b64_val(*p);
        if (v < 0) {
            return false;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o + 1 >= out_len) {
                return false;
            }
            out[o++] = (char)((acc >> bits) & 0xFF);
        }
    }
    out[o] = '\0';
    return true;
}

bool admin_auth_check_basic(const char *header_value)
{
    const admin_auth_state_t state = admin_auth_state();
    if (state == ADMIN_AUTH_UNSET) {
        return true;
    }
    if (state != ADMIN_AUTH_SET) {
        return false; /* unreadable credentials: no header can match */
    }
    if (header_value == NULL || strncasecmp(header_value, "Basic ", 6) != 0) {
        return false;
    }
    char decoded[ADMIN_AUTH_MAX_LEN + 16];
    if (!b64_decode(header_value + 6, decoded, sizeof(decoded))) {
        return false;
    }
    char *colon = strchr(decoded, ':');
    if (colon == NULL) {
        return false;
    }
    *colon = '\0';
    const bool user_ok = strcmp(decoded, ADMIN_AUTH_USER) == 0;
    uint8_t hash[AUTH_HASH_LEN];
    const bool derived = auth_derive(colon + 1, s_salt, hash);
    return user_ok && derived && auth_equal(hash, s_hash, AUTH_HASH_LEN);
}
