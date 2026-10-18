/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Layout model: pages, entity domains and suggestions, loading layout and entities
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
function defaultLayout() {
  return {
    version: 1,
    pages: [
      {
        id: "living",
        title: t("layout.default_page.title"),
        widgets: [],
      },
    ],
  };
}

function defaultEnergyConfig() {
  return ENERGY_ENTITY_KEYS.reduce((config, key) => {
    config[key] = "";
    return config;
  }, { source: ENERGY_SOURCE_HA });
}

function isEnergyPage(page) {
  return page?.type === ENERGY_PAGE_TYPE;
}

const HOUSE_PAGE_TYPE = "house_dashboard";
function isHousePage(page) {
  return page?.type === HOUSE_PAGE_TYPE;
}
// Energy and house pages hold no tiles of their own.
function isFixedPage(page) {
  return isEnergyPage(page) || isHousePage(page);
}

function energyPageUsesHaSource(page) {
  if (!isEnergyPage(page)) return false;
  const source = typeof page.energy?.source === "string" ? page.energy.source.trim() : "";
  return source !== ENERGY_SOURCE_MANUAL;
}

function layoutHasHaEnergyPage() {
  return (editor.layout?.pages || []).some((page) => energyPageUsesHaSource(page));
}

function normalizeEnergyConfig(page) {
  if (!page || !isEnergyPage(page)) return;
  if (!page.energy || typeof page.energy !== "object" || Array.isArray(page.energy)) {
    page.energy = defaultEnergyConfig();
  }
  const source = typeof page.energy.source === "string" ? page.energy.source.trim() : "";
  const hasManualSensors = ENERGY_ENTITY_KEYS.some((key) => typeof page.energy[key] === "string" && page.energy[key].trim());
  page.energy.source = ENERGY_SOURCES.has(source) ? source : (hasManualSensors ? ENERGY_SOURCE_MANUAL : ENERGY_SOURCE_HA);
  for (const key of ENERGY_ENTITY_KEYS) {
    page.energy[key] = typeof page.energy[key] === "string" ? page.energy[key].trim() : "";
  }
  if (page.energy.details !== ENERGY_DETAILS_SIDE) delete page.energy.details; // the popup is the default and stays unsaved
  page.widgets = [];
}

function getEnergyInputs() {
  return {
    home_power_entity_id: el.energyHomePower,
    solar_power_entity_id: el.energySolarPower,
    grid_power_entity_id: el.energyGridPower,
    grid_import_power_entity_id: el.energyGridImport,
    grid_export_power_entity_id: el.energyGridExport,
    battery_power_entity_id: el.energyBatteryPower,
    battery_charge_power_entity_id: el.energyBatteryCharge,
    battery_discharge_power_entity_id: el.energyBatteryDischarge,
    battery_soc_entity_id: el.energyBatterySoc,
    gas_entity_id: el.energyGas,
    water_entity_id: el.energyWater,
  };
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function snap(value) {
  return Math.round(value / GRID) * GRID;
}

function selectedPage() {
  if (!editor.layout) return null;
  return editor.layout.pages.find((p) => p.id === editor.selectedPageId) || null;
}

function selectedWidget() {
  const page = selectedPage();
  if (!page) return null;
  if (!Array.isArray(page.widgets)) return null;
  return page.widgets.find((w) => w.id === editor.selectedWidgetId) || null;
}

function inspectorWidgetType() {
  return selectedWidget()?.type || el.fType?.value || "sensor";
}

// The device-class rule set: a binary tile in the door look uses the door / window contact rules.
function inspectorRuleType() {
  const widget = selectedWidget();
  if (widget && widget.type === "binary_tile" && widget.display_mode === "door") return "contact_tile";
  return inspectorWidgetType();
}

function inspectorSliderEntityDomain() {
  if (el.fSliderEntityDomain) {
    return normalizeSliderEntityDomain(el.fSliderEntityDomain.value);
  }
  return normalizeSliderEntityDomain(selectedWidget()?.slider_entity_domain);
}

function inspectorButtonMode() {
  if (el.fButtonMode) {
    return normalizeButtonMode(el.fButtonMode.value);
  }
  return normalizeButtonMode(selectedWidget()?.button_mode);
}

function allowedEntityDomainsForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "wallbox_evcc") return ["select"];
  if (type === "empty_tile") return [];
  if (type === "sensor" || type === "graph" || type === "gauge" || type === "solar_forecast" || type === "price_forecast" || type === "parqet_portfolio" || type === "car_widget" || type === "bambu_printer" || type === "energy_timeline") return ["sensor"];
  if (type === "button") {
    const normalizedMode = normalizeButtonMode(buttonMode);
    return buttonModeRequiresMediaPlayer(normalizedMode) ? ["media_player"] : ["switch", "media_player"];
  }
  if (type === "light_tile") return ["light"];
  if (type === "heating_tile") return ["climate"];
  if (type === "weather_tile" || type === "weather_3day") return ["weather"];
  if (type === "todo_list") return ["todo"];
  if (type === "media_player" || type === "sonos_favorites") return ["media_player"];
  if (type === "vacuum_tile") return ["vacuum"];
  if (type === "cover_tile") return ["cover"];
  if (type === "climate_tile") return ["climate"];
  if (type === "room_tile") return ["sensor", "climate"]; /* the anchor: a temperature sensor or climate entity */
  if (type === "scene_tile") return ["scene", "script", "button", "input_button", "lock"];
  if (type === "select_tile") return ["select", "input_select"];
  if (type === "datetime_tile") return ["input_datetime", "time", "date", "datetime"];
  if (type === "lock_tile") return ["lock"];
  if (type === "alarm_tile") return ["alarm_control_panel"];
  if (type === "binary_tile") return ["binary_sensor", "event"];
  if (type === "contact_tile") return ["binary_sensor"];
  if (type === "camera_tile") return ["camera"];
  if (type === "fan_tile") return ["fan"];
  if (type === "slider") {
    const normalized = normalizeSliderEntityDomain(sliderDomain);
    if (normalized === "auto") {
      return ["light", "media_player", "cover"];
    }
    return [normalized];
  }
  return [];
}

function expectedDomainForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  const domains = allowedEntityDomainsForWidgetType(type, sliderDomain, buttonMode);
  return domains.length === 1 ? domains[0] : "";
}

function secondaryEntityConfigForWidgetType(type) {
  if (type === "weather_3day") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_weather_report",
      labelFallback: "Weather report sensor (optional)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "heating_tile") {
    return {
      enabled: true,
      domain: "sensor",
      optional: false,
      labelKey: "layout.inspector.secondary_entity",
      labelFallback: "Actual entity (sensor)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "vacuum_tile") {
    return {
      enabled: true,
      domain: "image",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_vacuum",
      labelFallback: "Map entity (image, optional)",
      invalidStatusKey: "layout.status.secondary_image_required",
    };
  }
  if (type === "sonos_favorites") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_sonos_favorites",
      labelFallback: "Favorites sensor (sensor)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "car_widget") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_car",
      labelFallback: "Charging power entity (sensor, optional)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "energy_timeline") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_energy_timeline",
      labelFallback: "Grid export energy counter (kWh, optional)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "parqet_portfolio") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_parqet_portfolio",
      labelFallback: "Unrealised gain sensor (optional)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "price_forecast") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_price_forecast",
      labelFallback: "Average sensor (EUR/kWh, optional)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  if (type === "gauge") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.secondary_entity_gauge",
      labelFallback: "Extra info 1 (sensor, optional, radial style only)",
      invalidStatusKey: "layout.status.secondary_sensor_required",
    };
  }
  return {
    enabled: false,
    domain: "",
    optional: true,
    labelKey: "layout.inspector.secondary_entity",
    labelFallback: "Actual entity (sensor)",
    invalidStatusKey: "layout.status.secondary_sensor_required",
  };
}

function tertiaryEntityConfigForWidgetType(type) {
  if (type === "vacuum_tile") {
    // Optional select entity holding the cleaning mode of a vacuum without
    // Roborock services (for example a Matter vacuum).
    return {
      enabled: true,
      domain: "select",
      optional: true,
      labelKey: "layout.inspector.tertiary_entity_vacuum",
      labelFallback: "Cleaning mode entity (select, optional)",
      invalidStatusKey: "layout.status.tertiary_select_required",
    };
  }
  if (type === "car_widget") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.tertiary_entity_car",
      labelFallback: "Range entity (sensor, optional)",
      invalidStatusKey: "layout.status.tertiary_sensor_required",
    };
  }
  if (type === "parqet_portfolio") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.tertiary_entity_parqet_portfolio",
      labelFallback: "XIRR sensor (optional)",
      invalidStatusKey: "layout.status.tertiary_sensor_required",
    };
  }
  if (type === "energy_timeline") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.tertiary_entity_energy_timeline",
      labelFallback: "Solar production energy counter (kWh, optional)",
      invalidStatusKey: "layout.status.tertiary_sensor_required",
    };
  }
  if (type === "gauge") {
    return {
      enabled: true,
      domain: "sensor",
      optional: true,
      labelKey: "layout.inspector.tertiary_entity_gauge",
      labelFallback: "Extra info 2 (sensor, optional, radial style only)",
      invalidStatusKey: "layout.status.tertiary_sensor_required",
    };
  }
  return {
    enabled: false,
    domain: "",
    optional: true,
    labelKey: "layout.inspector.tertiary_entity_car",
    labelFallback: "Range entity (sensor, optional)",
    invalidStatusKey: "layout.status.tertiary_sensor_required",
  };
}

function listEntitiesByDomain(domain) {
  if (!domain) return editor.entities;
  return editor.entities.filter((entity) => typeof entity.id === "string" && entity.id.startsWith(`${domain}.`));
}

// Home Assistant's friendly name for an entity, if the editor has already seen it (from the
// initial bulk entity fetch, or a picker result merged in since -- mergeEntityPickerItemsIntoEntities()).
// Used so a freshly added widget's title defaults to something a person recognizes ("Kueche") instead
// of falling all the way through to the raw entity_id ("light.kuche") or, worse, the widget's own
// internal auto-generated id ("page_1_light_tile_1") -- found while auditing which widget types show
// a name at all: none of them actually default to the HA name, only to whatever the editor put in
// `title` when the widget was created, which used to just be the id.
function entityFriendlyName(entityId) {
  if (!entityId) return "";
  const entity = editor.entities.find((candidate) => candidate?.id === entityId);
  return entity && typeof entity.name === "string" ? entity.name : "";
}

// A vacuum no longer reports its charge as an attribute (Home Assistant 2026.9): the integration adds a
// battery sensor next to it. The bulk entity list has no device or area, so the match goes by name: a
// battery-class sensor whose id or friendly name starts with the vacuum's. Returns "" when none fits.
function suggestVacuumBatterySensor(vacuumId) {
  if (typeof vacuumId !== "string" || !vacuumId.startsWith("vacuum.")) return "";
  const stem = vacuumId.slice("vacuum.".length).toLowerCase();
  const name = entityFriendlyName(vacuumId).trim().toLowerCase();
  let best = "";
  let bestScore = 0;
  for (const entity of listEntitiesByDomain("sensor")) {
    if (entity.device_class !== "battery") continue;
    const object = entity.id.slice("sensor.".length).toLowerCase();
    const label = String(entity.name || "").trim().toLowerCase();
    let score = 0;
    if (stem && object.startsWith(stem)) score = 3;
    else if (name && label.startsWith(name)) score = 2;
    else if (stem && object.includes(stem)) score = 1;
    if (score > bestScore || (score === bestScore && score > 0 && entity.id.length < best.length)) {
      best = entity.id;
      bestScore = score;
    }
  }
  return best;
}

