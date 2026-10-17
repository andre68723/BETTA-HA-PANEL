/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * httpd_query_key_value() hands back query values still URL-encoded ("K%C3%BCche", "Flur+OG").
 */
#pragma once

/* Decodes %XX and '+' in place; a malformed or %00 escape is kept as it is. */
void url_decode_in_place(char *s);
