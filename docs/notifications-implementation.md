<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Notification implementation

For setup, alerts and sound controls, see [Notifications](notifications.md).
This page holds the payload and topic reference and records the event and rendering path for contributors.

## Message payload

The fields apply to MQTT JSON and `POST /api/notifications` alike.

| Field | Required | Notes |
| --- | --- | --- |
| `message` | yes | Message body with optional `**bold**` / `__bold__` markers. Empty messages are rejected. |
| `title` | no | Optional short heading. |
| `message_id` | no | Stable identifier for logging and update-in-place semantics. |
| `action_entity_id` | no | Adds a large button to the card (for example "Open door"): a `button.*` / `input_button.*` is pressed, a `lock.*` is unlocked (or opened, see `action_service`), anything else (script, scene) is turned on. The card closes afterwards; for a lock it stays 6 s and the button shows the lock's state ("Unlocked", "Jammed") when the lock is in the layout (for example as a lock widget), otherwise "Sent". |
| `action_label` | no | Text of that button, up to 23 bytes of UTF-8 (the buffer holds 24 with the terminator). |
| `action_service` | no | For a `lock.*` action only: `unlock` (default) or `open`, which unlatches the door (`lock.open`) on locks that support it, such as a Nuki. Any other value, or `open` without a lock, is rejected. |
| `camera_entity_id` | no | Shows a snapshot of this camera in the card, refreshed every 3 seconds while it is open. The camera has to be known to the panel (it is when it is listed in an alert or used by a camera tile). |
| `timeout_sec` | no | `0` or missing means persistent until tapped. Values are clamped to the configured maximum. |
| `sound` | no | `chime` (ding-dong), `notify` (short tone), or `none` (default). Respects Settings > Sound: enable, volume and night mute. Invalid values are rejected. |
| `priority` | no | Accepted for forward compatibility, currently ignored. |

Limits: JSON body `APP_NOTIFICATION_MAX_JSON_LEN` (2048 bytes), title `APP_NOTIFICATION_MAX_TITLE_LEN` (64 bytes),
message `APP_NOTIFICATION_MAX_MESSAGE_LEN` (768 bytes), message ID `APP_NOTIFICATION_MAX_ID_LEN` (64 bytes) and
timeout `APP_NOTIFICATION_MAX_TIMEOUT_SEC` (3600 seconds). The sizes include the terminator: usable text is 63 bytes
for the title, 767 for the message and 23 for the action label, counted in bytes, not characters. A longer text field is
cut before the whole character or emoji unit that does not fit (an emoji with selector or skin tone, a flag, a keycap,
a joined sequence or a tag sequence), so it never ends in half a sequence. The same cut applies to the title, message and
action label of layout alerts. Ids are cut at a character boundary. An oversized body is rejected before parsing, and
chunked MQTT deliveries are reassembled in a bounded buffer.

Rendering uses the existing sanitizer followed by the bounded, allocation-free `ui/notification_markup.c` parser.
`ui/notification_message.c` keeps the ordinary label for plain messages and creates a width-constrained LVGL span group
for emphasis, with at most 32 runs. Runs copy their text and are cleared on replacement. Bold fonts use the base font's
metrics and emoji fallback; allocation/budget failure retains the base font. Dismissal, camera/action controls and
last-message recall share the existing popup lifecycle.

## HTTP responses

`POST /api/notifications` with `Content-Type: application/json`. Success returns `{"ok":true}` (the message was
queued for display); invalid JSON returns 400, missing or wrong HTTP credentials 401, another content type 415 and a
full event queue 503. With an admin password add `--user admin` to curl:

```bash
curl -X POST http://<panel-ip>/api/notifications \
  -H "Content-Type: application/json" \
  -d '{"title":"Laundry","message":"The washing machine is done.","message_id":"laundry_done","timeout_sec":30}'
```

## MQTT topics and discovery

`<prefix>` is the configured topic prefix (default `betta_ha_panel`) and `<device_id>` is derived from the room set
in Settings > Device (`Arbeitszimmer` becomes `betta_ha_panel_arbeitszimmer`), or from the Wi-Fi station MAC as
`panel-XXYY` while no room is set. The Home Assistant device name defaults to `BETTA <room>`; the MQTT "device name"
setting overrides it. Changing the room creates a new device in Home Assistant. The MQTT password is stored in NVS
only and never echoed back; TLS uses the ESP-IDF certificate bundle.

| Topic | Direction | Purpose |
| --- | --- | --- |
| `<prefix>/<device_id>/status` | panel → broker | Availability, retained: `online` on connect, `offline` via last will. |
| `<prefix>/<device_id>/notify/set` | broker → panel | Command topic. JSON payload with the same fields as `POST /api/notifications`. |
| `homeassistant/notify/<device_id>/config` | panel → broker | Retained discovery config for a `notify` entity. |
| `homeassistant/binary_sensor/<device_id>/config` | panel → broker | Retained discovery config for an "Online" connectivity sensor. |
| `homeassistant/sensor/<device_id>/ip/config` | panel → broker | Retained discovery config for an "IP address" diagnostic sensor. |
| `<prefix>/<device_id>/ip` | panel → broker | Current station IPv4 address, retained; republished on every connect. |