function entityMatchesWidgetType(
  entity,
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "empty_tile") return true;

  const id = typeof entity?.id === "string" ? entity.id : "";
  if (!id) return false;

  const allowedDomains = allowedEntityDomainsForWidgetType(type, sliderDomain, buttonMode);
  if (!allowedDomains.length) return true;
  const matchesDomain = allowedDomains.some((domain) => id.startsWith(`${domain}.`));
  if (!matchesDomain) return false;
  const modelEntity = entity?.capabilities
    ? entity
    : editor.entities.find((candidate) => candidate?.id === id);
  if (type === "slider" && id.startsWith("light.") && modelEntity?.capabilities?.dimming === false) {
    return false;
  }
  return true;
}

// Which device classes fit a field: sensors and binary sensors are ranked (or, for room and contact tiles,
// restricted) by what the widget shows, so a car tile offers battery sensors first and a contact tile only
// doors and windows. Entities of other domains are never dropped, and the soft rule never drops anything.
const ENTITY_CLASS_RULES = {
  room_tile: { primary: { classes: ["temperature"], hard: true } },
  contact_tile: { primary: { classes: ["door", "window", "opening", "garage_door", "lock"], hard: true } },
  car_widget: {
    primary: { classes: ["battery"] },
    secondary: { classes: ["power"] },
    tertiary: { classes: ["distance"] },
  },
  heating_tile: { secondary: { classes: ["temperature"] } },
  solar_forecast: {
    primary: { classes: ["energy"] },
    solar_today: { classes: ["energy"] },
    solar_tomorrow: { classes: ["energy"] },
    solar_day_3: { classes: ["energy"] },
    solar_day_4: { classes: ["energy"] },
    solar_day_5: { classes: ["energy"] },
  },
};

function applyEntityClassRule(list, type, slot) {
  const rule = ENTITY_CLASS_RULES[type] && ENTITY_CLASS_RULES[type][slot];
  if (!rule || !Array.isArray(list)) return list;
  const constrained = (entity) => /^(sensor|binary_sensor)\./.test(entity.id || "");
  const matches = (entity) => !constrained(entity) || rule.classes.includes(entity.device_class || "");
  // The panel's search results carry no device class: an unknown class must not hide an entity, only a known other one does.
  const known = (entity) => !constrained(entity) || !entity.device_class || rule.classes.includes(entity.device_class);
  if (rule.hard) return list.filter(known);
  return [...list.filter(matches), ...list.filter((entity) => !matches(entity))];
}

function listEntitiesForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "empty_tile") return [];
  return applyEntityClassRule(
    editor.entities.filter((entity) => entityMatchesWidgetType(entity, type, sliderDomain, buttonMode)),
    type,
    "primary",
  );
}

// Default for a second field (for example the actual temperature of a heating tile): the first entity of the
// domain that fits the widget's device class rule.
function pickDefaultSecondaryEntity(type, domain) {
  const list = applyEntityClassRule(listEntitiesByDomain(domain), type, "secondary");
  return list.length > 0 ? list[0].id : "";
}

function pickDefaultEntityForWidgetType(
  type,
  sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN,
  buttonMode = DEFAULT_BUTTON_MODE,
) {
  if (type === "empty_tile") return "";
  const matching = listEntitiesForWidgetType(type, sliderDomain, buttonMode);
  if (matching.length > 0) return matching[0].id;
  return "";
}

function uniqueId(prefix, list, accessor = (x) => x.id) {
  let i = 1;
  while (true) {
    const candidate = `${prefix}_${i}`;
    if (!list.some((entry) => accessor(entry) === candidate)) return candidate;
    i += 1;
  }
}

function sanitizeIdPart(value) {
  const normalized = String(value || "")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return normalized || "item";
}

function createWidgetIdForPage(page, type) {
  const pagePart = sanitizeIdPart(page?.id || page?.title || "page");
  const typePart = sanitizeIdPart(type || "widget");
  const allWidgets = editor.layout?.pages?.flatMap((p) => (Array.isArray(p.widgets) ? p.widgets : [])) || [];

  let i = 1;
  while (true) {
    const suffix = `_${i}`;
    const candidate = `${`${pagePart}_${typePart}`.slice(0, 31 - suffix.length)}${suffix}`;
    if (!allWidgets.some((widget) => widget?.id === candidate)) {
      return candidate;
    }
    i += 1;
  }
}

async function apiGet(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  return response.json();
}

function setEntityOptionsList(target, entities) {
  target.innerHTML = "";
  for (const entity of entities) {
    if (!entity || typeof entity.id !== "string" || !entity.id.length) continue;
    const option = document.createElement("option");
    option.value = entity.id;
    option.label = `${entity.id} (${entity.name || entity.id})`;
    target.appendChild(option);
  }
}

function normalizeEntitySearchTerm(value) {
  if (typeof value !== "string") return "";
  const trimmed = value.trim();
  if (!trimmed) return "";
  const wildcardIndex = trimmed.indexOf("*");
  if (wildcardIndex < 0) return trimmed;
  return trimmed.slice(wildcardIndex + 1).trim();
}

function parseEntitySearchInput(rawValue, fallbackDomain = "") {
  const value = typeof rawValue === "string" ? rawValue.trim() : "";
  let domain = typeof fallbackDomain === "string" ? fallbackDomain.trim().toLowerCase() : "";
  let search = value;

  const dotIndex = value.indexOf(".");
  if (dotIndex > 0) {
    const candidateDomain = value.slice(0, dotIndex).trim().toLowerCase();
    if (/^[a-z0-9_]+$/.test(candidateDomain)) {
      domain = candidateDomain;
      search = value.slice(dotIndex + 1);
    }
  }

  search = normalizeEntitySearchTerm(search);
  return { domain, search };
}

