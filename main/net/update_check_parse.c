/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "net/update_check_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define ASSET_PREFIX "betta86-ha-panel-"

const char *update_check_variant_for_project(const char *project_name)
{
    static const struct {
        const char *project;
        const char *variant;
    } k_variants[] = {
        {"betta-ha-panel", "panel4"},
        {"betta-ha-panel-10.1", "panel10"},
        {"betta-ha-panel-jc", "paneljc"},
        {"betta-ha-panel-tab5", "tab5"},
        {"betta-ha-panel-jc-v2", "paneljc-v2"},
    };
    if (project_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_variants) / sizeof(k_variants[0]); i++) {
        if (strcmp(project_name, k_variants[i].project) == 0) {
            return k_variants[i].variant;
        }
    }
    return NULL;
}

typedef struct {
    long core[3];
    const char *pre; /* points after '-', NULL for a release */
    size_t pre_len;
} version_t;

static void parse_version(const char *text, version_t *out)
{
    memset(out, 0, sizeof(*out));
    if (text == NULL) {
        return;
    }
    const char *p = text;
    if (*p == 'v' || *p == 'V') {
        p++;
    }
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*p)) {
            return;
        }
        char *end = NULL;
        out->core[i] = strtol(p, &end, 10);
        p = end;
        if (i < 2) {
            if (*p != '.') {
                return;
            }
            p++;
        }
    }
    if (*p == '-') {
        p++;
        size_t len = strcspn(p, "+");
        if (len > 0) {
            out->pre = p;
            out->pre_len = len;
        }
    }
}

/* One dot-separated pre-release identifier: letters compare as text, a trailing number numerically,
 * so "rc2" < "rc10" and "beta" < "rc". */
static int compare_identifier(const char *a, size_t a_len, const char *b, size_t b_len)
{
    size_t a_alpha = 0;
    size_t b_alpha = 0;
    while (a_alpha < a_len && !isdigit((unsigned char)a[a_alpha])) {
        a_alpha++;
    }
    while (b_alpha < b_len && !isdigit((unsigned char)b[b_alpha])) {
        b_alpha++;
    }
    size_t common = a_alpha < b_alpha ? a_alpha : b_alpha;
    int c = strncmp(a, b, common);
    if (c != 0) {
        return c;
    }
    if (a_alpha != b_alpha) {
        return a_alpha < b_alpha ? -1 : 1;
    }
    long a_num = a_alpha < a_len ? strtol(a + a_alpha, NULL, 10) : -1;
    long b_num = b_alpha < b_len ? strtol(b + b_alpha, NULL, 10) : -1;
    return (a_num > b_num) - (a_num < b_num);
}

static int compare_prerelease(const version_t *a, const version_t *b)
{
    if (a->pre == NULL || b->pre == NULL) {
        return (a->pre == NULL) - (b->pre == NULL);
    }
    const char *pa = a->pre;
    const char *pb = b->pre;
    const char *ea = a->pre + a->pre_len;
    const char *eb = b->pre + b->pre_len;
    while (pa < ea && pb < eb) {
        const char *da = memchr(pa, '.', (size_t)(ea - pa));
        const char *db = memchr(pb, '.', (size_t)(eb - pb));
        size_t la = (size_t)((da != NULL ? da : ea) - pa);
        size_t lb = (size_t)((db != NULL ? db : eb) - pb);
        int c = compare_identifier(pa, la, pb, lb);
        if (c != 0) {
            return c;
        }
        pa += la + (da != NULL ? 1 : 0);
        pb += lb + (db != NULL ? 1 : 0);
    }
    return (pa < ea) - (pb < eb);
}

int update_check_compare_versions(const char *a, const char *b, bool core_only)
{
    version_t va;
    version_t vb;
    parse_version(a, &va);
    parse_version(b, &vb);
    for (int i = 0; i < 3; i++) {
        if (va.core[i] != vb.core[i]) {
            return va.core[i] < vb.core[i] ? -1 : 1;
        }
    }
    return core_only ? 0 : compare_prerelease(&va, &vb);
}

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool update_check_parse_iso8601_utc(const char *text, int64_t *out_epoch)
{
    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int s = 0;
    char z = 0;
    if (text == NULL || out_epoch == NULL ||
        sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%c", &y, &mo, &d, &h, &mi, &s, &z) != 7 || z != 'Z' || mo < 1 ||
        mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) {
        return false;
    }
    *out_epoch = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    return true;
}

