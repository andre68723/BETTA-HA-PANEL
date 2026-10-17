<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->

# Home Assistant setup

For the first connection, follow [Install and set up](getting-started.md#4-connect-home-assistant):
it covers the token, required administrator account and complete WebSocket address.
To change them later, open **Settings → Home Assistant & MQTT**, update the address/token and save;
the panel restarts.

Most widgets use entities you already have. Choose them by name in the editor; the
[widget library](widgets/README.md) lists compatible entity types and any extra integrations.
The panel reads states and sends control actions to Home Assistant. It has its own dashboard layout;
HA's Lovelace dashboards and custom cards are not imported. MQTT is only needed if you choose it for messages.

??? info "If the panel stops connecting"

    Home Assistant bans an address after a few failed logins (`http.ban`), and a
    wrong, expired or revoked token fails every login. The panel therefore stops
    all requests with the token at the first rejection (HTTP 401, WebSocket
    `auth_invalid`, or HTTP 403, which usually means the address is already
    banned) and does not retry until it restarts; saving a new token in Settings
    restarts it. The editor shows a banner with the reason, and `/api/version`
    reports it as `ha_auth`. A token whose user is no administrator only turns
    off the entity search. To lift a ban, remove the panel's address from
    `/config/ip_bans.yaml` and restart Home Assistant.

## What a feature needs

Each widget guide lists its own requirements at the top; this is the overview.

| Feature | Needs in Home Assistant | Guide |
| --- | --- | --- |
| Weather tile, top-bar weather | a `weather.*` entity from any provider with daily forecasts | [Weather](widgets/weather_tile.md) |
| German report text on the forecast | **DWD Weather** and its *Weather report* sensor, disabled by default: show disabled entities, enable it | [Weather forecast](widgets/weather_3day.md) |
| Solar forecast | one `sensor.*` per day to show; Solcast PV Forecast is one source, replace the suggested IDs | [Solar forecast](widgets/solar_forecast.md) |
| Price forecast | one `sensor.*` with a `data` list of prices: EPEX Spot (Germany and many markets); Nord Pool and Tibber through a helper that stores their prices as `data` | [Price forecast](widgets/price_forecast.md) |
| Energy page, mode Home Assistant Energy | sources set up in HA's Energy dashboard | [below](#energy-and-rooms) |
| Energy timeline | cumulative kWh counters with `state_class` `total` or `total_increasing` | [Energy timeline](widgets/energy_timeline.md) |
| Energy page: seven-day details | Gas, Water or Solar as cumulative counters with long-term statistics (volume or energy unit) | [Seven-day details](pages.md#seven-day-details) |
| Room cards, house page | areas (and optionally floors) with devices or entities assigned | [below](#energy-and-rooms) |
| History popups, graph from Home Assistant | Recorder history of the entity; for the 7-day view a `measurement` sensor with long-term statistics | [History](history.md#choose-the-history-view) |
| Wallbox | evcc server (add-on or standalone) and the HACS ha-evcc integration; enable loadpoint controls, energy, remaining-duration and status entities; Sessions uses the integration’s WebSocket interface | [Wallbox](widgets/wallbox.md) |
| Bambu printer | the Bambu Lab integration (`ha-bambulab`); the panel never talks to the printer | [Bambu printer](widgets/bambu_printer.md) |
| Stock quote in the top bar | one numeric sensor, for example Yahoo Finance | [Top bar](top-bar.md) |
| Sonos favorites | the Sonos integration: a `media_player.*` and `sensor.sonos_favorites` | [Sonos favorites](widgets/sonos_favorites.md) |
| Messages from HA | MQTT broker and HA MQTT integration for graphical actions; optional HTTP route needs YAML | [Notifications](notifications.md) |
| Doorbell alerts | an `event.*` entity (or one that turns on); optional `button.*`/`lock.*` for opening and `camera.*` for the snapshot | [Doorbell alerts](notifications.md#doorbell-and-event-alerts) |

The panel targets Home Assistant 2026.10 or newer and ignores attributes removed before it: a light's mireds and a vacuum's own battery value. A vacuum's charge comes from its battery sensor (set it in the card's inspector; the editor suggests one for a new card).

Other details come from the entity's own integration: a vacuum's map image, a car's charging and climate sensors or a
media player's cover art only appear when those attributes exist. The built-in cameras of the JC and Tab5 panels sense
light and movement and need no integration.

## Energy and rooms

### Energy dashboard page

Add a page through **Pages → + Page → Energy dashboard**, then choose its source in the page inspector:

| Mode | Required data |
| --- | --- |
| Home Assistant Energy | Sources configured in Home Assistant's Energy dashboard |
| Manual live sensors | Your live power sensors for grid, solar, home and optional battery; gas/water are optional |

This is a whole-page dashboard. The independent Energy timeline widget is a
resizable widget and needs no Energy page.

??? info "Statistics, gaps and estimated source shares"

    The panel reads Home Assistant long-term statistics
    (`recorder/statistics_during_period`, five-minute `change`), one counter at a
    time, and refreshes every 15 minutes. Missing records stay gaps; **Outdated**
    appears after 40 minutes without fresh data. The forecast line covers only
    intervals supplied by its sensor.

    Source shares are estimates: aggregate meters cannot identify whether grid
    energy charged a battery or directly served the home. The popup explains
    the allocation rules and shows the underlying measured counters and
    statistic IDs separately.

### Room import and house page

Assign devices or entities to **areas** in Home Assistant before importing a
room or creating a house page. **Floors** optionally group rooms on the house
page. Child devices inherit their parent device's area unless the entity or child
device has its own assignment. Entity names follow Home Assistant's display names.
A [Room card](widgets/room_tile.md) can also be configured manually.

For the extra room field, choose Humidity, Cover or Media player and an entity
already included in the room. Cover and media controls appear only when the
entity supports them; unavailable entities show disabled controls.
