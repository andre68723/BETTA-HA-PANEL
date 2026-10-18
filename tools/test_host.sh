#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
#
# Host tests (tests/host): pure logic compiled with the system C compiler, no device, no ESP-IDF run.
# VARIANT selects the panel geometry. Uses a local cJSON source if fetched; otherwise the system
# libcjson development package (used in CI). No ESP-IDF build or generated sdkconfig is required.
set -eu
cd "$(dirname "$0")/.."
variant="${VARIANT:-panel4}"
cjson="managed_components/espressif__cjson/cJSON"
case "$variant" in
  panel4) variant_flags=(-DCONFIG_IDF_TARGET_ESP32P4=1 -DCONFIG_APP_PANEL_VARIANT_4INCH_720=1) ;;
  tab5) variant_flags=(-DCONFIG_IDF_TARGET_ESP32P4=1 -DCONFIG_APP_PANEL_VARIANT_TAB5_1280=1) ;;
  panel10) variant_flags=(-DCONFIG_IDF_TARGET_ESP32P4=1 -DCONFIG_APP_PANEL_VARIANT_10INCH_1280=1) ;;
  paneljc) variant_flags=(-DCONFIG_IDF_TARGET_ESP32P4=1 -DCONFIG_APP_PANEL_VARIANT_10INCH_JC=1 -DCONFIG_APP_PANELJC_PANEL_V2=0) ;;
  paneljc-v2) variant_flags=(-DCONFIG_IDF_TARGET_ESP32P4=1 -DCONFIG_APP_PANEL_VARIANT_10INCH_JC=1 -DCONFIG_APP_PANELJC_PANEL_V2=1) ;;
  *) echo "unknown host-test variant: $variant"; exit 2 ;;
esac

cjson_sources=()
cjson_flags=()
cjson_libs=()
if [ -f "$cjson/cJSON.c" ]; then
  cjson_sources=("$cjson/cJSON.c")
  cjson_flags=(-I "$cjson")
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcjson; then
  read -r -a cjson_flags <<< "$(pkg-config --cflags libcjson)"
  read -r -a cjson_libs <<< "$(pkg-config --libs libcjson)"
else
  echo "need managed_components/espressif__cjson or the system libcjson development package"
  exit 2
fi
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_main.c tests/host/test_energy_timeline_colors.c main/ui/theme/matte_title.c main/ui/theme/ui_style_registry.c main/settings/brightness_model.c main/settings/settings_restart.c main/settings/settings_validate.c main/layout/layout_validate.c main/layout/bambu_printer_slots.c main/layout/evcc_slots.c main/layout/layout_orientation.c main/layout/widget_types.c main/util/atomic_file.c tests/host/flash_gate_stub.c main/util/cover_scale.c main/drivers/display_policy.c main/util/json_util.c main/util/cover_guard.c main/util/boot_guard.c main/util/entity_id.c main/util/distance_units.c main/util/iso_time.c main/util/room_extra.c main/util/ota_health.c main/util/auto_update.c main/util/sha256_soft.c main/util/auto_update_state.c main/util/user_activity.c main/ui/nav_layout.c main/util/reboot_log.c main/util/net_watchdog.c main/util/ui_watchdog.c main/util/ota_chip_rev.c main/util/history_feed_policy.c main/util/chart_scale.c main/ha/energy_timeline_model.c main/util/url_decode.c main/util/notification_parse.c main/ui/notification_text.c main/util/chime_synth.c main/ha/ha_attrs_compact.c main/ha/ha_attr_serialize.c main/ha/ha_price_series.c main/ha/ha_history_stats.c main/ha/ha_layout_entities.c main/ha/ha_display_precision.c main/ha/ha_discovery_domains.c main/ha/ha_registry_discovery.c main/ha/ha_auth_state.c main/ha/ha_select_options.c main/ha/ha_control_options.c main/ha/ha_datetime_value.c main/ha/ha_history_reduce.c main/ha/ha_lock_state.c main/ha/ha_alarm_state.c main/ha/ha_room_registry.c main/ui/widgets/weather_value_parse.c main/ui/widgets/weather_condition_icon.c main/net/wifi_signal_level.c main/net/setup_passphrase.c main/net/update_check_parse.c "${cjson_sources[@]}" \
  "${cjson_libs[@]}" -lm -o "$out/test_host"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main \
  tests/host/test_display_preview.c main/drivers/display_policy.c main/settings/brightness_model.c \
  -o "$out/test_display_preview"
"$out/test_display_preview"
cc -std=gnu11 -Wall -Wextra -Werror -g -I main \
  tests/host/test_display_rotation.c main/drivers/display_rotation.c \
  -o "$out/test_display_rotation"
"$out/test_display_rotation"
"$out/test_host"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  -DCONFIG_IDF_TARGET_ESP32P4=1 "${cjson_flags[@]}" \
  tests/host/test_ha_controls.c main/ha/ha_registry_discovery.c main/ha/ha_control_options.c \
  main/ha/ha_attr_serialize.c main/ha/ha_price_series.c main/ha/ha_history_stats.c main/util/iso_time.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_ha_controls"