After the first connection Home Assistant shows a device with the configured name, model = board description,
software version = firmware version, the Wi-Fi MAC address, a **Visit device** link to the web editor
(`configuration_url`), a **Notify** entity, an **Online** sensor and an **IP address** diagnostic sensor. The MQTT
client only starts when MQTT is enabled and a broker host is set; connection changes restart the panel. Home
Assistant's MQTT `notify` platform forwards only the message text (it renders `command_template` with the variable
`value`), so a title or timeout needs `mqtt.publish`. Several panels on one broker get distinct topics and
`unique_id`s; give each its own device name. `GET /api/settings` returns `mqtt.command_topic`.

## Alert fields

Alerts configured under Settings > Alerts are stored as `"alerts"` in the layout JSON (up to four):
`entity_id` (an `event.*` entity fires when its state changes, any other entity when it turns `on`), `title`,
`message`, `sound` (`chime`, `notify` or `none`, the default for existing alerts), `timeout_sec` (default 30),
`action_entity_id`, `action_label` and `camera_entity_id`. The state at start-up or layout save is only remembered.
Tested with a motion sensor and test notifications, not with a real doorbell.

## Firmware implementation

The feature is intentionally routed through the existing application event
pipeline instead of calling LVGL from the HTTP handler.

Implemented flow:

1. The entry point reads the raw JSON: `api_notifications_post_handler()` for
   HTTP, the `MQTT_EVENT_DATA` handler in `main/net/mqtt_mgr.c` for the
   command topic (after reassembling chunked deliveries).
2. `notification_parse_json()` (`main/util/notification_parse.c`) validates
   the payload and allocates an `app_notification_t` on the heap, copying
   `title`, `message`, `message_id`, and `timeout_sec` into bounded fields.
3. The entry point publishes `EV_NOTIFICATION_SHOW` with only a pointer in the
   event union.
4. Ownership transfers to the UI task once the event is queued successfully.
5. If event publishing fails, the caller frees the payload; the HTTP handler
   additionally returns `503`.
6. `ui_runtime` receives the event with the display lock held and calls
   `ui_notification_popup_show()`.
7. The notification popup copies/applies the text and frees the payload before
   returning. This happens for both create and update-in-place paths.

This keeps `app_event_t` small. The queue preallocates
`APP_EVENT_QUEUE_LENGTH * sizeof(app_event_t)`, so notification title/message
buffers are not stored inline in the event union.

If the display lock is temporarily unavailable, `ui_runtime` stores the pending
heap payload and retries from its normal UI task loop. A newer deferred
notification replaces an older deferred one and frees the older payload.

## UI rendering

The overlay is implemented in `ui_notification_popup.c`.

Key details:

- Uses `lv_layer_top()` so it appears above all pages and widgets.
- Uses a full-screen clickable scrim for dismissal and touch blocking.
- Uses a card styled with existing BETTA theme colors.
- Uses the shared round X close button from `ui_popup.c`.
- Calls `display_note_activity()` so incoming notifications wake/reset display
  idle handling.
- Uses an LVGL one-shot timer for `timeout_sec`.
- Enables card scrolling for long messages.
- Adapts card size based on actual screen dimensions:
  - larger displays get a wider card and more vertical space for long messages;
  - smaller displays keep the compact card.

## Text and emoji handling

LVGL text fonts in this firmware do not include full emoji glyph coverage, and
adding a real emoji font would be expensive for flash/RAM and still would not
provide color emoji rendering.

The popup therefore sanitizes title and message text before assigning it to
LVGL labels:

- normal UTF-8 text is preserved;
- variation selectors and zero-width joiners are removed;
- common emoji/symbol ranges are removed to avoid missing-glyph boxes;
- the left-right arrow `↔` is replaced with `~`;
- leading spaces left behind by removed emoji prefixes are trimmed per line.

This keeps Home Assistant messages readable even when they contain emoji
prefixes such as weather, solar, robot, or status icons.

## Audio implementation

Tones are synthesized at 16 kHz, mono 16 bit: a 1.2-second ding-dong or a 0.3-second notification.
One temporary task owns playback; another request restarts it after the current short I2S queue
has drained. PCM uses PSRAM (38.4 KB for ding-dong); codec handles, I2S and its 4 x 256-frame
DMA buffers are released after each clip. The microphone is not initialized.

## Not implemented

- Add an optional notification-specific token for the HTTP path. (The optional admin
  password already protects `/api/notifications` with HTTP Basic, like the rest of the API.)
- Add optional default timeout configuration.
- Add a small notification center for recently missed messages.
- Dismiss the original Home Assistant persistent notification by calling
  `persistent_notification.dismiss` when `message_id` maps to
  `notification_id`.
- Add custom Home Assistant acknowledgement or expired events if users need
  escalation automations.