function entityContainsSearch(entity, search) {
  if (!search) return true;
  const needle = search.toLowerCase();
  const id = String(entity?.id || "").toLowerCase();
  const name = String(entity?.name || "").toLowerCase();
  return id.includes(needle) || name.includes(needle);
}

function filterLocalEntitySuggestions(source, domain, search, maxItems) {
  const results = [];
  for (const entity of source) {
    if (!entity || typeof entity.id !== "string" || entity.id.length === 0) continue;
    if (domain && !entity.id.startsWith(`${domain}.`)) continue;
    if (!entityContainsSearch(entity, search)) continue;
    results.push(entity);
    if (results.length >= maxItems) break;
  }
  return results;
}

// Cleaning programs of the vacuum's mode select, read from the panel's Home Assistant state: a real drop-down list
// with "keep the last used program" first. The saved value stays selectable even when the list cannot be read yet.
function fillVacuumModeSelect(options, current) {
  const select = el.fVacuumDefaultMode;
  if (!select) return;
  const wanted = current !== undefined ? current : select.value;
  select.replaceChildren();
  const add = (value, text) => {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = text;
    select.appendChild(option);
  };
  add("", t("layout.inspector.vacuum_default_mode_none"));
  const seen = new Set([""]);
  for (const option of options) {
    if (!seen.has(option)) {
      seen.add(option);
      add(option, option);
    }
  }
  if (wanted && !seen.has(wanted)) add(wanted, wanted);
  select.value = wanted || "";
}

async function loadVacuumModeOptions(entityId) {
  const select = el.fVacuumDefaultMode;
  if (!select) return;
  const current = selectedWidget()?.default_mode || select.value || "";
  const id = String(entityId || "").trim();
  let options = [];
  if (id.startsWith("select.")) {
    try {
      const data = await apiGet(`/api/state?entity_id=${encodeURIComponent(id)}`);
      const attrs = JSON.parse(data.items?.[0]?.attributes_json || "{}");
      options = Array.isArray(attrs.options) ? attrs.options.map(String) : [];
    } catch (_) { /* keep the empty list */ }
  }
  fillVacuumModeSelect(options, current);
}

async function fetchEntitySuggestions(domain, search, limit) {
  const params = new URLSearchParams();
  if (domain) params.set("domain", domain);
  if (search) params.set("search", search);
  params.set("limit", String(limit));
  const data = await apiGet(`/api/entities?${params.toString()}`);
  return Array.isArray(data.items) ? data.items : [];
}

function primaryEntitySource() {
  const inspectorType = inspectorWidgetType();
  const sliderDomain = inspectorSliderEntityDomain();
  const buttonMode = inspectorButtonMode();
  const typedOptions = listEntitiesForWidgetType(inspectorType, sliderDomain, buttonMode);
  return typedOptions.length > 0 ? typedOptions : editor.entities;
}

function defaultPrimaryEntityDomain() {
  return expectedDomainForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode());
}

function entityDomainFromValue(value) {
  const text = String(value || "").trim();
  const dotIndex = text.indexOf(".");
  if (dotIndex <= 0) return "";
  const domain = text.slice(0, dotIndex).trim().toLowerCase();
  return /^[a-z0-9_]+$/.test(domain) ? domain : "";
}

function preferredInspectorPrimaryDomain(input) {
  const allowed = allowedEntityDomainsForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode());
  if (!allowed.length) return "";
  const currentDomain = entityDomainFromValue(input?.value);
  if (currentDomain && allowed.includes(currentDomain)) {
    return currentDomain;
  }
  return expectedDomainForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode()) || allowed[0];
}

// Extra widget bindings use the same search and change event as the primary three fields.
const INSPECTOR_EXTRA_ENTITY_FIELDS = [
  ["energy_timeline", "etl_charge", "fEtlCharge", "sensor", "etl_charge"],
  ["energy_timeline", "etl_discharge", "fEtlDischarge", "sensor", "etl_discharge"],
  ["energy_timeline", "etl_soc", "fEtlSoc", "sensor", "etl_soc"],
  ["energy_timeline", "etl_forecast", "fEtlForecast", "sensor", "etl_forecast"],
  ["car_widget", "car_precond", "fCarPrecond", "switch,input_boolean,climate,button,input_button,select", "car_precond"],
  ["car_widget", "car_lock", "fCarLock", "lock,binary_sensor", "car_lock"],
  ["car_widget", "car_temp", "fCarTemp", "sensor", "car_temp"],
  ["vacuum_tile", "vacuum_battery", "fVacuumBattery", "sensor", "vacuum_battery"],
  ["parqet_portfolio", "parqet_dividends", "fParqetDividends", "sensor", "parqet_dividends"],
  ["parqet_portfolio", "parqet_return", "fParqetReturn", "sensor", "parqet_return"],
  ["bambu_printer", "bambu_camera", "fBambuCamera", "camera", "bambu_camera"],
  ...["Nozzle", "Bed", "Progress", "Remaining", "Stage", "Job", "Pause", "Resume", "Stop"].map(field =>
    ["bambu_printer", `bambu_${field.toLowerCase()}`, `fBambu${field}`,
      ["Pause", "Resume", "Stop"].includes(field) ? "button" : "sensor", `bambu_${field.toLowerCase()}`]),
];

