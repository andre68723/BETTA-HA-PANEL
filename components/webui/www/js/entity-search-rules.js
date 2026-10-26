/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
 * Search eligibility and its visible explanation share this catalog. Domain validation
 * on saved layouts remains separate: discovering metadata never deletes a binding.
 */
const ENTITY_SEARCH_PRIMARY = {
  sensor: "domain", button: "domain", slider: "slider", gauge: "numeric", graph: "numeric",
  empty_tile: "none", light_tile: "domain", heating_tile: "domain", weather_tile: "domain",
  weather_3day: "domain", solar_forecast: "solar", energy_price_forecast: "price",
  parqet_portfolio: "monetary", stock_quotes: "quote", car_widget: "battery",
  wallbox_evcc: "evcc", bambu_printer: "printer", todo_list: "domain", media_player: "domain",
  sonos_favorites: "sonos", cover_tile: "domain", fan_tile: "domain", contact_tile: "contact",
  camera_tile: "domain", climate_tile: "domain", scene_tile: "domain", select_tile: "domain",
  datetime_tile: "domain", lock_tile: "domain", alarm_tile: "domain", energy_timeline: "counter",
  binary_tile: "domain", room_tile: "temperature", vacuum_tile: "domain", roborock_tile: "domain",
};
const ENTITY_SEARCH_SLOTS = {
  heating_tile: { secondary: "temperature" },
  weather_tile: { secondary: "report" },
  weather_3day: { secondary: "report" },
  sonos_favorites: { secondary: "favorites" },
  vacuum_tile: { secondary: "domain", tertiary: "domain", vacuum_battery: "battery" },
  gauge: { secondary: "domain", tertiary: "domain" },
  car_widget: { secondary: "power", tertiary: "distance", car_precond: "domain", car_lock: "car_lock", car_temp: "temperature" },
  parqet_portfolio: { secondary: "monetary", tertiary: "return_percent", parqet_dividends: "monetary", parqet_return: "return_percent" },
  energy_price_forecast: { secondary: "average" },
  energy_timeline: { secondary: "counter", tertiary: "counter", etl_charge: "counter", etl_discharge: "counter", etl_soc: "battery", etl_forecast: "solcast" },
  room_tile: { room_entities: "room", room_extra: "room_extra" },
  bambu_printer: { bambu_camera: "domain", bambu_nozzle: "temperature", bambu_bed: "temperature", bambu_progress: "percent",
    bambu_remaining: "duration", bambu_stage: "domain", bambu_job: "domain", bambu_pause: "domain", bambu_resume: "domain", bambu_stop: "domain" },
};

function widgetEntitySearchRole(type, slot = "primary") {
  if (type === "energy_page") return slot;
  if (type === "roborock_tile") type = "vacuum_tile";
  if (type === "solar_forecast") return "solar";
  if (type === "stock_quotes") return "quote";
  if (type === "wallbox_evcc") return "evcc";
  return slot === "primary" ? ENTITY_SEARCH_PRIMARY[type] || "domain" : ENTITY_SEARCH_SLOTS[type]?.[slot] || "domain";
}

// Bits are emitted by the HA discovery template. Keep in step with ha_entity_source.h.
const ENTITY_SEARCH_FLAGS = { numeric: 1, report: 2, favorites: 4, quote: 8, sonos: 16, printer: 32, climate_temperature: 64, printer_status: 128 };

function entitySearchAttributeMetadata(state, attrs = {}) {
  const number = String(state ?? "").trim();
  let flags = number && Number.isFinite(Number(number)) ? ENTITY_SEARCH_FLAGS.numeric : 0;
  if ((typeof attrs.data === "string" && attrs.data.length > 400 && attrs.data.includes("Wetter")) || (typeof attrs.s === "string" && attrs.d && typeof attrs.d === "object")) flags |= ENTITY_SEARCH_FLAGS.report;
  if (attrs.items && !Array.isArray(attrs.items) && typeof attrs.items === "object") flags |= ENTITY_SEARCH_FLAGS.favorites;
  if (typeof attrs.current_temperature === "number") flags |= ENTITY_SEARCH_FLAGS.climate_temperature;
  const modes = attrs.supported_color_modes;
  const dimming = Array.isArray(modes) ? modes.some(mode => typeof mode === "string" && mode && !["onoff", "unknown"].includes(mode)) :
    (!!attrs.color_mode && !["onoff", "unknown"].includes(attrs.color_mode)) || !!((attrs.supported_features || 0) & 209) ||
    typeof attrs.brightness === "number" || typeof attrs.brightness_pct === "number";
  return { search_flags: flags, supported_features: typeof attrs.supported_features === "number" ? attrs.supported_features : null,
    capabilities: { dimming } };
}

