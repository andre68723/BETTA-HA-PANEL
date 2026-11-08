<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Releases, OTA and rollback

## Rollback

- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`: a fresh OTA image starts as "pending verify". `util/ota_validate.c` marks it
  valid after 60 s of uptime with the HTTP server answering and Wi-Fi connected (or the setup AP up); a reset before that
  boots the previous image. `ota_image_state` in `/api/version` shows the state.
- The bootloader is not part of an OTA: flash it once over USB (`scripts/build.sh <variant> -p <port> flash`, see
  [Changing the partition table](build.md#changing-the-partition-table)). An old bootloader still takes OTA images but has
  no rollback (`ota_image_state: new`); `bootloader.rollback` in `/api/version` and Settings → Device show which one a
  panel has. Bootloader only:
  `python3 -m esptool --chip esp32p4 -p <port> write-flash 0x2000 build/<variant>/bootloader/bootloader.bin`.
- A new esp_hosted version needs another C6 image. That restart must not happen while the image is pending, or the
  bootloader rolls back and the old image flashes the C6 back. `wifi_mgr.c` defers the C6 update until the image is valid.
- The OTA digest is computed in software (`util/sha256_soft.c`): the SHA engine returned wrong digests for the 4.8 MB
  download on a paneljc (2026-10-02). Installs from the release found by the update check are compared with GitHub's
  `digest`; other URLs and uploads are not verified.

## Automatic install

Off by default (`net/auto_update_task.c`, decision in the host-tested `util/auto_update.c`). It installs only when all
hold: a newer release with a SHA-256 digest exists (pre-releases follow "Include pre-releases"); the bootloader can roll
back; the clock is set and it is between 03:00 and 04:00 local time, after the panel's own start minute; nobody used the
panel or editor for 10 minutes (`util/user_activity.c`); Wi-Fi is at least −75 dBm and RAM is free; that version was not
rolled back before. The attempt must be committed to NVS before the download starts; storage errors suspend automatic installs and retry the pending state without discarding blocked versions. After the restart, `auto_update_state_resolve_boot()` records
`installed` (only once the image is valid), `rolled_back` (version blocked, notice on the panel) or `failed`.
`/api/ota/status` has the `auto_update` object, including `storage_ok` and `wait_storage`. Browser uploads and OTA JSON request bodies abort after 30 seconds without progress or 180 seconds total. Image validation requires the flash gate; a timeout aborts the OTA handle instead of bypassing the lock.

A pre-release tag (`v0.9.2-rc2`) is compared as a whole version; a plain `v0.9.1` compares the core and
falls back to the upload time, so give test releases an `-rcN` suffix. Images carry their version in `BETTAOS_RELEASE_VERSION`;
a test build uses `BETTA_VERSION=v0.9.2-rc2 scripts/build.sh <variant> reconfigure build`. `POST /api/ota/check` runs the
daily check now.

## Publish a release

Merging a pull request into `main` publishes the release; nothing is pushed or released without the owner's request. The
`release` job of `.github/workflows/build.yml` runs after the five variants are built and packaged:

1. It reads `BETTAOS_RELEASE_VERSION` from the root `CMakeLists.txt`. Raise it in the pull request that is meant to release
   (`v0.9.2-rc2` for a test version, `v0.9.2` for a release) and describe it in `docs/release-notes.md`.
2. If a release with that name exists it does nothing, so merges without a version change publish nothing.
3. Otherwise it creates the tag on the merged commit and the GitHub release with the images, `SHA256SUMS`, the text of
   `scripts/release_body.sh` and GitHub's list of merged work. A version with a suffix (`-rc1`) is a pre-release.

`workflow_dispatch` runs the same steps; on any ref but `main`, or with `dry_run`, it stops before creating anything and
prints the release it would publish. To publish by hand instead: set the version,
`scripts/build.sh <variant> reconfigure build` for each board, `scripts/package_release.sh <version> <variant>...`
(`release/out/<version>/`) and
`gh release create <version> -R andre68723/BETTA-HA-PANEL --target <commit> --notes-file <notes>
release/out/<version>/*.bin release/out/<version>/SHA256SUMS`; `--target` must be the commit the images were built from.

Check a release: enable "Include pre-releases", `POST /api/ota/check`, read `update` in `GET /api/ota/status`. Install a
changed partition table over USB first.

Packaged versions stay in `release/out/` (git-ignored) until `scripts/clean.sh --release [N]` removes all but the newest N.

## Test the automatic update

Test builds carry a hook (`CONFIG_APP_AUTO_UPDATE_TEST_HOOK`, `config/sdkconfig.defaults.autoupdate_test`, never in a
release): `POST /api/ota/auto-update-test` with `{"force":true}`, `{"reset":true}` or `{"attempt":"<tag>"}`. The build
must report an older version than the release:

```bash
EXTRA_DEFAULTS=config/sdkconfig.defaults.autoupdate_test BETTA_VERSION=v0.9.2-rc2 scripts/build.sh <variant> reconfigure build
scripts/auto_update_device_test.sh <ip> install|rollback|state|reset
```

`install` must end with `result: installed`; with the old bootloader the state is `wait_no_rollback`. `rollback` stores
an attempt of `v0.9.2-rc2-bad`; flash a crashing image of that version, then `result` is `rolled_back` and
`blocked_version` names it. Delete `build/<variant>/sdkconfig` before rebuilding the normal image.