"$out/test_ha_controls"

# Electricity price lists: both source shapes, the regular grid, the attribute budget, day statistics and cheap windows.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  -DCONFIG_IDF_TARGET_ESP32P4=1 "${cjson_flags[@]}" \
  tests/host/test_price_series.c main/ha/ha_price_series.c main/ha/ha_history_stats.c main/util/iso_time.c \
  main/ha/ha_attr_serialize.c main/ha/ha_control_options.c main/ha/ha_registry_discovery.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_price_series"
TZ=Europe/Berlin "$out/test_price_series"

# Touch input: the contact tracker with its counters and the queue that hands every tap to LVGL.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  tests/host/test_touch.c main/drivers/touch_track.c main/drivers/touch_latch.c -o "$out/test_touch"
"$out/test_touch"

# Parqet portfolio sensors: the total value attributes (top holdings, interval), numbers, gain percentage and interval labels.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  -DCONFIG_IDF_TARGET_ESP32P4=1 "${cjson_flags[@]}" \
  tests/host/test_parqet.c main/ha/parqet_portfolio.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_parqet"

# The reply of parqet/get_performance reduced for the portfolio widget: figures, holdings of the interval and the value curve.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  -DCONFIG_IDF_TARGET_ESP32P4=1 "${cjson_flags[@]}" \
  tests/host/test_parqet_perf.c main/ha/parqet_performance.c main/ha/parqet_portfolio.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_parqet_perf"
"$out/test_parqet_perf"
"$out/test_parqet"

# Exercise Tab5 geometry and orientation independently of the default host variant.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  -DCONFIG_IDF_TARGET_ESP32P4=1 -DCONFIG_APP_PANEL_VARIANT_TAB5_1280=1 "${cjson_flags[@]}" \
  tests/host/test_orientation.c main/drivers/orientation_policy.c main/ui/display_geometry.c \
  main/layout/layout_orientation.c main/layout/widget_types.c "${cjson_sources[@]}" \
  "${cjson_libs[@]}" -lm -o "$out/test_orientation"
"$out/test_orientation"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main \
  tests/host/test_cover_request_lifetime.c main/ha/cover_request_lifetime.c \
  -o "$out/test_cover_request_lifetime"
"$out/test_cover_request_lifetime"

cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g -I main \
  tests/host/test_mjpeg_parser.c main/ha/mjpeg_parser.c -o "$out/test_mjpeg_parser"
"$out/test_mjpeg_parser"

cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g -I tests/host/camera_stubs -I main \
  tests/host/test_camera_stream.c main/ha/ha_camera_stream.c main/ha/mjpeg_parser.c -o "$out/test_camera_stream"
"$out/test_camera_stream"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  "${variant_flags[@]}" "${cjson_flags[@]}" tests/host/test_camera_layout.c \
  main/layout/layout_validate.c main/layout/widget_types.c main/layout/layout_orientation.c \
  main/layout/bambu_printer_slots.c main/layout/evcc_slots.c \
  main/ha/ha_alarm_state.c main/util/entity_id.c main/util/room_extra.c main/util/chime_synth.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_camera_layout"
"$out/test_camera_layout"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs \
  "${variant_flags[@]}" "${cjson_flags[@]}" tests/host/test_price_layout.c \
  main/layout/layout_validate.c main/layout/widget_types.c main/layout/layout_orientation.c \
  main/layout/bambu_printer_slots.c main/layout/evcc_slots.c main/ha/ha_layout_entities.c \
  main/ha/ha_alarm_state.c main/util/entity_id.c main/util/room_extra.c main/util/chime_synth.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_price_layout"
"$out/test_price_layout"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main \
  tests/host/test_text_scale.c main/ui/text_scale.c \
  -o "$out/test_text_scale"
"$out/test_text_scale"

# Alarm panel state: parsing, actions per state and feature set, when a code is required.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main "${cjson_flags[@]}" \
  tests/host/test_alarm_state.c main/ha/ha_alarm_state.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -o "$out/test_alarm_state"
"$out/test_alarm_state"

# Recorder refreshes must not reclassify previously imported graph samples as live.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main \
  tests/host/test_graph_history_merge.c -lm -o "$out/test_graph_history_merge"
"$out/test_graph_history_merge"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_bambu_printer.c main/ui/widgets/bambu_printer_policy.c main/layout/bambu_printer_slots.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_bambu_printer"
"$out/test_bambu_printer"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_printer_lookup.c main/ha/ha_printer_lookup.c main/layout/bambu_printer_slots.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_printer_lookup"
"$out/test_printer_lookup"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main \
  tests/host/test_notification_text.c main/ui/notification_text.c \
  -o "$out/test_notification_text"
"$out/test_notification_text"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main \
  tests/host/test_notification_markup.c main/ui/notification_markup.c main/ui/notification_text.c \
  -o "$out/test_notification_markup"
"$out/test_notification_markup"

# Built-in DE/EN translations: strict JSON, one "lvgl" root, equal keys, working ui_i18n_get() lookup.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_i18n_builtin.c main/settings/i18n_store.c main/ui/ui_i18n.c main/util/atomic_file.c tests/host/flash_gate_stub.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_i18n_builtin"
"$out/test_i18n_builtin"