function inspectorEntityPickerTarget(targetKey) {
  const widgetType = inspectorWidgetType();
  const primaryDomains = allowedEntityDomainsForWidgetType(widgetType, inspectorSliderEntityDomain(), inspectorButtonMode());
  const secondaryConfig = secondaryEntityConfigForWidgetType(widgetType);
  const tertiaryConfig = tertiaryEntityConfigForWidgetType(widgetType);
  const extraTargets = {};
  for (const [type, slot, field, domains, title] of INSPECTOR_EXTRA_ENTITY_FIELDS) {
    if (type !== widgetType) continue;
    extraTargets[slot] = { input: el[field], domain: domains.includes(",") ? "any" : domains,
      allowedDomains: domains.split(","), titleKey: `layout.inspector.${title}`, titleFallback: "Choose Entity" };
  }
  if (widgetType === "wallbox_evcc") {
    for (const [, field, domains] of EVCC_BINDINGS) {
      extraTargets[`evcc_${field.toLowerCase()}`] = { input: el[`fEvcc${field}`],
        domain: domains.includes(",") ? "any" : domains, allowedDomains: domains.split(","),
        titleKey: `evcc.${field.toLowerCase()}`, titleFallback: "Choose Entity" };
    }
  }
  if (widgetType === "room_tile") {
    const extras = roomEntitiesFromInspector();
    extraTargets.room_entities = { input: el.fRoomEntities, domain: "any", append: true,
      excludedEntityIds: [el.fEntity?.value, ...extras],
      titleKey: "layout.inspector.room_entities", titleFallback: "Room entities" };
    if (extras.length >= 7) delete extraTargets.room_entities;
    const mode = el.fRoomExtraMode?.value;
    extraTargets.room_extra = { input: el.fRoomExtraEntity, domain: ROOM_EXTRA_DOMAINS[mode],
      allowedEntityIds: roomExtraCandidates(mode, el.fEntity?.value || "", extras),
      titleKey: "layout.inspector.room_extra_entity", titleFallback: "Entity" };
  }
  const targets = {
    ...extraTargets,
    primary: {
      input: el.fEntity,
      domain: primaryDomains.length > 1 ? "any" : preferredInspectorPrimaryDomain(el.fEntity),
      allowedDomains: primaryDomains,
      // The title of the widget type's own picker ("Choose light") when there is one.
      titleKey: ENTITY_PICKER_CONFIGS[widgetType]?.titleKey || "layout.inspector.entity",
      titleFallback: ENTITY_PICKER_CONFIGS[widgetType]?.titleFallback || "Choose Entity",
    },
    secondary: {
      input: el.fSecondaryEntity,
      domain: secondaryConfig.domain,
      titleKey: widgetType === "sonos_favorites"
        ? "entity_picker.title_sonos_favorites_sensor"
        : secondaryConfig.labelKey,
      titleFallback: widgetType === "sonos_favorites"
        ? "Choose Sonos Favorites Sensor"
        : secondaryConfig.labelFallback,
      initialSearch: widgetType === "sonos_favorites" ? "sonos_favorites" : "",
    },
    tertiary: {
      input: el.fTertiaryEntity,
      domain: tertiaryConfig.domain,
      titleKey: tertiaryConfig.labelKey,
      titleFallback: tertiaryConfig.labelFallback,
    },
    solar_today: {
      input: el.fSolarForecastToday,
      domain: "sensor",
      titleKey: "layout.inspector.solar_today",
      titleFallback: "Today forecast",
    },
    solar_tomorrow: {
      input: el.fSolarForecastTomorrow,
      domain: "sensor",
      titleKey: "layout.inspector.solar_tomorrow",
      titleFallback: "Tomorrow forecast",
    },
    solar_day_3: {
      input: el.fSolarForecastDay3,
      domain: "sensor",
      titleKey: "layout.inspector.solar_day_3",
      titleFallback: "Day 3 forecast",
    },
    solar_day_4: {
      input: el.fSolarForecastDay4,
      domain: "sensor",
      titleKey: "layout.inspector.solar_day_4",
      titleFallback: "Day 4 forecast",
    },
    solar_day_5: {
      input: el.fSolarForecastDay5,
      domain: "sensor",
      titleKey: "layout.inspector.solar_day_5",
      titleFallback: "Day 5 forecast",
    },
  };
  const target = targets[targetKey];
  if (!target?.input || target.input.disabled || !target.domain) {
    return null;
  }
  return { ...target, slot: targetKey };
}

function openInspectorEntityPicker(targetKey) {
  const target = inspectorEntityPickerTarget(targetKey);
  if (!target) return;
  openLightEntityPicker(`inspector_${targetKey}`, {
    inspectorTarget: target,
    configOverride: { ...entityPickerConfigForDomain(target.domain, target.titleKey, target.titleFallback),
      allowedDomains: target.allowedDomains, allowedEntityIds: target.allowedEntityIds,
      excludedEntityIds: target.excludedEntityIds },
    initialSearch: target.initialSearch || "",
  });
}

function assignInspectorEntity(target, id) {
  if (!target?.input || target.input.disabled || !/^[a-z0-9_]+\.[a-zA-Z0-9_]+$/.test(id)) return false;
  const domain = id.split(".")[0];
  if (target.domain !== "any" && target.domain !== domain) return false;
  if (target.allowedDomains?.length && !target.allowedDomains.includes(domain)) return false;
  if (target.allowedEntityIds && !target.allowedEntityIds.includes(id)) return false;
  if (target.excludedEntityIds?.includes(id)) return false;
  if (target.append) {
    const ids = target.input.value.split(/[\n,;]+/).map(value => value.trim()).filter(Boolean);
    if (ids.length >= 7 || ids.includes(id)) return false;
    target.input.value = [...ids, id].join("\n");
  } else target.input.value = id;
  target.input.dispatchEvent(new Event("change", { bubbles: true }));
  target.input.focus();
  return true;
}

// Remove in the entity picker: clears an optional binding. The main entity and appended lists (room entities) have none.
function removeInspectorEntity(target) {
  if (!target?.input || target.input.disabled || !target.slot || target.slot === "primary" || target.append) return false;
  target.input.value = "";
  target.input.dispatchEvent(new Event("change", { bubbles: true }));
  return true;
}

