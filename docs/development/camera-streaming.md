<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- see docs/release-notes.md -->
# Camera streaming

User setup: [Camera](../widgets/camera_tile.md) and [Bambu printer](../widgets/bambu_printer.md).
Live mode is opt-in (`camera_live: true` on a Camera widget). Existing layouts
keep the snapshot timer and tap-to-refresh behaviour. A Bambu widget's optional
`camera_entity_id` adds a live-view action in its Status popup.

## Transport and decoding

Still images use the shared cover worker. On the configured HA origin, camera
proxy URLs drop their rotating `token` query parameters and authenticate with the
panel's Bearer credential. Other query parameters are preserved. Missing credentials
stop the request; foreign origins never receive the panel credential. Bearer requests
disable redirects. Signed media URLs outside the camera proxy keep their own token.

Only the latest image request for a consumer can complete. An HA disconnect cancels
queued/in-flight camera callbacks; cancelled requests are checked before the HTTP gate
opens. A rejected Bearer stops later image requests, including MJPEG workers, through
the shared auth guard. The popup's explicit reconnect is queued to the HA task: it stops
the old transport before clearing that guard, with a 15-second user retry cooldown.

`ha/ha_camera_stream.c` requests `/api/camera_proxy_stream/<camera entity>`
from the configured Home Assistant origin. It accepts no arbitrary URL, disables
redirects and keeps the bearer token in the worker context only. It does not
connect to the printer or negotiate RTSP, HLS, WebRTC or audio.

`ha/mjpeg_parser.c` reads bounded multipart JPEG parts with `Content-Length`.
The parser accepts standard boundary parameters and HA's delimiter convention,
including quoted parameters, and rejects unsupported content, oversized headers,
duplicate lengths and incomplete JPEG markers. Each encoded frame is limited
to 1 MiB. A stalled or endlessly incomplete frame is bounded by a 15-second
progress/frame deadline; failed connections retry at most three times until a
new frame succeeds. Authentication failures and redirects are not retried.

Two workers may run concurrently. Each has an 8 KiB PSRAM stack, one encoded
buffer and a mailbox for one decoded frame. Unconsumed frames are dropped,
decoding is capped at five frames per second, and the view requests at most
960 × 640 output. Source resolution remains subject to the shared decoder's
pixel cap. These are resource limits, not measured frame rates.

`ha_cover_fetcher_decode_jpeg()` serializes hardware JPEG access with the existing
still-image worker. Network and decoding run outside the display lock. Live
requests yield to HA heavy work and internal-memory pressure. Their frame
mailboxes transfer buffer ownership to the UI, without worker-to-LVGL callbacks.

## UI ownership

`ui/ui_camera_view.c` supplies the shared live feed and full-screen view. Its
LVGL timer takes completed frames without waiting on the mailbox mutex. It
invalidates the image cache before replacing/freeing a descriptor's buffer.
Hidden views, overlays above the view and display protection stop their stream;
the newest foreground view takes precedence. Connection messages remain visible
when the retained picture is no longer live.

Deleting a feed cancels its worker without waiting for HTTP and releases its
consumer reference. The worker retains its own reference until cleanup, never
an LVGL or widget pointer. Page hiding/deletion closes owned full-screen views.

## Checks

- `tools/test_host.sh`: fragmented multipart parsing, malformed and oversized
  input, URL/entity validation, real-worker HTTP failures, silence deadlines,
  cancellation inside a read, mailbox contention and buffer/reference cleanup.
  URL/auth policy checks cover expired camera query tokens, preserved size parameters,
  foreign/lookalike origins and signed media URLs. Retry checks cover explicit requests,
  renewed rejection and cooldown without an automatically scheduled attempt.
- `node --test tests/webui/*.test.mjs`: real Inspector editing events,
  enable/disable, save/reload, camera discovery binding and backup preservation.
- `scripts/simulator.sh panel4 --regression controls`: Camera snapshot defaults,
  repeated frame replacement, foreground views, hide/sleep and owner deletion.
  The Bambu regression covers Status → Live camera and owner teardown.

Simulator frames and HTTP responses are synthetic. Firmware builds establish
compilation only.

On 2026-10-16, an original Guition JC8012P4A1 with ESP32-P4 v1.3 received a
Bambu A1 camera image through Home Assistant in the live tile, including after
leaving and returning to its page. The tested build was `4d419467-dirty`, stamped
`Oct 16 2026 08:07:04`. Its OTA health check passed; at 186 seconds uptime, HA
was authenticated, neither watchdog reported a stall, no crash report was
retained, and the camera task's minimum free stack was 6,208 of 8,192 bytes.
The capture verifies LVGL rendering. Source freshness, frame rate, physical
touch/full-screen behaviour and long-term stability were not measured.
