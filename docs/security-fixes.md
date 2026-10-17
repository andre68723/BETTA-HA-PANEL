<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Security fixes

Security changes of the fork compared with upstream v0.8.2, from source reviews up to 2026-10-07. The current state and
what is still open are in [Security implementation](security-implementation.md); the rest of the changes are in the
[release notes](release-notes.md).

| Area | v0.8.2 | v0.9.0 |
| --- | --- | --- |
| Home Assistant token and MQTT password | A changed `ha.ws_url` or `mqtt.broker_host` sent the stored secret to the new address, so anyone on the LAN could redirect it | A changed address needs the secret in the same request (`400` otherwise); the stored one is never sent to it |
| Cross-site requests | A page could post `text/plain` or a bodyless form to the panel | POST and PUT need `Content-Type: application/json` (`application/octet-stream` for the upload), `415` otherwise; no CORS answer |
| Cross-origin reads | `Access-Control-Allow-Origin: *` on the GET endpoints | Removed everywhere |
| Editor scripting and framing | No Content-Security-Policy, no protection against framing the editor | Content-Security-Policy without inline script, `frame-ancestors 'none'`, `X-Frame-Options`, `nosniff`, no referrer; preview text from titles and Home Assistant states is escaped |
| Home Assistant token to other servers | Cover and camera fetches followed an absolute `entity_picture` URL with the token, the Host override and the pinned name | Only URLs on the Home Assistant origin carry them; redirects are not followed when the token is sent |
| Cover PNG parser | A chunk length near `0xFFFFFFF5` wrapped in 32-bit arithmetic, so a crafted cover could crash the panel | Bounds check without wrap-around; images above 9,000,000 (streamed) or 2,073,600 (full decode) pixels are skipped |
| Admin authentication | None: editor and API open to the whole network | Optional password (HTTP Basic), 6 wrong attempts lock a client for 60 s |
| Setup access point | Open, no password | WPA2 with a random two-word, two-digit passphrase per start, shown on the display |
| Notification actions | An action id was pasted into the service-call JSON unchecked | Plain entity id in a fixed list of domains; anything else is dropped; layout alerts are validated the same way |
| Home Assistant login ban | A rejected token was retried and could trigger Home Assistant's IP ban | A rejected token stops requests until a restart |
| Firmware update | No digest comparison for releases, no rollback of an unvalidated image, redirects could leave HTTPS | Release digest comparison, rollback on an image that does not mark itself valid, HTTPS-only redirects |
