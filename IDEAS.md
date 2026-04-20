# Ideas

## Dedicated energy dashboard page

Status: first electricity MVP implemented in firmware/editor, including a
default `ha_energy` source for HA Energy statistics and a `manual_live`
fallback for explicit W/kW sensors. The web editor preview now uses the HA
Energy snapshot to hide unconfigured nodes and can reflect gas/water sources.
LVGL gas/water rendering, EV, and richer compare periods remain follow-up
work.

Create a Home Assistant style energy distribution dashboard for the panel as
its own full page, not as a normal draggable widget on an existing page.

Reference sources checked:
- Home Assistant frontend repository:
  `src/panels/lovelace/cards/energy/hui-energy-distribution-card.ts`
- Related Home Assistant energy data logic:
  `src/data/energy.ts`

Useful Home Assistant behavior to translate:
- The Lovelace card uses the configured HA energy sources and groups them by
  type: grid, solar, battery, gas, water, and optional low-carbon data.
- It calculates derived electricity paths instead of only rendering raw sensor
  values: grid to home, solar to home, solar to grid, solar to battery,
  battery to home, grid to battery, and battery to grid.
- The home node ring is split by contribution source, using solar, battery,
  low-carbon grid, and high-carbon grid segments where data is available.
  On the panel, the ring should always be a closed 100% arc made only from the
  sources that contributed to today's home consumption. If all consumption came
  from the grid, the complete ring is grid-colored.
- Moving flow dots are only shown for active non-zero flows. Dot duration is
  based on the flow's share of total active line flow.
- HA uses a compact dark card, colored circular nodes, short labels outside
  the circles, small animated flow dots, and energy-specific theme colors.

Page-level direction:
- Introduce a dedicated page type, for example:
  `"type": "energy_dashboard"`.
- The page still lives inside the existing layout `pages[]` array so the
  bottom navigation can show it like any other page.
- An energy page should ignore normal widget placement and render a complete
  LVGL scene into the page container.
- The editor should create this through an "Energy Page" action, not through
  the widget palette.
- Page config should live at page level, for example:
  `"energy": { ... }`, rather than inside `widgets[]`.

Core behavior:
- Show energy sources, storage, consumers, and grid as circular nodes connected by colored flow lines.
- Animate small dots on each line in the direction the energy is currently flowing.
- Use the line color for the moving dots.
- Increase dot speed as the current power flow increases.
- Hide or slow the animation when a flow is zero or unavailable.
- Keep the visual language close to Home Assistant energy cards: dark background, colored rings, compact labels, icons, and live values.

Likely nodes:
- Home / current load
- Grid import and export
- Solar production
- Battery charge / discharge and battery percentage
- Optional low-carbon / gas / EV / water heater nodes if configured

Possible data model:
- Page-level config object, not a widget:
  - `source`: default `ha_energy` for automatic Home Assistant Energy
    dashboard statistics; optional `manual_live` for explicit live W/kW sensor
    fields.
  - `home_power_entity_id`
  - `solar_power_entity_id`
  - `grid_power_entity_id` or split `grid_import_power_entity_id` / `grid_export_power_entity_id`
  - `battery_power_entity_id` or split `battery_charge_power_entity_id` / `battery_discharge_power_entity_id`
  - `battery_soc_entity_id`
  - optional `gas_power_entity_id`, `water_flow_entity_id`, `ev_power_entity_id`, `heat_pump_power_entity_id`
  - optional daily total entities for labels where live power alone is not enough
- Flow direction should be derived from signed power values where possible.
- Split import/export entities should be supported because many HA setups
  expose positive-only sensors.
- Use W/kW for live flow and Wh/kWh for daily energy totals where available.

Implementation notes:
- Add a page-level branch in `ui_runtime_load_layout()`:
  - normal pages keep rendering `widgets[]`
  - `energy_dashboard` pages call a dedicated energy page renderer
- Keep energy page instances in a separate runtime list from normal widget
  instances, because they subscribe to multiple entities and are not widgets.
- In `manual_live` mode, energy page entity IDs must be collected by the HA
  client just like widget entity IDs so the configured Home Assistant sensors
  are fetched, subscribed, and pushed into the display runtime.
- In default `ha_energy` mode, the HA client should use Home Assistant's
  energy preferences and recorder statistics APIs instead of manual sensor
  subscriptions.
- Draw connection lines on an LVGL canvas or lightweight custom draw layer.
- Use an LVGL timer for animation ticks.
- Compute dot offsets from elapsed time so animation stays smooth without storing many objects.
- Scale speed non-linearly so small flows remain visible and large flows feel
  faster without becoming noisy. HA's simple baseline is roughly:
  `duration = max - (flow / total_active_flow) * (max - min)`.
- Keep CPU and memory use low enough for the panel by limiting dot count and redraw area.
- Add validation support for optional configured entity IDs before exposing it
  in the web layout editor.
- The first implementation can support electricity only: home, grid, solar,
  battery, and battery SOC. Gas, water, EV, and heat pump can follow once the
  page renderer is stable.
- The current MDI font is intentionally small. For the energy page we likely
  need a larger generated icon font covering home, transmission tower, solar,
  battery, gas, water, and optional EV/heat-pump symbols.

LVGL layout sketch for 720x600 content area:
- Top row: optional solar node centered, optional low-carbon/gas/water side nodes.
- Middle row: grid left, home center, optional EV/heat pump right.
- Bottom row: battery centered below home.
- Lines:
  - grid <-> home horizontal
  - solar -> home curved down
  - solar -> grid curved left for export
  - solar -> battery vertical or soft curve
  - battery <-> home curved up
  - battery <-> grid curved left
- Use 80-96 px nodes, 2 px connector lines, and 3-4 px flow dots so the page
  remains readable from wall-panel distance.

Reference style:
- Home Assistant energy distribution card with animated flow dots.