static const cJSON *find_variant_asset(const cJSON *release, const char *variant)
{
    char suffix[40];
    snprintf(suffix, sizeof(suffix), "-%s.ota.bin", variant);
    const size_t suffix_len = strlen(suffix);
    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
    const cJSON *asset = NULL;
    cJSON_ArrayForEach(asset, assets)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
        if (!cJSON_IsString(name) || !cJSON_IsString(url)) {
            continue;
        }
        const size_t len = strlen(name->valuestring);
        /* "-paneljc.ota.bin" must not match "...-paneljc-v2.ota.bin" and the other way round: the
         * suffix starts at the variant's own dash. */
        if (len > strlen(ASSET_PREFIX) + suffix_len && strncmp(name->valuestring, ASSET_PREFIX, strlen(ASSET_PREFIX)) == 0 &&
            strcmp(name->valuestring + len - suffix_len, suffix) == 0 &&
            strncmp(url->valuestring, "https://", 8) == 0) {
            return asset;
        }
    }
    return NULL;
}

bool update_check_sha256_matches(const char *hex, const uint8_t digest[32])
{
    if (hex == NULL || digest == NULL || strlen(hex) != 64) {
        return false;
    }
    for (int i = 0; i < 32; i++) {
        char pair[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
        if (!isxdigit((unsigned char)pair[0]) || !isxdigit((unsigned char)pair[1]) ||
            (uint8_t)strtoul(pair, NULL, 16) != digest[i]) {
            return false;
        }
    }
    return true;
}

bool update_check_parse_releases(const char *json, size_t json_len, const char *variant, bool include_prereleases,
    const char *running_version, int64_t build_epoch, update_check_release_t *out)
{
    if (json == NULL || variant == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL || (!cJSON_IsArray(root) && !cJSON_IsObject(root))) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *best = NULL;
    const cJSON *best_asset = NULL;
    const cJSON *best_tag = NULL;
    const int count = cJSON_IsArray(root) ? cJSON_GetArraySize(root) : 1;
    for (int i = 0; i < count; i++) {
        const cJSON *release = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, i) : root;
        const cJSON *tag = cJSON_GetObjectItemCaseSensitive(release, "tag_name");
        if (!cJSON_IsString(tag) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(release, "draft"))) {
            continue;
        }
        const bool prerelease = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(release, "prerelease"));
        if (prerelease && !include_prereleases) {
            continue;
        }
        const cJSON *asset = find_variant_asset(release, variant);
        if (asset == NULL) {
            continue;
        }
        if (best == NULL || update_check_compare_versions(tag->valuestring, best_tag->valuestring, false) > 0) {
            best = release;
            best_asset = asset;
            best_tag = tag;
        }
    }

    if (best != NULL) {
        out->found = true;
        out->prerelease = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(best, "prerelease"));
        snprintf(out->tag, sizeof(out->tag), "%s", best_tag->valuestring);
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(best_asset, "browser_download_url");
        snprintf(out->asset_url, sizeof(out->asset_url), "%s", url->valuestring);
        const cJSON *digest = cJSON_GetObjectItemCaseSensitive(best_asset, "digest");
        if (cJSON_IsString(digest) && strncmp(digest->valuestring, "sha256:", 7) == 0 &&
            strlen(digest->valuestring + 7) == 64) {
            const char *hex = digest->valuestring + 7;
            bool valid = true;
            for (int i = 0; i < 64; i++) {
                valid = valid && isxdigit((unsigned char)hex[i]);
                out->asset_sha256[i] = (char)tolower((unsigned char)hex[i]);
            }
            out->asset_sha256[valid ? 64 : 0] = '\0';
        }
        const cJSON *updated = cJSON_GetObjectItemCaseSensitive(best_asset, "updated_at");
        if (!cJSON_IsString(updated) || !update_check_parse_iso8601_utc(updated->valuestring, &out->published)) {
            out->published = 0;
        }
        /* A running pre-release names itself ("v0.9.0-rc2"), so the whole version decides: rc3 and the final release are
         * newer, an older rc is not. A plain "v0.9.0" cannot say which candidate or release it is, so only its core counts
         * and the upload time breaks the tie. */
        const bool running_prerelease = strchr(running_version, '-') != NULL;
        const int core = update_check_compare_versions(out->tag, running_version, !running_prerelease);
        if (core > 0) {
            out->newer = true;
        } else if (core == 0 && build_epoch > 0 && out->published > 0) {
            out->newer = out->published > build_epoch + UPDATE_CHECK_SAME_VERSION_MARGIN_S;
        }
    }
    cJSON_Delete(root);
    return true;
}