// Names in the inspector's entity rows: Home Assistant's name without the words all named entities start with
// ("Trade Republic Gesamtwert", "Trade Republic XIRR" -> "Gesamtwert", "XIRR"); "" where the editor knows no name.
function inspectorEntityRowNames(ids) {
  const names = ids.map(id => {
    const entity = id ? editor.entities.find(item => item?.id === id) : null;
    return entity?.name && entity.name !== id ? String(entity.name) : "";
  });
  const named = names.filter(Boolean).map(name => name.split(" "));
  let prefix = named.length >= 2 ? named[0] : [];
  for (const words of named.slice(1)) {
    let i = 0;
    while (i < prefix.length && i < words.length && prefix[i] === words[i]) i++;
    prefix = prefix.slice(0, i);
  }
  return names.map(name => {
    const words = name.split(" ");
    return name && prefix.length && words.length > prefix.length ? words.slice(prefix.length).join(" ") : name;
  });
}

function openSettingsTopbarEntityPicker(kind, inputOverride = null) {
  const target = kind === "weather"
    ? {
        input: el.settingsTopbarWeatherEntity,
        domain: "weather",
        titleKey: "entity_picker.title_weather",
        titleFallback: "Choose Weather",
      }
    : {
        input: el.settingsTopbarStockEntity,
        domain: "sensor",
        titleKey: "entity_picker.title_sensor",
        titleFallback: "Choose Sensor",
      };
  if (inputOverride) target.input = inputOverride;
  if (!target.input || target.input.disabled) return;
  openLightEntityPicker(`settings_topbar_${kind}`, {
    inspectorTarget: target,
    configOverride: entityPickerConfigForDomain(target.domain, target.titleKey, target.titleFallback),
  });
}

