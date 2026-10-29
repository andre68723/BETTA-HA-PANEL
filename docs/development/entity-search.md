<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -->
# Entity search rules

The editor uses `entity-search-rules.js` for search eligibility and the translated explanation in each popup.
`layout-model.js` supplies the allowed domains for the current field, slider domain and button action.
Creation and Inspector searches share these domains; the Script and Event shortcuts deliberately remain narrower.
Room creation uses area discovery instead of selecting one anchor.
The entity-first search under **+ Widget** also filters its proposed widget types through these contracts;
text sensors do not offer Graph and on/off-only lights do not offer Slider.

The following covers every current widget and its entity fields. A domain-only rule is deliberate: for example,
any cover class is valid for the Cover widget. Its supported features determine the available controls.

| Widget | Field | Domains | Additional search rule |
| --- | --- | --- | --- |
| Sensor | Main | `sensor`, `binary_sensor` | None; text, enums, dates and numbers are all displayable. |
| Button | Main | `switch`, `media_player`; explicit media actions: `media_player` | None; the selected action determines the domain. |
| Slider | Main | `light`, `media_player`, `cover`, narrowed by its domain setting | Dimming, volume-set or set-position capability respectively. Missing capability metadata is excluded. |
| Gauge | Main | `sensor` | Numeric state, unit or numeric state class; excludes text/enum/date classes. |
| Gauge | Extra information 1/2 | `sensor` | None; these fields display text as well as numbers. |
| Graph | Main | `sensor` | Numeric, as for Gauge. History availability is not established by the entity search. |
| Empty | — | — | No entity search. |
| Light | Main | `light` | None; on/off-only lights are valid. |
| Heating | Main / Actual temperature | `climate` / `sensor` | Main: none. Actual: temperature class or temperature unit. |
| Climate | Main | `climate` | None; HVAC modes and features determine its controls. |
| Weather | Main | `weather` | None. |
| Weather forecast | Main / Report | `weather` / `sensor` | Report: the native DWD parser's contract (`data` text longer than 400 characters containing `Wetter`). |
| Solar forecast | Remaining, today, tomorrow, days 3–5 | `sensor` | Numeric energy, Wh/kWh/MWh, energy class or no class. The user must choose the correct forecast/day. |
| Energy price forecast | Main / Average reference | `sensor` | Supported price-series structure / scalar EUR/kWh or €/kWh. |
| Energy timeline | Import, export, solar, battery charge/discharge | `sensor` | Wh/kWh/MWh, `total` or `total_increasing`, energy class or no class; excludes Solcast forecasts. Recorder sum history is additionally required. |
| Energy timeline | Battery SOC / Forecast today | `sensor` | Battery percentage / Solcast detailed forecast structure. |
| Parqet portfolio | Value, unrealised gain, dividends | `sensor` | `monetary` device class. The class does not distinguish fees from value or dividends. |
| Parqet portfolio | XIRR, unrealised return | `sensor` | Numeric with unit `%` and no physical device class. The user chooses the corresponding financial metric. |
| Stocks & ETFs | Quotes 1–8 | `sensor` | Registered with Easy Stock; an arbitrary `symbol` attribute is insufficient. |
| Car | SOC / Charging power / Range | `sensor` | Battery `%` / power W or kW / distance m, km or mi; matching class or no class. |
| Car | Interior temperature | `sensor` | Temperature class or unit. |
| Car | Lock | `lock`, `binary_sensor` | Binary sensors require lock, door or opening class. |
| Car | Preconditioning | `switch`, `input_boolean`, `climate`, `button`, `input_button`, `select` | None; these are the service targets supported by the native widget. |
| Wallbox | Loadpoint and all bindings | Detected evcc entities | Dedicated evcc discovery; bindings remain read-only and come from the selected loadpoint/instance. |
| Bambu printer | Status and every extra field | `sensor`, `button`, `camera` by role | Inspector searches use the exact role IDs from printer discovery. Extra fields stay with the selected printer. Creation requires a Bambu print-status sensor. |
| To-do list | Main | `todo` | None. |
| Media player | Main | `media_player` | None; capabilities determine the controls. |
| Sonos favorites | Target / Favorites source | `media_player` / `sensor` | Sonos integration / `items` dictionary. |
| Cover | Main | `cover` | None; all device classes are valid. |
| Fan | Main | `fan` | None; speed and preset features are optional. |
| Contact | Main | `binary_sensor` | Door, window, opening, garage-door or lock class; unknown classes are excluded. |
| Camera | Main | `camera`, `image` | None; both are accepted by the native widget. |
| Scene / Script | Main | `scene`, `script`, `button`, `input_button`, `lock` | None; these are the native action targets. The Script creation shortcut uses only `script`. |
| Select | Main | `select`, `input_select` | None. |
| Date/time | Main | `input_datetime`, `time`, `date`, `datetime` | None; the entity determines whether date, time or both can be edited. |
| Lock | Main | `lock` | None. Locks have no HA device class. |
| Alarm | Main | `alarm_control_panel` | None; supported arm modes determine controls. |
| Binary / Event | Main | `binary_sensor`, `event` | None; the door look uses the Contact rule. The Event creation shortcut uses only `event`. |
| Room | Anchor | `sensor`, `climate` | Temperature sensor / climate with `current_temperature`. |
| Room | Room entities | Supported room domains | Sensors: temperature/humidity/battery. Binary sensors: contacts/presence/low battery. Other supported actor domains remain open. Assigned IDs are excluded. |
| Room | Extra value | `sensor`, `cover`, `media_player` according to mode | Only assigned room IDs; humidity requires a humidity percentage. |
| Vacuum / legacy Roborock | Main / Map / Cleaning mode / Battery | `vacuum` / `image` / `select` / `sensor` | Battery percentage; other fields use their domains only. |

