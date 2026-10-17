/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Content checks for runtime settings strings that the length limits in api_settings.c do not cover.
 * Pure functions, host-tested. An empty string is accepted by every check: it means "use the default" or "unset".
 */
#pragma once

#include <stdbool.h>

/* NTP server or MQTT broker host: a host name, IPv4 or bracket-free IPv6 literal ([A-Za-z0-9._:-], no spaces). */
bool settings_host_valid(const char *host);
/* POSIX TZ string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3", "<+03>-3"): printable ASCII only. */
bool settings_timezone_valid(const char *tz);
/* MQTT topic prefix: printable ASCII without spaces, no '+' or '#' wildcards, no leading, trailing or doubled '/'. */
bool settings_topic_prefix_valid(const char *prefix);
/* Free text shown to people (room, device name): no control characters. */
bool settings_text_valid(const char *text);
