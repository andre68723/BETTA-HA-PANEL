#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Exercise the automatic update on a real panel, in minutes instead of a night. Needs a test build with the hook
# (config/sdkconfig.defaults.autoupdate_test) that reports an OLDER version than the published release; see docs/development/release.md,
# "Testing the automatic update".
#
#   scripts/auto_update_device_test.sh <panel-ip> state        show the panel's automatic update status
#   scripts/auto_update_device_test.sh <panel-ip> install      install the published newer release now (the happy path)
#   scripts/auto_update_device_test.sh <panel-ip> rollback     fake an attempt, then flash a crashing image by hand when asked (the rollback path)
#   scripts/auto_update_device_test.sh <panel-ip> reset        forget the stored attempt and blocked version
#
# Reads and writes the panel's settings (only ota.auto_install, which it switches on); set BETTA_PASSWORD when the panel has an admin password.
set -euo pipefail

[ $# -ge 2 ] || { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
ip=$1
action=$2
auth=()
[ -z "${BETTA_PASSWORD:-}" ] || auth=(-u "admin:$BETTA_PASSWORD")
api() { curl -s -m 15 "${auth[@]}" "$@"; }
post() { api -X POST -H 'Content-Type: application/json' -d "$2" "http://$ip$1"; }
field() { python3 -c "import sys,json; d=json.load(sys.stdin); print($1)" 2>/dev/null || echo "?"; }

status_line() {
    api "http://$ip/api/ota/status" | field "'auto_update', json.dumps(d.get('auto_update')), '| update:', d['update'].get('version'), 'available' if d['update'].get('available') else 'not-available'"
}
running() { api "http://$ip/api/version" | field "d['version'], 'image:', d.get('ota_image_state'), 'bootloader-rollback:', d.get('bootloader',{}).get('rollback'), 'up', d.get('uptime_s'), 's'"; }
wait_http() { for _ in $(seq 1 60); do [ "$(curl -s -m 4 -o /dev/null -w '%{http_code}' "http://$ip/api/version")" = 200 ] && return 0; sleep 4; done; return 1; }
require_hook() {
    api "http://$ip/api/ota/status" | python3 -c "import sys,json; sys.exit(0 if json.load(sys.stdin).get('auto_update',{}).get('test_hook') else 1)" ||
        { echo "this panel runs a build without the test hook (build with EXTRA_DEFAULTS=config/sdkconfig.defaults.autoupdate_test)" >&2; exit 1; }
}

case $action in
state)
    running
    status_line
    ;;
reset)
    require_hook
    post /api/ota/auto-update-test '{"reset":true,"force":false}' >/dev/null
    status_line
    ;;
install)
    require_hook
    echo "before: $(running)"
    api -X PUT -H 'Content-Type: application/json' -d '{"ota":{"update_check":true,"include_prereleases":true,"auto_install":true},"reboot":false}' "http://$ip/api/settings" >/dev/null
    post /api/ota/auto-update-test '{"reset":true,"force":false}' >/dev/null
    post /api/ota/check '{}' >/dev/null || true
    for _ in $(seq 1 12); do
        api "http://$ip/api/ota/status" | python3 -c "import sys,json; sys.exit(0 if json.load(sys.stdin)['update'].get('available') else 1)" && break
        sleep 5
    done
    echo "check: $(status_line)"
    post /api/ota/auto-update-test '{"force":true}' >/dev/null
    echo "forced; waiting for the install and the restart ..."
    sleep 20
    for _ in $(seq 1 90); do
        r=$(api "http://$ip/api/ota/status" | field "d.get('state'), d.get('progress')" || true)
        echo "  $r"
        case "$r" in *success*) break;; esac
        sleep 3
    done
    wait_http || { echo "the panel did not come back" >&2; exit 1; }
    sleep 75 # the 60 s check of the new image
    echo "after:  $(running)"
    echo "result: $(status_line)"
    ;;
rollback)
    require_hook
    echo "before: $(running)"
    post /api/ota/auto-update-test '{"reset":true,"attempt":"v0.9.1-rc1-bad"}' >/dev/null
    echo "attempt stored: $(status_line)"
    echo "Now flash the crashing image (version v0.9.1-rc1-bad) with scripts/ota_flash.sh $ip <image>; the panel should crash, fall back and report rolled_back."
    echo "Then:  scripts/auto_update_device_test.sh $ip state   (after about two minutes; the panel shows a notice after about 90 s)"
    ;;
*)
    echo "unknown action $action" >&2
    exit 2
    ;;
esac
