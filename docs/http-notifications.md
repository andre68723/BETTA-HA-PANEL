<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# HTTP notifications

This optional route sends messages without an MQTT broker. It requires editing Home Assistant YAML.
For editor-configured doorbell alerts or graphical MQTT notification actions, use [Notifications](notifications.md).

## Send from Home Assistant

**1. Add a command** to `configuration.yaml` and restart or reload Home Assistant:

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

Keep the panel's address stable with a DHCP reservation or hostname. If you set an
[admin password](editor.md#settings), add `username: admin` and `password: "<password>"` to the entry.

**2. Send a message**, for example from *Developer tools → Actions* or an automation:

```yaml
action: rest_command.betta_panel_notification
data:
  title: "Laundry"
  message: "The washing machine is done."
  timeout_sec: 30
```

## What a message can contain

| Option | What it does |
| --- | --- |
| `message` | The text (required). |
| `title` | A short heading. |
| `timeout_sec` | Seconds until it disappears; `0` or missing keeps it until tapped. |
| `sound` | `chime` (ding-dong), `notify` (short tone) or `none`; see [Sound](notifications.md#sound). |
| `action_entity_id`, `action_label` | A large button, for example *Open door*: presses a button, unlocks a lock or turns on a script or scene. |
| `camera_entity_id` | A snapshot of that camera in the card, refreshed every 3 seconds while it is open. |
| `message_id` | A name for the message, useful in logs. |

Keep titles and messages short so they stay readable on the smallest panel. Box-drawing lines become dashes (a separator line stays a line), `✓` becomes `✔` and `Δ` becomes `±`. See [Emoji](notifications.md#emoji) for pictographs. Sizes, limits and all
fields are in the [payload reference](notifications-implementation.md#message-payload).

??? example "Mirror Home Assistant's persistent notifications"

    ```yaml
    alias: Forward persistent notifications to BETTA panel
    mode: queued
    max: 10
    triggers:
      - trigger: persistent_notification
        update_type: [added, updated]
    actions:
      - action: rest_command.betta_panel_notification
        data:
          title: "{{ trigger.notification.title | default('') }}"
          message: "{{ trigger.notification.message | default('') }}"
          message_id: "{{ trigger.notification.notification_id | default('') }}"
          timeout_sec: 30
    ```

    This uses the HTTP command above. If your Home Assistant version names the trigger variables differently,
    adjust the template.

## MQTT message options

The MQTT Notify entity sends plain text. For a title, timeout, sound, camera or action button,
publish a JSON message to the command topic shown in the panel's MQTT settings. For example:

```yaml
action: mqtt.publish
data:
  topic: "betta_ha_panel/panel-1A2B/notify/set"   # use your panel's command topic
  retain: false
  payload: >-
    {"message": "The washing machine is done.", "title": "Laundry", "timeout_sec": 30}
```

Keep **Retain** off, otherwise an old message returns on reconnect. Payload fields are the same as HTTP.
For the complete contract and limits, see [Notification implementation](notifications-implementation.md#message-payload).

## HTTP troubleshooting

| Response | Check |
| --- | --- |
| `401` | Add user `admin` and the panel's admin password. |
| `415` | Set the content type to `application/json`. |
| `503` | The panel is busy; send again. |
| `400` | Check the JSON and the required non-empty `message`. |