Settings weather uses `weather`; settings stock tickers use the same quote policy as Stocks & ETFs.
Energy-page fields have separate rules: power W/kW, battery SOC, or cumulative gas/water with the relevant
class, unit and state class. Their rules never inherit the last selected widget.

## Metadata and limits

`ha_entity_source.c` owns bounded discovery queries. The buffer uses PSRAM in firmware. Queries return unit,
class, state class, source-format flags, integration membership and capabilities without copying complete
forecast lists. Integration membership comes from HA's registry helpers, never from names or ID prefixes.
The popup retains the unfiltered response cache so another field can apply its own policy.

Discovery names are stored in the HA model, shared by the cached entity API, Room details
and stock rows. The template keeps HA 2026.10's `friendly_name` when present; without it,
`entity_name()` and `device_name()` supply a name without a repeated device prefix.
Registry-only discovery retains the model's existing name when available. A state with
only an ID cannot erase a discovered name, while an updated legacy friendly name still
applies. This does not subscribe to registry renames: names without the legacy attribute
refresh on discovery. Saved widget titles are independent of discovery names.

Numeric metadata keeps temporarily unavailable sensors selectable. A matching unit does not establish that a
sensor belongs to the intended car, room or portfolio. Solar forecasts and monetary metrics retain an explicit
role-selection note. Structural series eligibility is not full validation of every timestamp or sample; the
native parser validates selected data. Energy statistics/history cannot be proved from state metadata alone.

The active domains and rules stay visible even for an empty or pending result. Assigned/excluded ID restrictions
are explained separately. Domain-only fields explicitly say that no additional device-class filter applies.
A filtered-out entity is not restored by an empty-results fallback. Saved bindings are retained; changing
metadata never silently rewrites a layout. Confirmed source mismatches for energy, price and portfolio retain a short visible count. Detailed affected fields,
requirements and incomplete cached-metadata explanations belong to the Entities info popup. Missing required
metadata does not establish an invalid saved binding; the strict search filter still requires eligible results.

Node tests exercise each measurement/source/capability contract, all widget/field hints in both languages,
creation domains and event → model → saved JSON → reload. Host tests check production query bounds and metadata
serialization; running the exact generated queries against HA checks its Jinja functions. Browser checks are
still needed for presentation; neither those nor host tests establish physical panel input behavior.
