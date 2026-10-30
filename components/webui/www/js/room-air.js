/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
const AIR_DEFAULTS = { rh_red_low: 30, rh_low: 40, rh_high: 60, rh_red_high: 70,
  co2_amber: 1000, co2_red: 2000, pm_amber: 15, pm_red: 35, drying_margin: 1 };
const AIR_ROLES = [
  ["temperature_entity_id", "Temperature", "sensor", "temperature"],
  ["co2_entity_id", "Co2", "sensor", "co2"],
  ["pm25_entity_id", "Pm25", "sensor", "pm25"],
  ["voc_entity_id", "Voc", "sensor", "voc"],
  ["outdoor_temperature_entity_id", "OutTemperature", "sensor,weather", "out_temperature"],
  ["outdoor_humidity_entity_id", "OutHumidity", "sensor,weather", "out_humidity"],
  ["window_entity_id", "Window", "binary_sensor", "window"],
  ["native_rating_entity_id", "Rating", "sensor", "rating"],
];

function airConfigValid(c) {
  return Object.keys(AIR_DEFAULTS).every(key => Number.isFinite(c[key])) &&
    c.rh_red_low >= 0 && c.rh_red_high <= 100 && c.rh_low - c.rh_red_low > 4 &&
    c.rh_high - c.rh_low > 4 && c.rh_red_high - c.rh_high > 4 &&
    c.co2_amber > 50 && c.co2_red - c.co2_amber > 100 && c.pm_amber > 3 &&
    c.pm_red - c.pm_amber > 6 && c.drying_margin > 0 && c.drying_margin <= 20;
}

function airNormalize(role, state, unit) {
  const text = String(state ?? "");
  // Same strict decimal grammar as the firmware, including exponents; no blank, hex or trailing junk.
  if (!/^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(text)) return null;
  let n = Number(text);
  if (!Number.isFinite(n)) return null;
  if (["humidity", "out_humidity"].includes(role)) return unit === "%" && n >= 0 && n <= 100 ? n : null;
  if (["temperature", "out_temperature"].includes(role)) {
    if (unit === "°F") n = (n - 32) * 5 / 9;
    else if (unit !== "°C") return null;
    return n >= -80 && n <= 80 ? n : null;
  }
  const units = { co2: ["ppm"], pm25: ["µg/m³", "μg/m³"], voc: ["ppb"] };
  return units[role]?.includes(unit) && n >= 0 ? n : null;
}

function airLoadInspector(widget) {
  const active = widget?.type === "room_air_tile";
  for (const id of ["airSources", "airEvaluation"]) document.getElementById(id)?.classList.toggle("hidden", !active);
  for (const [key, field] of AIR_ROLES) if (el[`fAir${field}`]) el[`fAir${field}`].value = active ? widget[key] || "" : "";
  const config = { ...AIR_DEFAULTS, ...(widget?.air_thresholds || {}) };
  for (const key of Object.keys(AIR_DEFAULTS)) {
    const input = document.getElementById(`air_${key}`);
    if (input) input.value = config[key];
  }
  const enabled = document.getElementById("airOutdoorEnabled");
  if (enabled) enabled.checked = widget?.air_outdoor_enabled !== false;
  airRefreshOutdoor();
}

function airRefreshOutdoor() {
  const enabled = document.getElementById("airOutdoorEnabled");
  const fields = document.getElementById("airOutdoorFields");
  if (fields) fields.hidden = enabled?.checked === false;
}