# Energy dashboard preferences: flat grid sources, power-only grid, battery power sensors, two solar sources, legacy flow arrays.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main "${cjson_flags[@]}" \
  tests/host/test_energy_prefs.c main/ha/ha_energy_prefs.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_energy_prefs"
"$out/test_energy_prefs"

# Recorder statistics: metadata and hourly reply parsing, eligibility, windows and local-day sums across DST.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main "${cjson_flags[@]}" \
  tests/host/test_history_stats.c main/ha/ha_history_stats.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_history_stats"
"$out/test_history_stats"

# The real 7-day history worker with pthread-backed FreeRTOS stubs and a scriptable WebSocket: busy gate, silence, late
# and failing replies, partial sources, units, queue overflow and no allocation left behind after a thousand requests.
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g -pthread \
  -DWEEK_REPLY_TIMEOUT_MS=300 -DWEEK_GATE_POLL_MS=10 -DWEEK_CHECK_GATE_WAIT_MS=100 -DWEEK_REQUEST_GATE_WAIT_MS=100 \
  -I tests/host/week_stubs -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_history_week.c main/ha/ha_history_week.c main/ha/ha_history_stats.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_history_week"
"$out/test_history_week"

# The admin password store against a fake NVS with injected faults (finding 16: an unreadable credential closes the panel).
cc -std=gnu11 -Wall -Wextra -Werror -g -I tests/host/auth_stubs -I main \
  tests/host/test_admin_auth.c -o "$out/test_admin_auth"
"$out/test_admin_auth"

# Crash evidence: two-report rotation, raw-byte downloads, failed writes, restart/dedup and individual deletion.
cc -std=gnu11 -Wall -Wextra -Werror -g -I main "${cjson_flags[@]}" \
  tests/host/test_coredump_store.c main/util/coredump_store.c main/util/coredump_diagnostics.c main/util/sha256_soft.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -o "$out/test_coredump_store"
"$out/test_coredump_store"

# Transactional settings and preservation on storage failures, using the actual stores.
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -I tests/host/storage_stubs -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_runtime_storage.c main/util/sha256_soft.c main/ui/text_scale.c main/settings/brightness_model.c main/ui/theme/ui_style_registry.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_runtime_storage"
"$out/test_runtime_storage"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_graph_history_store.c main/ui/graph_history_store.c main/util/atomic_file.c \
  tests/host/flash_gate_stub.c main/util/transfer_deadline.c "${cjson_sources[@]}" "${cjson_libs[@]}" \
  -o "$out/test_graph_history_store"
"$out/test_graph_history_store"

# Actual automatic-update worker: no OTA starts before the durable attempt is committed.
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -I tests/host/storage_stubs -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_auto_update_storage.c main/util/auto_update.c main/util/auto_update_state.c main/util/ota_health.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_auto_update_storage"
"$out/test_auto_update_storage"

cc -std=gnu11 -Wall -Wextra -Werror -g -I tests/host/storage_stubs -I main -I tests/host/stubs "${variant_flags[@]}" \
  tests/host/test_config_events.c main/app_events.c main/util/flash_gate.c main/util/atomic_file.c \
  -o "$out/test_config_events"
"$out/test_config_events"

cc -std=gnu11 -Wall -Wextra -Werror -g -I tests/host/storage_stubs -I main \
  tests/host/test_ota_transfer.c main/api/ota_transfer.c main/util/transfer_deadline.c main/util/sha256_soft.c \
  -o "$out/test_ota_transfer"
"$out/test_ota_transfer"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  -DHA_EVCC_SESSIONS_TEST_HOOKS=1 -Dtime=ha_evcc_sessions_test_time \
  tests/host/test_evcc.c tests/host/test_evcc_sessions.c main/ha/ha_evcc_sessions.c main/util/iso_time.c main/layout/layout_validate.c main/ha/ha_alarm_state.c main/layout/widget_types.c main/layout/layout_orientation.c main/util/entity_id.c main/util/room_extra.c main/util/chime_synth.c main/layout/bambu_printer_slots.c main/ha/ha_layout_entities.c main/ha/ha_evcc_lookup.c main/ha/ha_evcc_value.c main/layout/evcc_slots.c main/ha/ha_select_options.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_evcc"

cc -std=gnu11 -Wall -Wextra -Werror -g -I main -I tests/host/stubs "${variant_flags[@]}" "${cjson_flags[@]}" \
  tests/host/test_backup_layout.c main/layout/layout_validate.c main/ha/ha_alarm_state.c main/layout/widget_types.c main/layout/layout_orientation.c main/util/entity_id.c main/util/room_extra.c main/util/chime_synth.c main/layout/bambu_printer_slots.c main/layout/evcc_slots.c main/ha/ha_layout_entities.c main/ha/ha_select_options.c \
  "${cjson_sources[@]}" "${cjson_libs[@]}" -lm -o "$out/test_backup_layout"
"$out/test_backup_layout"
"$out/test_evcc"
