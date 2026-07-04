# Home Assistant Notifications on BETTA HA Panel

This document describes the implemented Home Assistant notification overlay.
The feature lets Home Assistant, curl, or any local HTTP client post a message
to the panel. The panel wakes the display, shows the message above the current
dashboard, and dismisses it on tap or after an optional timeout.

## User-Facing Behavior

- Notifications are shown as a modal overlay on top of the current page.
- A semi-transparent scrim blocks touches to the dashboard behind it.
- The card contains an optional title, the message text, and a localized
  dismiss button.
- Tapping the scrim, card, or dismiss button closes the notification.
- `timeout_sec` controls automatic dismissal. `0` or a missing timeout keeps
  the notification visible until tapped.
- A new notification replaces the current notification in place.
- Long notifications can scroll inside the card.
- On large displays, such as 10 inch variants, long notifications use a wider
  and taller card. Smaller panels keep a compact card.

There is no notification history or missed-notification center yet. Home
Assistant remains the durable source for persistent notifications.

## HTTP API

Endpoint:

```http
POST /api/notifications
Content-Type: application/json
```

Example:

```bash
curl -X POST http://<panel-ip>/api/notifications \
  -H "Content-Type: application/json" \
  -d '{"title":"Laundry","message":"The washing machine is done.","message_id":"laundry_done","timeout_sec":30}'
```

Payload fields:

| Field | Required | Notes |
| --- | --- | --- |
| `message` | yes | Plain text message body. Empty messages are rejected. |
| `title` | no | Optional short heading. |
| `message_id` | no | Stable identifier for logging and update-in-place semantics. |
| `timeout_sec` | no | `0` or missing means persistent until tapped. Values are clamped to the configured maximum. |
| `priority` | no | Accepted for forward compatibility, currently ignored. |

Limits:

- Maximum JSON body: `APP_NOTIFICATION_MAX_JSON_LEN` (`2048` bytes).
- Maximum title: `APP_NOTIFICATION_MAX_TITLE_LEN` (`64` bytes).
- Maximum message: `APP_NOTIFICATION_MAX_MESSAGE_LEN` (`768` bytes).
- Maximum message ID: `APP_NOTIFICATION_MAX_ID_LEN` (`64` bytes).
- Maximum timeout: `APP_NOTIFICATION_MAX_TIMEOUT_SEC` (`3600` seconds).

The endpoint currently has no notification-specific authentication token. It is
protected only by the same local HTTP server guard used by other panel API
routes. A global API authentication model or optional notification token can be
added later.

## Home Assistant Setup

Home Assistant can call the endpoint through `rest_command`.

Example `configuration.yaml`:

```yaml
rest_command:
  betta_panel_notification:
    url: "http://<panel-ip>/api/notifications"
    method: POST
    content_type: "application/json"
    payload: >
      {
        "title": {{ title | default('') | to_json }},
        "message": {{ message | to_json }},
        "message_id": {{ message_id | default('') | to_json }},
        "timeout_sec": {{ timeout_sec | default(30) }}
      }
```

After adding or changing `rest_command`, restart Home Assistant or reload the
configuration path required by the local setup.

Manual test from Home Assistant Developer Tools -> Actions:

```yaml
action: rest_command.betta_panel_notification
data:
  title: "HA Test"
  message: "This notification comes from Home Assistant"
  message_id: "ha_manual_test"
  timeout_sec: 20
```

To mirror Home Assistant persistent notifications automatically:

```yaml
alias: Forward persistent notifications to BETTA panel
mode: queued
max: 10
triggers:
  - trigger: persistent_notification
    update_type:
      - added
      - updated
actions:
  - action: rest_command.betta_panel_notification
    data:
      title: "{{ trigger.notification.title | default('') }}"
      message: "{{ trigger.notification.message | default('') }}"
      message_id: "{{ trigger.notification.notification_id | default('') }}"
      timeout_sec: 30
```

Notifications created with `notify.persistent_notification` can then be mirrored
to the panel. If a Home Assistant version exposes different trigger variables,
the forwarding automation needs to be adjusted to match the actual
`persistent_notification` trigger data.

## Firmware Implementation

The feature is intentionally routed through the existing application event
pipeline instead of calling LVGL from the HTTP handler.

Implemented flow:

1. `api_notifications_post_handler()` reads and validates the JSON request.
2. It allocates an `app_notification_t` payload on the heap.
3. It copies `title`, `message`, `message_id`, and `timeout_sec` into the
   bounded payload fields.
4. It publishes `EV_NOTIFICATION_SHOW` with only a pointer in the event union.
5. Ownership transfers to the UI task once the event is queued successfully.
6. If event publishing fails, the HTTP handler frees the payload and returns
   `503`.
7. `ui_runtime` receives the event with the display lock held and calls
   `ui_notification_popup_show()`.
8. The notification popup copies/applies the text and frees the payload before
   returning. This happens for both create and update-in-place paths.

This keeps `app_event_t` small. The queue preallocates
`APP_EVENT_QUEUE_LENGTH * sizeof(app_event_t)`, so notification title/message
buffers are not stored inline in the event union.

If the display lock is temporarily unavailable, `ui_runtime` stores the pending
heap payload and retries from its normal UI task loop. A newer deferred
notification replaces an older deferred one and frees the older payload.

## UI Rendering

The overlay is implemented in `ui_notification_popup.c`.

Key details:

- Uses `lv_layer_top()` so it appears above all pages and widgets.
- Uses a full-screen clickable scrim for dismissal and touch blocking.
- Uses a card styled with existing BETTA theme colors.
- Uses `ui_i18n_get("notification.dismiss", "Dismiss")` for the button label.
- Calls `display_note_activity()` so incoming notifications wake/reset display
  idle handling.
- Uses an LVGL one-shot timer for `timeout_sec`.
- Enables card scrolling for long messages.
- Adapts card size based on actual screen dimensions:
  - larger displays get a wider card and more vertical space for long messages;
  - smaller displays keep the compact card.

## Text and Emoji Handling

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

## Current Limitations

- No notification history or notification center.
- No queue of visible notifications; the latest notification replaces the
  current one.
- No feature-specific authentication token yet.
- Closing a panel notification does not currently call
  `persistent_notification.dismiss` in Home Assistant.
- `priority` is accepted but ignored.
- Messages longer than the configured byte limits are truncated by bounded
  copies.

## Possible Follow-Ups

- Add global API authentication or an optional notification token.
- Add a notification settings section in the web UI.
- Add optional default timeout configuration.
- Add a small notification center for recently missed messages.
- Dismiss the original Home Assistant persistent notification by calling
  `persistent_notification.dismiss` when `message_id` maps to
  `notification_id`.
- Add custom Home Assistant acknowledgement or expired events if users need
  escalation automations.
