<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Security

The panel is made for a trusted home network. It talks to Home Assistant directly, with no cloud service, and sets up no
port forwarding.

## Running a panel safely

1. **Set the admin password** (Settings > Backup & security). Without it, anyone on your network can open the editor,
   change settings, see the screen and install firmware.
2. **Keep the panel on your home network**, ideally on an IoT VLAN or SSID. Never forward port 80 from your router.
3. **Give the panel its own Home Assistant user** and a long-lived token you can revoke. The user has to be an
   administrator: the editor's entity search and room import need it.
4. **Do the first setup at home.** The setup access point `BETTA-Setup-XXXX` is protected by a short passphrase shown on
   the display; set the admin password as soon as the panel is online.
5. **MQTT:** use a broker login and, on a shared network, TLS.
6. **Forgot the password?** Erase the settings over USB (`esptool erase-flash`); the setup starts again.

## What to know

- The editor uses plain HTTP: someone who can listen on your network sees the screen and the password. Use a network you
  trust.
- Firmware updates are not signed. With the password set only you can install one; updates from the release are checked
  against GitHub's checksum, and an image that does not start is rolled back.
- The Home Assistant token and the Wi-Fi and MQTT passwords are stored unencrypted on the panel. If a panel is lost, revoke
  its token.
- Without a password anyone on your network can show a fake notification card, with a button if it carries an action.
- Crash dumps (Settings > Device) can contain secrets from memory: do not post them publicly.

More: [Security implementation](security-implementation.md) (endpoints, protections, known limits) and
[Security fixes](security-fixes.md) since v0.8.2.