function airSaveInspector(widget, type) {
  if (type !== "room_air_tile") return true;
  const config = {};
  for (const key of Object.keys(AIR_DEFAULTS)) {
    const input = document.getElementById(`air_${key}`);
    config[key] = input ? (String(input.value).trim() ? Number(input.value) : NaN) : (widget.air_thresholds?.[key] ?? AIR_DEFAULTS[key]);
  }
  if (!airConfigValid(config)) { setStatus(t("air.invalid_thresholds"), true); return false; }
  const next = {};
  for (const [key, field, domains] of AIR_ROLES) {
    const id = el[`fAir${field}`]?.value.trim() ?? widget[key] ?? "";
    if (id && (!/^[a-z_]+\.[a-z0-9_]+$/.test(id) || !domains.split(",").includes(entityDomainFromValue(id)))) {
      setStatus(t("air.invalid_binding", { role: t(`air.${AIR_ROLES.find(row => row[0] === key)[3]}`) }), true); return false;
    }
    next[key] = id;
  }
  // Commit only after validation, so a failed edit cannot partially replace sources.
  for (const [key] of AIR_ROLES) { if (next[key]) widget[key] = next[key]; else delete widget[key]; }
  widget.air_thresholds = config;
  const enabled = document.getElementById("airOutdoorEnabled");
  widget.air_outdoor_enabled = enabled ? enabled.checked : widget.air_outdoor_enabled !== false;
  airRefreshOutdoor();
  return true;
}