function entitySearchRoleMatches(entity, role) {
  const meta = entitySourceMetadata(entity), domain = String(entity.id || "").split(".")[0];
  const flag = key => !!((meta.search_flags || 0) & ENTITY_SEARCH_FLAGS[key]);
  const unit = meta.unit, dc = meta.device_class;
  const numeric = flag("numeric") || (!!unit && !["enum", "timestamp", "date"].includes(dc)) || ["measurement", "total", "total_increasing"].includes(meta.state_class);
  const measurement = (classes, units) => numeric && (!dc || classes.includes(dc)) && (units.includes(unit) || (!unit && classes.includes(dc)));
  switch (role) {
    case "none": return false;
    case "numeric": return numeric;
    case "temperature": return domain === "climate" ? flag("climate_temperature") || typeof meta.current_temperature === "number" : measurement(["temperature"], ["°C", "°F", "C", "F", "K"]);
    case "battery": return measurement(["battery"], ["%"]);
    case "return_percent": return numeric && unit === "%" && !dc;
    case "percent": return numeric && unit === "%";
    case "power": return measurement(["power"], ["W", "kW", "w", "kw"]);
    case "distance": return measurement(["distance"], ["m", "km", "mi"]);
    case "duration": return measurement(["duration"], ["s", "min", "h"]);
    case "monetary": return dc === "monetary";
    case "average": return ["EUR/kWh", "€/kWh"].includes(unit);
    case "price": return meta.has_price_series === true;
    case "solcast": return meta.has_solar_forecast === true;
    case "solar": return measurement(["energy"], ["Wh", "kWh", "MWh"]);
    case "gas": case "water": return ["total", "total_increasing"].includes(meta.state_class) &&
      (role === "water" ? dc === "water" && ["m³", "L", "gal"].includes(unit) : ["gas", "energy"].includes(dc) && ["m³", "ft³", "CCF", "Wh", "kWh", "MWh"].includes(unit));
    case "counter": return ["Wh", "kWh", "MWh"].includes(unit) && ["total", "total_increasing"].includes(meta.state_class)
      && (!dc || dc === "energy") && meta.has_solar_forecast !== true;
    case "contact": return ["door", "window", "opening", "garage_door", "lock"].includes(dc);
    case "car_lock": return domain === "lock" || ["lock", "door", "opening"].includes(dc);
    case "room":
      if (domain === "sensor") return ["temperature", "humidity", "battery"].includes(dc);
      if (domain === "binary_sensor") return ["door", "window", "opening", "garage_door", "motion", "occupancy", "presence", "moving", "battery"].includes(dc);
      return ["light", "cover", "fan", "climate", "media_player", "lock", "switch", "vacuum"].includes(domain);
    case "room_extra": return domain !== "sensor" || measurement(["humidity"], ["%"]);
    case "slider":
      if (domain === "light") return meta.capabilities?.dimming === true;
      return typeof meta.supported_features === "number" && !!(meta.supported_features & (domain === "media_player" ? 4 : domain === "cover" ? 4 : 0));
    case "printer": return flag("printer_status");
    case "quote": case "report": case "favorites": case "sonos": return flag(role);
    // evcc fields are filled by the loadpoint discovery, not by freely binding arbitrary selects.
    default: return true;
  }
}

// Creation aliases intentionally keep their narrower purpose (script / doorbell / room import).
function entityPickerCreationDomains(type) {
  if (["room_tile", "script_tile", "doorbell_tile", "contact_tile", "image"].includes(type)) return {};
  const domains = allowedEntityDomainsForWidgetType(type);
  return domains.length ? { domain: domains.length > 1 ? "any" : domains[0], allowedDomains: domains } : {};
}
