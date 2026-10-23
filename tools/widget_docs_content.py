# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- added in the BETTA-HA-PANEL fork, see docs/release-notes.md
"""The text of each widget guide, in the fields every guide shares (tools/generate_widget_docs.py).

summary       one or two sentences under the title; names every primary domain as `domain.*`
entities      (role, domains, required) rows; the first row is the main entity
requirements  integrations or Home Assistant features beyond the entities, or None
options       (inspector label, effect) rows, labels as the English inspector shows them
panel         what the tile shows and how it reacts to touch, as list items
"""

WIDGETS = {
    "alarm_tile": {
        "summary": "Shows the state of an `alarm_control_panel.*` entity and arms or disarms it, asking for a code when the entity needs one.",
        "entities": [("Alarm panel", "`alarm_control_panel.*`", True)],
        "requirements": None,
        "options": [("Actions offered on tap", "the arm and disarm actions the tile offers; by default all the entity supports"),
                    ("History on long press", "a long press opens the Recorder history")],
        "panel": ["States: disarmed, armed home, away, night, vacation and custom bypass, arming, pending; triggered in the error colour.",
                  "A tap opens the action list; a PIN pad follows when the entity needs a code."],
    },
    "bambu_printer": {
        "summary": "Shows a Bambu Lab printer's status, temperatures, progress and remaining time from its `sensor.*` entities, with Pause/Resume and Stop buttons.",
        "entities": [("Print status", "`sensor.*`", True), ("Nozzle and bed temperature", "`sensor.*`", True),
                     ("Progress, remaining time", "`sensor.*`", True), ("Current step, job name", "`sensor.*`", False),
                     ("Pause, Resume and Stop buttons", "`button.*`", False), ("Live camera", "`camera.*` (MJPEG)", False)],
        "requirements": "The **[Bambu Lab integration (`ha-bambulab`)](https://github.com/greghesp/ha-bambulab)**. The panel only talks to "
                        "Home Assistant; it has no direct connection to the printer. Setting up the integration is described by its own project.",
        "options": [("Printer", "the printer whose entities **Find printers** fills in; with a single printer it is chosen at once"),
                    ("Entities (adjust single ones)", "change or clear a single binding; without the buttons the card stays read-only")],
        "panel": ["Status in the header, with the current step below it on a tall card.",
                  "A drawing of the printer with nozzle and bed temperature; both take a warm colour while hot.",
                  "Progress bar and remaining time of a running or paused job; a finished or failed job is marked **Last job**.",
                  "**Pause** or **Resume** (one button), **Status** and **Stop**."],
    },
    "binary_tile": {
        "summary": "Shows the state of a `binary_sensor.*` or `event.*` entity, with optional history on tap.",
        "entities": [("Binary sensor or event", "`binary_sensor.*`, `event.*`", True)],
        "requirements": None,
        "options": [("Display", "text with the time since the change, an icon, or the time since only"),
                    ("History on tap", "a tap opens the Recorder history")],
        "panel": ["State words follow the device class (Open, Motion, Power on).",
                  "An `event.*` entity shows **Triggered** and **Last 5 min**.",
                  "Read-only."],
    },
    "button": {
        "summary": "Switches a `switch.*` entity or sends a `media_player.*` action from a compact touch button.",
        "entities": [("Switch or media player", "`switch.*`, `media_player.*`", True)],
        "requirements": None,
        "options": [("Button mode", "auto, play/pause, stop, next or previous"),
                    ("Button accent color", "the colour of the active button")],
        "panel": ["A tap runs the action directly, without a popup."],
    },
    "camera_tile": {
        "summary": "Shows snapshots of a `camera.*` or `image.*` entity, or an optional MJPEG live picture.",
        "entities": [("Camera or image", "`camera.*`, `image.*`", True)],
        "requirements": None,
        "options": [("Live picture (MJPEG)", "streams while visible; off by default. Only cameras with MJPEG; RTSP, H.264/H.265 and audio are not supported")],
        "panel": ["The snapshot refreshes every 10 seconds while visible; a tap loads a new one.",
                  "With Live picture a tap opens a full-screen view. Streams pause on hidden pages, under popups and during display protection; at most two run at once.",
                  "The source camera decides how often a new image is available. The onboard ambient-light camera is separate."],
    },
    "car_widget": {
        "summary": "Shows a car's battery level from a `sensor.*` entity as rings, with optional range, charging power and vehicle controls.",
        "entities": [("Battery level", "`sensor.*`", True), ("Charging power", "`sensor.*`", False), ("Range", "`sensor.*`", False),
                     ("Preconditioning", "`switch.*`, `climate.*`, `button.*`, `select.*`", False),
                     ("Door lock", "`lock.*`, `binary_sensor.*`", False), ("Interior · °C", "`sensor.*`", False)],
        "requirements": None,
        "options": [],
        "panel": ["Rings for the battery level and, when set, the range and charging power.",
                  "From 200 × 330 px the lock state and interior temperature share a line under the rings, with the preconditioning button across the card below it; the Inspector says when the tile is too small for them.",
                  "Preconditioning is a button; everything else is read-only."],
    },
    "climate_tile": {
        "summary": "Controls a `climate.*` entity such as an air conditioner through its supported HVAC modes.",
        "entities": [("Climate", "`climate.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["Current and target temperature in 0.5° steps.",
                  "Mode, fan and swing chips when the entity supports them; a tap on a chip opens its picker."],
    },
    "contact_tile": {
        "summary": "Shows whether a door or window `binary_sensor.*` is open, with optional history on tap.",
        "entities": [("Door or window sensor", "`binary_sensor.*`", True)],
        "requirements": None,
        "options": [("History on tap", "a tap opens the Recorder history")],
        "panel": ["Door or window follows the device class, otherwise the name.",
                  "On a narrow tile the state takes a smaller size before it is shortened.", "Read-only."],
    },
    "cover_tile": {
        "summary": "Shows the position of a `cover.*` entity such as a blind and operates it with up, stop and down.",
        "entities": [("Cover", "`cover.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["Position bar and state words.",
                  "Up, stop and down act directly. The bar is an indicator; to drag a position use a [Slider](slider.md) on the same cover."],
    },
    "datetime_tile": {
        "summary": "Shows and edits an `input_datetime.*`, `time.*`, `date.*` or `datetime.*` entity with a roller popup.",
        "entities": [("Date or time", "`input_datetime.*`, `time.*`, `date.*`, `datetime.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["Date and/or time in the panel language; seconds only when not zero.",
                  "A tap opens date and time rollers; **Save** sends one service call."],
    },
    "empty_tile": {
        "summary": "Reserves dashboard space. It has no entity and subscribes to nothing.",
        "entities": [],
        "requirements": None,
        "options": [],
        "panel": ["Empty space, no touch action. In the editor it is added as **Empty**."],
    },
    "energy_timeline": {
        "summary": "Compares cumulative energy counters (`sensor.*`, kWh) over 24 hours, with rings and a detailed timeline.",
        "entities": [("Grid import · kWh", "`sensor.*`", True), ("Grid export · kWh", "`sensor.*`", False),
                     ("Solar production · kWh", "`sensor.*`", False), ("Battery charge and discharge · kWh", "`sensor.*`", False),
                     ("State of charge · %", "`sensor.*`", False), ("Solcast sensor", "`sensor.*`", False)],
        "requirements": "Cumulative counters in kWh (state class `total` or `total_increasing`) from your meter, inverter or battery "
                        "integrations, and Home Assistant's **[Recorder](https://www.home-assistant.io/integrations/recorder/)** with their "
                        "long-term statistics (enabled by default).",
        "options": [("Look", "nested rings with values, or rings in a row with the timeline below"),
                    ("Colors (Appearance)", "the six flow colours, as swatches or HEX; **Reset to defaults** restores them")],
        "panel": ["A wider card shows more details.",
                  "A tap opens a popup with the measured counters and the estimated mix."],
    },
    "fan_tile": {
        "summary": "Controls a `fan.*` entity with the speed, preset and oscillation controls it supports.",
        "entities": [("Fan", "`fan.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["Below 220 px height minus, power and plus; above it a turning rotor.",
                  "One chip per speed step, oscillation and up to three presets, as the entity supports them; every control acts directly."],
    },
    "gauge": {
        "summary": "Shows a numeric `sensor.*` as a vertical, horizontal or radial gauge.",
        "entities": [("Sensor", "`sensor.*`", True), ("Extra readout 1 and 2", "`sensor.*`", False)],
        "requirements": None,
        "options": [("Gauge style", "linear (vertical bar), horizontal (bar, title and value in one row above it) or radial (arc)"),
                    ("Bar look", "linear and horizontal: smooth fill or segmented LED blocks"),
                    ("LED blocks (4-30, empty = 10)", "the number of segments; fewer on a short column"),
                    ("Range minimum, Range maximum", "the values the bar or arc fills between; the number shown is always the real state")],
        "panel": ["Radial and horizontal show the extra readouts as small text, inside the arc or in a row under the bar when there is room.",
                  "A distance in metres is shown in km.", "Read-only."],
    },
    "graph": {
        "summary": "Follows a numeric value over time as a line, smoothed line or bar chart; usually a `sensor.*`, any entity with a numeric state works.",
        "entities": [("Numeric entity", "a sensor or another entity with a numeric state", True)],
        "requirements": "For **History from Home Assistant**: the [Recorder](https://www.home-assistant.io/integrations/recorder/) must retain the entity.",
        "options": [("Display mode", "line, smoothed line, smoothed line with points, or bars"),
                    ("Time window (minutes)", "the period shown"),
                    ("Bar interval (min)", "bars only: the period one bar covers"),
                    ("History from Home Assistant", "the last 24 hours from the Recorder instead of samples collected on the panel; see below"),
                    ("Render points (empty = auto)", "the number of points drawn"),
                    ("Graph line color", "the line colour")],
        "panel": ["Read-only."],
    },
    "heating_tile": {
        "summary": "Shows a heating `climate.*` entity's measured and target temperatures and adjusts the target.",
        "entities": [("Thermostat", "`climate.*`", True), ("Actual temperature", "`sensor.*`", True)],
        "requirements": None,
        "options": [("Heating style", "full arc, or a semi-arc with mode buttons"),
                    ("Arc opening", "semi-arc only: left, right, top or bottom")],
        "panel": ["A tap on the card switches the thermostat on or off.", "**+** and **−** step the target temperature."],
    },
    "light_tile": {
        "summary": "Switches a `light.*` entity and adjusts brightness, colour temperature, colour and effects as far as the light supports them.",
        "entities": [("Light", "`light.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["The brightness bar is a touch slider; a tap on the lamp icon switches the light.",
                  "The colour button opens colour, colour temperature and Effect; a light with effects only opens the effect picker.",
                  "Controls appear only for what Home Assistant reports the light supports."],
    },
    "lock_tile": {
        "summary": "Shows the state of a `lock.*` entity and locks or unlocks it; optional history opens with a long press.",
        "entities": [("Lock", "`lock.*`", True)],
        "requirements": None,
        "options": [("Ask before unlocking or opening", "a confirmation before unlocking or opening; off by default"),
                    ("History on long press", "a long press opens the Recorder history")],
        "panel": ["States: locked, unlocked, locking, unlocking, open, opening; jammed in the error colour.",
                  "A tap locks or unlocks; a jammed lock is locked again.",
                  "From 260 px width an **Open** button when the lock supports it."],
    },
    "media_player": {
        "summary": "Controls playback and volume of a `media_player.*` entity, with the current track and cover art.",
        "entities": [("Media player", "`media_player.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["The volume bar is a drag slider; minus, plus and play/pause act directly."],
    },
    "parqet_portfolio": {
        "summary": "Shows a Parqet portfolio from its `sensor.*` entities: total value, gain, XIRR, dividends and the largest holdings.",
        "entities": [("Total value entity", "`sensor.*`", True), ("Unrealised gain sensor", "`sensor.*`", False),
                     ("Unrealised return sensor (%)", "`sensor.*`", False), ("XIRR sensor", "`sensor.*`", False),
                     ("Dividends sensor", "`sensor.*`", False)],
        "requirements": "The **[Parqet Companion integration](https://github.com/cubinet-code/ha-parqet-companion)** (HACS) for a portfolio "
                        "you track on [Parqet](https://www.parqet.com). The panel only talks to Home Assistant; setting up the integration is "
                        "described by its own project.\n\n"
                        "The sensors report one **interval**, set in the integration's options (default Max). The largest holdings come from "
                        "the attribute `top_holdings` of the total value sensor (five entries). The integration disables its detailed sensors "
                        "by default; only the unrealised return in percent is useful here: enable *Unrealised return (gross)* in its entity settings.",
        "options": [("Hide amounts (percentages only)", "percentages and weights only; the value is replaced by the return (or the XIRR), dividends are not shown"),
                    ("Show tabs and interval buttons on the tile", "**Performance** and **Holdings** tabs and the intervals **1D, 1W, 1M, YTD, MAX**"),
                    ("Start view, Start interval", "what the tile starts with; without the buttons they choose what it shows")],
        "panel": ["The value, the gain with the return in percent, XIRR and dividends, and on a taller tile the largest holdings as weight bars.",
                  "The tile names the interval in its corner.",
                  "The widget does not compute the return: the total value includes cash, and the result would differ from Parqet's figure."],
    },
    "stock_quotes": {
        "summary": "Shows up to eight quotes of Yahoo Finance or Easy Stock `sensor.*` entities as a list or as charts, with the last session's change and a detail view over five periods.",
        "entities": [("Quote 1", "`sensor.*` of Yahoo Finance or Easy Stock", True), ("Quote 2 to 8", "`sensor.*` of Yahoo Finance or Easy Stock", False)],
        "requirements": "One of two integrations from HACS, or both mixed: **[Yahoo Finance](https://github.com/iprak/yahoofinance)** "
                        "(`iprak/yahoofinance`) or **[Easy Stock](https://github.com/derspe/ha-easy-stock)**. The panel reads from Home "
                        "Assistant's entity registry which integration a sensor belongs to; a sensor of another integration shows "
                        "**Not a quote**. Easy Stock's daily closes come from the integration's own history endpoint; for Yahoo Finance, "
                        "longer periods use Home Assistant's statistics, so they exist only since Home Assistant records the sensor.",
        "options": [("Quote 1 to 8", "the quotes in their order; ↑ moves a quote up, an emptied row closes the gap"),
                    ("View", "**List**: name, price and the last session's change; **Charts**: a small chart per quote"),
                    ("Period", "1 day, 1 week, 30 days, year to date or 1 year: the charts' first period and the one the details open with"),
                    ("Change", "percent, amount, or both"),
                    ("Show the symbol", "the ticker under the name (list) or after it (charts)")],
        "panel": ["The price is the sensor's state with its currency; pence stay pence (GBp). A change that does not belong to the shown price "
                  "(a converted or after-hours price) is left out, a missing change shows `--`.",
                  "Rows scroll when they do not fit; a tap (not a drag) opens the details: price, the last session's change, market "
                  "state, the time of the quote and of the last update in Home Assistant, and a chart over **1D, 1W, 30D, YTD, 1Y**.",
                  "Every chart names its source: **Closes** (Easy Stock, against the close before the period), **Recorded** (a day of "
                  "Home Assistant's records, against the previous close) or Home Assistant's daily, weekly or hourly means, whose change "
                  "is a **trend**, not a market return. A shorter history says from when it starts.",
                  "Charts are fetched only for a page on screen, one at a time, and refreshed every 15 minutes."],
    },
    "price_forecast": {
        "summary": "Shows a day of electricity prices from a price `sensor.*` as bars, with the price now, today and tomorrow, and the cheap hours.",
        "entities": [("Price sensor", "`sensor.*` with a price list", True), ("Average sensor (EUR/kWh)", "`sensor.*`", False)],
        "requirements": None,
        "options": [("Average (fixed, EUR/kWh; empty = calculated)", "a fixed average instead of the day's own; an average sensor replaces it too"),
                    ("Day switch (Today / Tomorrow)", "Today/Tomorrow buttons, one toggle on a narrow tile; off shows the 24 hours around now"),
                    ("Cheap times (shade and name the cheapest hours)", "shades the cheap windows and names the next one"),
                    ("Line instead of filled bars", "a stepped line")],
        "panel": ["One bar per interval in cent per kWh: green below the average, orange above it; past intervals dimmed, the running one outlined.",
                  "Price now, interval, minimum and maximum.",
                  "A cheap window is the lowest quarter of the day's prices (rounded to 0.001 EUR/kWh), at least one hour long and not over yet.",
                  "Design inspired by the community's price timeline cards, for example [Neisi/ha-price-timeline-card](https://github.com/Neisi/ha-price-timeline-card); the code is the panel's own."],
    },
    "room_tile": {
        "summary": "Groups a room's values and controls in one card, with a popup for its entities; any domain, filled from a Home Assistant area or by hand.",
        "entities": [("Main entity", "any", True), ("Room entities (up to 7)", "any", False),
                     ("Extra field", "one of the room entities", False)],
        "requirements": None,
        "options": [("Room entities", "one entity per line, up to seven; importing an area in the editor fills them"),
                    ("Extra field", "humidity, cover (status and open/close) or media player (status and play/pause)"),
                    ("Controls on the card", "lights, switches and covers as buttons, on a card of about 400 × 240 px")],
        "panel": ["A tap opens a popup with every entity and toggles for lights, switches, covers, fans and media players.",
                  "With **Controls on the card** lights and switches toggle together and covers get up and down."],
    },
    "scene_tile": {
        "summary": "Runs a `scene.*`, `script.*`, `button.*`, `input_button.*` or `lock.*` action from a compact tile.",
        "entities": [("Action", "`scene.*`, `script.*`, `button.*`, `input_button.*`, `lock.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["A tap runs the action directly, without a popup."],
    },
    "select_tile": {
        "summary": "Chooses an option of a `select.*` or `input_select.*` entity.",
        "entities": [("Select", "`select.*`, `input_select.*`", True)],
        "requirements": None,
        "options": [],
        "panel": ["A tap opens the option list; picking one requests it.",
                  "Leave enough width for the title and the longest option; the example is 360 × 100 px."],
    },
    "sensor": {
        "summary": "Shows the value of a `sensor.*` or `binary_sensor.*` entity, formatted by its device class, with optional name, age and Recorder history.",
        "entities": [("Sensor", "`sensor.*`, `binary_sensor.*`", True)],
        "requirements": None,
        "options": [("Show name", "the widget name; on by default"),
                    ("Show last-updated time", "the age of the value; on by default"),
                    ("Unit, Format, Time format, Currency", "by device class, see below. The inspector header shows the class and unit as chips (\"No device class\" when Home Assistant gives none) and offers only that class's choices"),
                    ("History on tap", "a tap opens the Recorder history; only when the Recorder retains the entity")],
        "panel": ["The value as formatted for its device class (see Options).", "Read-only."],
    },
    "slider": {
        "summary": "Adjusts a writable number such as light brightness, cover position or an input number.",
        "entities": [("Writable number", "light brightness, cover position, an input number and the like", True)],
        "requirements": None,
        "options": [("Slider entity type", "what the slider sets"),
                    ("Slider direction", "auto, left to right, right to left, bottom to top or top to bottom"),
                    ("Slider accent color", "the colour of the bar")],
        "panel": ["Drag or tap the bar to set the value directly."],
    },
    "solar_forecast": {
        "summary": "Compares the predicted solar production from `sensor.*` forecasts for today and the next four days.",
        "entities": [("Remaining today (sensor)", "`sensor.*`", True), ("Today forecast", "`sensor.*`", False),
                     ("Tomorrow, Day 3–5 forecast", "`sensor.*`", False)],
        "requirements": "Sensors that forecast the solar production per day, for example from "
                        "**[Solcast PV Forecast](https://github.com/BJReplay/ha-solcast-solar)**; any integration that provides them works. "
                        "The editor suggests Solcast entity ids as examples.",
        "options": [("Bar orientation", "horizontal or vertical"),
                    ("Bar max (kWh)", "the value of a full bar"),
                    ("Line up days with the weather tile", "vertical bars only: the columns use the weather forecast's day grid, so a solar tile with the same left edge, width and day count puts each day under its weather day")],
        "panel": ["Today's row carries two readings: its bar and value are the day's total (**Today forecast**); the part still "
                  "to come (**Remaining today**) is the bright end of the bar over the dimmed total, so the dim start reads as "
                  "already produced. This needs both sensors; in the example 25 of 55 kWh are still to come.",
                  "With **Line up days with the weather tile** the header shows **Rest** and the remaining kWh.",
                  "A tap opens a popup with Rest and every day's value; it closes after a minute."],
    },
    "sonos_favorites": {
        "summary": "Plays a Sonos favourite on a `media_player.*` from a list on the dashboard.",
        "entities": [("Sonos player", "`media_player.*`", True), ("Favorites sensor", "`sensor.sonos_favorites`", True)],
        "requirements": "The **[Sonos integration](https://www.home-assistant.io/integrations/sonos/)** with its favorites sensor.",
        "options": [],
        "panel": ["The favourites as rows, in one or two columns by name length.", "A tap on a favourite plays it directly."],
    },
    "todo_list": {
        "summary": "Shows a `todo.*` list and marks single items done.",
        "entities": [("To-do list", "`todo.*`", True)],
        "requirements": "A list of Home Assistant's **[To-do](https://www.home-assistant.io/integrations/todo/)** integration from any "
                        "provider, for example Local To-do, Shopping List or Todoist.",
        "options": [],
        "panel": ["A tap on a row marks that item done."],
    },
    "vacuum_tile": {
        "summary": "Shows a `vacuum.*` entity's status, its cleaning actions and an optional map in the tile.",
        "entities": [("Vacuum", "`vacuum.*`", True), ("Map image", "`image.*`", False), ("Cleaning mode", "`select.*`", False),
                     ("Battery sensor", "`sensor.*`", False)],
        "requirements": None,
        "options": [("Default cleaning program", "set on the mode entity before Start (not when resuming); empty keeps the last program"),
                    ("History on long press", "a long press opens the last 24 hours of activity")],
        "panel": ["Start/pause, stop and dock follow the vacuum's capabilities; a suction picker appears when it reports fan speeds.",
                  "The battery sensor is the only source of the charge since Home Assistant 2026.9; the chip is hidden while no value is known.",
                  "The map is read-only. The legacy layout name `roborock_tile` uses this widget."],
    },
    "wallbox_evcc": {
        "summary": "Shows and controls an evcc loadpoint through its entities (the mode `select.*` and the related sensors, numbers and selects): charging mode, vehicle, target SOC, power, energy and remaining time.",
        "entities": [("Loadpoint mode", "`select.*`", True), ("Vehicle, target SOC", "`select.*`, `number.*`", False),
                     ("Status, power, energy, remaining time, SOC", "`sensor.*`, `binary_sensor.*`", False),
                     ("PV, battery, grid and home power (energy-flow header)", "`sensor.*`", False)],
        "requirements": "The **[evcc Home Assistant integration](https://github.com/marq24/ha-evcc)** (HACS), connected to an evcc server "
                        "(the add-on or a standalone server). Enable the loadpoint mode, vehicle, charge limit, power, charged energy, remaining "
                        "duration, SOC and connection entities. Smart mode needs integration 2026.8.3 or later; checked with 2026.10.2. All "
                        "actions pass through Home Assistant.",
        "options": [("Loadpoint", "**Find loadpoints** reads the integration; existing bindings stay until you accept a replacement"),
                    ("Advanced entities", "every binding and its target domain"),
                    ("Header", "off, the charging power as a bar up to **Maximum charging power (kW)** (empty = 11), or the energy flow: "
                               "sources above two bars on one scale, consumers below; **Find loadpoints** also fills the site sensors")],
        "panel": ["Status, power, charged energy, remaining time and the current SOC on a green bar.",
                  "The optional header sits above the modes: the charging power against its maximum, or the site's flow in kW (PV, battery, grid in; home, car, battery, export out). It needs about 400 px (power) or 440 px (flow) of height and stays off on a shorter card."],
    },
    "weather_3day": {
        "summary": "Compares forecast days of a `weather.*` entity: temperatures and precipitation in configurable columns.",
        "entities": [("Weather", "`weather.*`", True), ("DWD weather report", "`sensor.*`", False)],
        "requirements": "A **[weather](https://www.home-assistant.io/integrations/weather/)** entity with daily forecasts. The report text "
                        "needs the **[DWD Weather integration](https://github.com/FL550/dwd_weather)** and its separate *Weather report* sensor "
                        "(see [DWD report setup](../home-assistant.md#what-a-feature-needs)); the report is German.",
        "options": [("Visible days", "fewer days give each column more room; Auto shows as many as are readable"),
                    ("Show rain chance and amount", "on by default"),
                    ("Show name", "off by default")],
        "panel": ["A tap on a day opens a popup with pressure, wind, UV and that day's report.",
                  "With the report, a tap on the temperature shows the full text; a wide header shows a summary (about 150 × 40 px free)."],
    },
    "weather_tile": {
        "summary": "Shows the current conditions of a `weather.*` entity, its temperature and the extra readings the provider supplies.",
        "entities": [("Weather", "`weather.*`", True)],
        "requirements": None,
        "options": [("Show name", "off by default, to leave room for the conditions")],
        "panel": ["Today's low and precipitation when there is room; the same Meteocons as the forecast.",
                  "UV, pressure and other extras appear only when the provider supplies them.", "Read-only."],
    },
}
