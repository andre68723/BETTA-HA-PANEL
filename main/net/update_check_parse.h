/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Pure part of the daily firmware update check (net/update_check.c): reads a GitHub release API
 * response and decides whether it holds a newer build for this panel. No ESP-IDF dependencies, so
 * the host tests cover it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A running firmware that reports a plain version (v0.9.0) cannot say which candidate or release it
 * is: a release on the same core (v0.9.0-rc3, v0.9.0, or a re-upload) then counts as newer only when
 * its asset was uploaded this long after the firmware was built. A firmware that reports its
 * pre-release (v0.9.0-rc2, set in CMakeLists.txt or by BETTA_VERSION) is compared as a whole: rc3 and
 * v0.9.0 are newer at once, the same version only by this margin (a re-upload). The margin also covers
 * a build time read in the panel's time zone that differs from the build machine's. */
#define UPDATE_CHECK_SAME_VERSION_MARGIN_S (12 * 3600)

typedef struct {
    bool found;          /* a release with an OTA asset for this variant */
    bool newer;          /* ... that is newer than the running firmware */
    bool prerelease;
    char tag[32];        /* "v0.9.0-rc2" */
    char asset_url[256]; /* browser_download_url of the variant's .ota.bin */
    char asset_sha256[65]; /* the asset's "digest" ("sha256:<hex>") in lowercase hex, "" when GitHub reports none */
    int64_t published;   /* asset upload time, seconds since the epoch (UTC) */
} update_check_release_t;

/* Release asset variant ("panel4", "paneljc", ...) for an ESP app project name ("betta-ha-panel-jc"),
 * or NULL for an unknown project. The names match the project() names in the root CMakeLists.txt. */
const char *update_check_variant_for_project(const char *project_name);

/* Semantic version order of two tags with an optional leading "v": <0, 0 or >0. A pre-release sorts
 * before its release; "rc10" sorts after "rc2". Unparsable tags compare as 0.0.0. With core_only,
 * the pre-release parts are ignored. */
int update_check_compare_versions(const char *a, const char *b, bool core_only);

/* "2026-09-26T16:46:25Z" -> seconds since the epoch. */
bool update_check_parse_iso8601_utc(const char *text, int64_t *out_epoch);

/* True when `hex` (64 hex digits, any case) is the SHA-256 `digest` of 32 bytes. */
bool update_check_sha256_matches(const char *hex, const uint8_t digest[32]);

/* Reads a GitHub API response: one release object (/releases/latest) or an array (/releases).
 * Drafts are skipped, pre-releases unless include_prereleases. Of the remaining releases that carry a
 * "betta86-ha-panel-*-<variant>.ota.bin" asset, the highest version is chosen; out->newer compares it
 * with running_version and the firmware build time (0 = unknown). Returns false for JSON it cannot
 * read; a readable response without a matching release returns true with out->found false. */
bool update_check_parse_releases(const char *json, size_t json_len, const char *variant, bool include_prereleases,
    const char *running_version, int64_t build_epoch, update_check_release_t *out);