function scheduleEntityAutocomplete(kind, immediate = false) {
  const isSecondary = kind === "secondary";
  const isTertiary = kind === "tertiary";
  const state = isTertiary ? entityAutocomplete.tertiary : (isSecondary ? entityAutocomplete.secondary : entityAutocomplete.primary);
  const input = isTertiary ? el.fTertiaryEntity : (isSecondary ? el.fSecondaryEntity : el.fEntity);
  const options = isTertiary ? document.getElementById("tertiaryEntityOptions") : (isSecondary ? el.sensorEntityOptions : el.entityOptions);
  const secondaryConfig = secondaryEntityConfigForWidgetType(inspectorWidgetType());
  const tertiaryConfig = tertiaryEntityConfigForWidgetType(inspectorWidgetType());
  if (!input || !options) return;
  if (input.disabled) return;
  if (isSecondary && !secondaryConfig.enabled) return;
  if (isTertiary && !tertiaryConfig.enabled) return;

  if (state.timerId !== null) {
    window.clearTimeout(state.timerId);
    state.timerId = null;
  }

  const run = async () => {
    const requestSeq = ++state.requestSeq;
    const raw = input.value || "";
    const fallbackDomain = isTertiary ? tertiaryConfig.domain : (isSecondary ? secondaryConfig.domain : defaultPrimaryEntityDomain());
    const { domain, search } = parseEntitySearchInput(raw, fallbackDomain);
    const source = isTertiary
      ? listEntitiesByDomain(tertiaryConfig.domain)
      : (isSecondary ? listEntitiesByDomain(secondaryConfig.domain) : primaryEntitySource());
    const allowedDomains = isTertiary
      ? [tertiaryConfig.domain]
      : (isSecondary
        ? [secondaryConfig.domain]
        : allowedEntityDomainsForWidgetType(inspectorWidgetType(), inspectorSliderEntityDomain(), inspectorButtonMode()));

    if (domain && allowedDomains.length > 0 && !allowedDomains.includes(domain)) {
      setEntityOptionsList(options, []);
      return;
    }

    const slotName = isTertiary ? "tertiary" : (isSecondary ? "secondary" : "primary");
    const widgetType = inspectorRuleType();
    const localResults = applyEntityClassRule(
      filterLocalEntitySuggestions(source, domain, search, ENTITY_AUTOCOMPLETE_MAX_ITEMS * 4),
      widgetType,
      slotName,
    ).slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
    setEntityOptionsList(options, localResults);

    const shouldQueryApi = domain.length > 0 || search.length >= 2;
    if (!shouldQueryApi) return;

    try {
      const remoteResults = await fetchEntitySuggestions(domain, search, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
      if (requestSeq !== state.requestSeq) return;
      if (remoteResults.length > 0) {
        setEntityOptionsList(options, applyEntityClassRule(remoteResults, widgetType, slotName));
      }
    } catch (_) {
      // Keep local fallback options.
    }
  };

  if (immediate) {
    void run();
    return;
  }

  state.timerId = window.setTimeout(() => {
    state.timerId = null;
    void run();
  }, ENTITY_AUTOCOMPLETE_DEBOUNCE_MS);
}

function scheduleSettingsWeatherAutocomplete(immediate = false, inputOverride = null) {
  const input = inputOverride || el.settingsTopbarWeatherEntity;
  const options = el.settingsWeatherEntityOptions;
  const state = entityAutocomplete.settingsWeather;
  if (!input || !options || !state) return;

  if (state.timerId !== null) {
    window.clearTimeout(state.timerId);
    state.timerId = null;
  }

  const run = async () => {
    const requestSeq = ++state.requestSeq;
    const { search } = parseEntitySearchInput(input.value || "", "weather");
    const localResults = filterLocalEntitySuggestions(
      listEntitiesByDomain("weather"),
      "weather",
      search,
      ENTITY_AUTOCOMPLETE_MAX_ITEMS,
    );
    setEntityOptionsList(options, localResults);

    if (search.length < 2) return;
    try {
      const remoteResults = await fetchEntitySuggestions("weather", search, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
      if (requestSeq !== state.requestSeq) return;
      if (remoteResults.length > 0) {
        setEntityOptionsList(options, remoteResults);
      }
    } catch (_) {
      // Keep local fallback options.
    }
  };

  if (immediate) {
    void run();
    return;
  }

  state.timerId = window.setTimeout(() => {
    state.timerId = null;
    void run();
  }, ENTITY_AUTOCOMPLETE_DEBOUNCE_MS);
}

function scheduleSettingsStockAutocomplete(input, immediate = false) {
  const options = el.settingsStockEntityOptions;
  const state = entityAutocomplete.settingsStocks;
  if (!input || !options || !state) return;

  if (state.timerId !== null) {
    window.clearTimeout(state.timerId);
    state.timerId = null;
  }

  const run = async () => {
    const requestSeq = ++state.requestSeq;
    const { search } = parseEntitySearchInput(input.value || "", "sensor");
    const localResults = filterLocalEntitySuggestions(
      listEntitiesByDomain("sensor"),
      "sensor",
      search,
      ENTITY_AUTOCOMPLETE_MAX_ITEMS,
    );
    setEntityOptionsList(options, localResults);

    if (search.length < 2) return;
    try {
      const remoteResults = await fetchEntitySuggestions("sensor", search, ENTITY_AUTOCOMPLETE_MAX_ITEMS);
      if (requestSeq !== state.requestSeq) return;
      if (remoteResults.length > 0) {
        setEntityOptionsList(options, remoteResults);
      }
    } catch (_) {
      // Keep local fallback options.
    }
  };

  if (immediate) {
    void run();
    return;
  }

  state.timerId = window.setTimeout(() => {
    state.timerId = null;
    void run();
  }, ENTITY_AUTOCOMPLETE_DEBOUNCE_MS);
}

let layoutLoadRequest = 0;

async function loadLayout() {
  const requestId = ++layoutLoadRequest;
  const previous = layoutSnapshot();
  setStatus(t("layout.status.loading"));
  try {
    const candidate = await apiGet("/api/layout");
    if (requestId !== layoutLoadRequest) return false;
    validateLayoutStructure(candidate);
    if (layoutSnapshot() !== previous) {
      setStatus(t("layout.status.load_changed"), true);
      return false;
    }
    const before = { layout: editor.layout, pageId: editor.selectedPageId,
      widgetId: editor.selectedWidgetId, width: CANVAS_WIDTH, height: CANVAS_HEIGHT,
      saved: savedLayoutSnapshot };
    layoutHistory.suspended = true;
    try {
      editor.layout = candidate;
      applyCanvasGeometry(candidate, true);
      normalizeLayoutWidgets(candidate);
      editor.selectedPageId = candidate.pages[0].id;
      editor.selectedWidgetId = null;
      renderAll();
      clearDirty();
      hideUndoToast();
      closeWidgetActions();
    } catch (err) {
      editor.layout = before.layout;
      editor.selectedPageId = before.pageId;
      editor.selectedWidgetId = before.widgetId;
      savedLayoutSnapshot = before.saved;
      applyCanvasGeometry({ canvas_w: before.width, canvas_h: before.height }, true);
      // Repaint the original model if a presentation/normalization failure
      // interrupted installation. Keep the original error and history intact.
      if (before.layout) { try { renderAll(); } catch (_) {} }
      setDirtyState(layoutSnapshot() !== savedLayoutSnapshot);
      throw err;
    } finally {
      layoutHistory.suspended = false;
    }
    layoutHistoryReset();
    setStatus(t("layout.status.loaded"));
    return true;
  } catch (err) {
    if (requestId !== layoutLoadRequest) return false;
    // At first startup provide an editable empty canvas, while retaining the
    // failure status. A failed reload must preserve model, selection and history.
    if (!editor.layout) {
      editor.layout = defaultLayout();
      editor.selectedPageId = editor.layout.pages[0].id;
      editor.selectedWidgetId = null;
      renderAll();
      clearDirty();
      layoutHistoryReset();
    }
    setStatus(t("layout.status.load_failed", { error: err.message }), true);
    return false;
  }
}

// The panel answers /api/entities and /api/state in pages of 128; a layout can use more entities than that.
async function apiGetAllItems(path) {
  const items = [];
  for (let page = 0; page < 64; page += 1) {
    const separator = path.includes("?") ? "&" : "?";
    const data = await apiGet(page === 0 ? path : `${path}${separator}offset=${items.length}`);
    const pageItems = Array.isArray(data.items) ? data.items : [];
    items.push(...pageItems);
    if (pageItems.length === 0 || !Number.isFinite(data.total) || items.length >= data.total) break;
  }
  return items;
}

async function loadEntities() {
  try {
    editor.entities = await apiGetAllItems("/api/entities");
    renderEntityOptions();
    renderCanvas(); // the sensor and gauge previews show each entity's unit
  } catch (err) {
    setStatus(t("layout.status.entity_fetch_failed", { error: err.message }), true);
  }
}

async function refreshStates() {
  try {
    const items = await apiGetAllItems("/api/state");
    editor.states = new Map();
    editor.stateMeta = new Map();
    for (const item of items) {
      editor.states.set(item.entity_id, item.state);
      try {
        const attrs = JSON.parse(item.attributes_json || "{}");
        if (attrs.device_class || attrs.unit_of_measurement || attrs.vehicle?.name || typeof attrs.current_temperature === "number") {
          editor.stateMeta.set(item.entity_id, {
            device_class: attrs.device_class || "",
            unit: attrs.unit_of_measurement || "",
            vehicle_name: typeof attrs.vehicle?.name === "string" ? attrs.vehicle.name : "",
            current_temperature: typeof attrs.current_temperature === "number" ? attrs.current_temperature : null,
          });
        }
      } catch (_) { /* attributes cut off by the size limit: no meta */ }
    }
    renderCanvas();
  } catch (_) {
    // Keep previous preview values.
  }
}

async function loadEnergyPreview() {
  if (!layoutHasHaEnergyPage()) {
    if (editor.energySnapshot !== null) {
      editor.energySnapshot = null;
      renderCanvas();
    }
    return;
  }
  try {
    editor.energySnapshot = await apiGet("/api/ha/energy");
    renderCanvas();
  } catch (_) {
    if (editor.energySnapshot !== null) {
      editor.energySnapshot = null;
      renderCanvas();
    }
  }
}

function widgetSupportsName(type) {
  return WIDGET_NAME_TYPES.has(type);
}

function widgetNameDefault(type) {
  return !WIDGET_NAME_HIDDEN_BY_DEFAULT.has(type);
}

function widgetShowsName(widget) {
  if (typeof widget.show_name === "boolean") return widget.show_name;
  if (!widgetNameDefault(widget.type)) return false;
  return widget.type !== "sensor" || widget.sensor_show_label !== false;
}

// A gauge too narrow for its name would show "SH…": the name is switched off when it does not fit and back on (the type's
// default, so nothing is stored) when the gauge is wide enough. Applied when a gauge is added or its width changes; ticking
// "Show name" afterwards still wins until the next resize. Poppins at 16 px averages about 9 px per character.
const GAUGE_NAME_CHAR_PX = 9;
const GAUGE_NAME_PAD_PX = 16;

function gaugeNameFits(widget) {
  const name = String(widget.title || widget.id || "");
  return Number(widget.rect?.w || 0) >= name.length * GAUGE_NAME_CHAR_PX + GAUGE_NAME_PAD_PX;
}

function autoGaugeName(widget) {
  if (!widget || widget.type !== "gauge") return;
  if (gaugeNameFits(widget)) delete widget.show_name;
  else widget.show_name = false;
}

function widgetNamePreview(widget, tag = "div", attributes = "") {
  if (!widgetShowsName(widget)) return "";
  const name = escapeHtml(widget.title || widget.id || widget.type);
  return `<${tag}${attributes ? ` ${attributes}` : ""}>${name}</${tag}>`;
}

// ---- Room card extra field (room_extra_mode / room_extra_entity_id) ------------------------------------------
// One optional piece of information or one direct action for an entity that already belongs to the card: the room's own
// entity_id or one of its room_entities. The firmware checks the same rules (layout_validate.c, util/room_extra.c).
const ROOM_EXTRA_MODES = ["default", "humidity", "cover", "media_player"];
const ROOM_EXTRA_DOMAINS = { humidity: "sensor", cover: "cover", media_player: "media_player" };

function roomExtraCardIds(primary, extras) {
  const ids = [];
  for (const id of [primary, ...(Array.isArray(extras) ? extras : [])]) {
    if (typeof id === "string" && id && !ids.includes(id)) ids.push(id);
  }
  return ids;
}

// A humidity sensor as far as Home Assistant told us; an entity whose class is unknown is not excluded.
function roomExtraIsHumidity(id) {
  const entity = (editor.entities || []).find((candidate) => candidate?.id === id);
  return !entity || !entity.device_class || entity.device_class === "humidity";
}

function roomExtraCandidates(mode, primary, extras) {
  const domain = ROOM_EXTRA_DOMAINS[mode];
  if (!domain) return [];
  return roomExtraCardIds(primary, extras).filter(
    (id) => id.startsWith(`${domain}.`) && (mode !== "humidity" || roomExtraIsHumidity(id)));
}

// Writes the two fields onto a widget or a house room, or removes them (default mode, or an entity that does not fit).
// Returns true when a chosen entity had to be dropped.
function applyRoomExtra(target, mode, entityId, primary, extras) {
  const wanted = ROOM_EXTRA_MODES.includes(mode) && mode !== "default";
  const fits = wanted && roomExtraCandidates(mode, primary, extras).includes(entityId);
  if (fits) {
    target.room_extra_mode = mode;
    target.room_extra_entity_id = entityId;
  } else {
    delete target.room_extra_mode;
    delete target.room_extra_entity_id;
  }
  return wanted && !fits;
}

// Re-checks a saved choice against the card (after an import or an edit of the room entities).
function normalizeRoomExtra(target, primary, extras) {
  if (!target || (target.room_extra_mode === undefined && target.room_extra_entity_id === undefined)) return false;
  return applyRoomExtra(target, target.room_extra_mode, target.room_extra_entity_id, primary, extras);
}


// Compact navigation: beyond fullNavPages() pages the bottom bar shows the page icons (a number when a page has none) and the title only
// for the shown page, so pages must be told apart by icon. The icon a page really gets: its own key, "none" is no icon, empty is
// automatic, which is the room icon for the first page and nothing for the others (ui_pages_icon_key in firmware).
function effectivePageIcon(layout, index) {
  const key = typeof layout.pages[index].icon === "string" ? layout.pages[index].icon : "";
  if (key === "none") return "";
  if (key === "") return index === 0 ? "room" : "";
  return key;
}

// What is wrong with one page's tab in a layout that needs the compact bar: "missing" (it would show its number) or "shared"
// (another page has the same icon); "" when all is well or the bar is not compact.
function navIconIssue(layout, index, fullPages) {
  if (!layout || !Array.isArray(layout.pages) || layout.pages.length <= fullPages) return "";
  const mine = effectivePageIcon(layout, index);
  if (mine === "") return "missing";
  for (let i = 0; i < layout.pages.length; i++) {
    if (i !== index && effectivePageIcon(layout, i) === mine) return "shared";
  }
  return "";
}