function bindAirInspector() {
  for (const [, field] of AIR_ROLES) bindInspectorAutoApply(el[`fAir${field}`], ["change"], { softEntityValidation: true });
  for (const key of Object.keys(AIR_DEFAULTS)) bindInspectorAutoApply(document.getElementById(`air_${key}`), ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(document.getElementById("airOutdoorEnabled"), ["change"], { softEntityValidation: true });
  const reset = document.getElementById("airReset");
  if (reset) reset.onclick = () => {
    for (const [key, value] of Object.entries(AIR_DEFAULTS)) document.getElementById(`air_${key}`).value = value;
    autoApplyInspector({ softEntityValidation: true });
  };
}

function airPreviewData(widget) {
  const c = { ...AIR_DEFAULTS, ...widget.air_thresholds };
  const roles = [["entity_id", "humidity"], ...AIR_ROLES.map(([key, , , role]) => [key, role])];
  const values = {}, bands = {};
  let partial = false, overall = -1;
  for (const [key, role] of roles) {
    const id = widget[key];
    if (!id) continue;
    const meta = entityDisplayInfo(id);
    const expected = { humidity: "humidity", temperature: "temperature", co2: "carbon_dioxide", pm25: "pm25" }[role];
    values[role] = expected && meta.device_class && meta.device_class !== expected ? null :
      airNormalize(role, editor.states.get(id), meta.unit);
    if (!["humidity", "co2", "pm25", "voc"].includes(role)) continue;
    const n = values[role];
    if (n === null || role === "voc") { bands[role] = -1; partial = true; continue; }
    const band = role === "humidity" ? (n < c.rh_red_low || n > c.rh_red_high ? 2 : n < c.rh_low || n > c.rh_high ? 1 : 0) :
      role === "co2" ? (n >= c.co2_red ? 2 : n >= c.co2_amber ? 1 : 0) : (n >= c.pm_red ? 2 : n >= c.pm_amber ? 1 : 0);
    bands[role] = band; overall = Math.max(overall, band);
  }
  if (partial && overall === 0) overall = -1;
  let comparison = null;
  if (widget.air_outdoor_enabled !== false) {
    for (const role of ["out_temperature", "out_humidity"]) {
      const id = widget[role === "out_temperature" ? "outdoor_temperature_entity_id" : "outdoor_humidity_entity_id"];
      if (!id?.startsWith("weather.")) continue;
      const state = editor.states.get(id), meta = entityDisplayInfo(id);
      values[role] = !state || ["unknown", "unavailable"].includes(state) ? null :
        airNormalize(role, role === "out_temperature" ? meta.weather_temperature : meta.weather_humidity,
          role === "out_temperature" ? meta.temperature_unit : "%");
    }
    if (["humidity", "temperature", "out_humidity", "out_temperature"].every(role => values[role] != null)) {
      const ah = (temp, rh) => 216.7 * rh / 100 * 6.112 * Math.exp(17.62 * temp / (243.12 + temp)) / (273.15 + temp);
      const difference = ah(values.temperature, values.humidity) - ah(values.out_temperature, values.out_humidity);
      comparison = { difference, drying: difference >= c.drying_margin, conflict: bands.co2 > 0 && bands.humidity > 0 && values.humidity > c.rh_high && difference < 0 };
    }
  }
  return { values, bands, partial, overall, comparison };
}

function airPreviewMarkup(widget, name) {
  const r = airPreviewData(widget), w = widget.rect?.w || 360, h = widget.rect?.h || 240;
  const meta = previewLineHeight(textRoleSize("meta", 20)), key = previewLineHeight(textRoleSize("key", 36));
  const color = r.overall === 0 ? "var(--pv-ok, #42b883)" : r.overall === 1 ? "#d49a28" : r.overall === 2 ? "var(--pv-error, #e75d68)" : "var(--pv-muted)";
  const fmt = (id) => {
    const state = editor.states.get(id), unit = (entityDisplayInfo(id).unit || "").replace("μg/m³", "µg/m³");
    return id && state != null && !["unknown", "unavailable", ""].includes(String(state)) ? `${state}${unit ? ` ${unit}` : ""}` : "—";
  };
  let y = 0;
  const row = (text, height, cls = "") => {
    const html = `<div class="air-preview-row ${cls}" style="top:${y}px;height:${height}px">${escapeHtml(text)}</div>`;
    y += height + 4; return html;
  };
  let rows = widgetShowsName(widget) ? row(widget.title || t("air.title"), meta, "air-preview-name") : "";
  rows += row(t(["air.not_evaluated", "air.good", "air.fair", "air.poor"][r.overall + 1]) + (r.partial ? " *" : ""), meta, "air-preview-status");
  rows += row(fmt(widget.entity_id), key, "air-preview-value");
  const pollutants = (widget.co2_entity_id || widget.pm25_entity_id) && w - 24 >= 330 && h - 24 - y >= 95;
  if (pollutants) rows += row([widget.co2_entity_id ? `CO₂ ${fmt(widget.co2_entity_id)}` : "", widget.pm25_entity_id ? `PM ${fmt(widget.pm25_entity_id)}` : ""].filter(Boolean).join(" · "), meta);
  const reasons = [r.comparison?.conflict ? "air.conflict_short" : r.bands.co2 > 0 ? "air.co2_short" : "", r.bands.humidity > 0 ? r.values.humidity < (widget.air_thresholds?.rh_low ?? 40) ? "air.dry" : "air.humid_short" : "", r.bands.pm25 > 0 ? "air.pm_short" : ""].filter(Boolean);
  const scope = !widget.co2_entity_id && !widget.pm25_entity_id && !widget.voc_entity_id ? "air.rh_range" : "air.in_range_short";
  rows += row(t(reasons[0] || (r.overall === 0 ? scope : "air.partial")) + (reasons.length > 1 || r.partial || (!pollutants && (widget.co2_entity_id || widget.pm25_entity_id)) ? " +" : ""), meta);
  if (h - 24 - y >= meta) {
    let extra = widget.native_rating_entity_id ? `${t("air.rating")}: ${fmt(widget.native_rating_entity_id)}` :
      widget.temperature_entity_id ? fmt(widget.temperature_entity_id) : t("air.humidity_scope");
    if (widget.native_rating_entity_id && entityDisplayInfo(widget.native_rating_entity_id).device_class === "enum") {
      const state = editor.states.get(widget.native_rating_entity_id);
      extra = `${t("air.rating")}: ${t(`air.enum_${state}`, {}, state || "—")}`;
    }
    rows += row(extra, meta);
  }
  return `<div class="tp tp-air" style="--air-color:${color}"><div class="air-preview-surface"></div>${rows}</div>`;
}

function airTranslateInspector() {
  for (const node of document.querySelectorAll("#airSources [data-i18n], #airEvaluation [data-i18n]")) node.textContent = t(node.dataset.i18n);
}
