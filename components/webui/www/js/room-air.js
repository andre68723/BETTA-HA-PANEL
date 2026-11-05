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
  const active = widget?.type === "air_quality_tile";
  document.getElementById("airSources")?.classList.toggle("hidden", !active);
  for (const [key, field] of AIR_ROLES) if (el[`fAir${field}`]) el[`fAir${field}`].value = active ? widget[key] || "" : "";
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
  if (type !== "air_quality_tile") return true;
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
  const enabled = document.getElementById("airOutdoorEnabled");
  widget.air_outdoor_enabled = enabled ? enabled.checked : widget.air_outdoor_enabled !== false;
  airRefreshOutdoor();
  return true;
}

function bindAirInspector() {
  for (const [, field] of AIR_ROLES) bindInspectorAutoApply(el[`fAir${field}`], ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(document.getElementById("airOutdoorEnabled"), ["change"], { softEntityValidation: true });
}

function airPreviewData(widget) {
  const c = AIR_DEFAULTS; // fixed rules; an older layout's air_thresholds are ignored
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
    if (role === "voc") { bands[role] = -1; continue; } /* display only */
    if (n === null) { bands[role] = -1; partial = true; continue; }
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
  const r = airPreviewData(widget), w = (widget.rect?.w || 360) - 24, h = (widget.rect?.h || 240) - 24;
  const meta = previewLineHeight(textRoleSize("meta", 20)), label = previewLineHeight(textRoleSize("label", 24)), value = previewLineHeight(textRoleSize("value", 28));
  const bandColor = (band) => band === 0 ? "var(--pv-ok, #42b883)" : band === 1 ? "#d49a28" : band === 2 ? "var(--pv-error, #e75d68)" : "var(--pv-muted)";
  const color = bandColor(r.overall);
  // Tile precision as in w_room_air.c: whole ppm, % and ppb; one decimal for temperature and PM2.5 below 10, no trailing ".0".
  const rounded = (state, role) => {
    const number = Number(state);
    if (!role || String(state).trim() === "" || !Number.isFinite(number)) return state;
    const decimals = role === "temperature" || (role === "pm25" && Math.abs(number) < 10) ? 1 : 0;
    return number.toFixed(decimals).replace(/\.0$/, "");
  };
  const fmt = (id, role) => {
    const state = editor.states.get(id), unit = (entityDisplayInfo(id).unit || "").replace("μg/m³", "µg/m³");
    return id && state != null && !["unknown", "unavailable", ""].includes(String(state)) ? `${rounded(state, role)}${unit ? `${unit.startsWith("°") ? "" : " "}${unit}` : ""}` : "—";
  };
  const box = (cls, x, y, width, height, text, style = "") =>
    `<div class="air-preview-row ${cls}" style="left:${x}px;top:${y}px;width:${width}px;height:${height}px;${style}">${escapeHtml(text)}</div>`;
  let html = "";
  // Header: title on the left, quality chip on the right; colour lives only in the chips and in the readings that cause it.
  const reasons = [r.comparison?.conflict ? "air.conflict_short" : r.bands.co2 > 0 ? "air.co2_short" : "", r.bands.humidity > 0 ? r.values.humidity < (widget.air_thresholds?.rh_low ?? 40) ? "air.dry" : "air.humid_short" : "", r.bands.pm25 > 0 ? "air.pm_short" : ""].filter(Boolean);
  // Nothing to advise when everything is fine: the status chip alone says Good (w_room_air.c).
  const hasAdvice = reasons.length > 0 || r.overall !== 0;
  const advice = t(reasons[0] || "air.partial") + (reasons.length > 1 || r.partial ? " ›" : "");
  const status = t(["air.not_evaluated", "air.good", "air.fair", "air.poor"][r.overall + 1]) + (r.partial ? " *" : "");
  const chipH = meta + 12, chipY = h - chipH, headerH = Math.max(label, chipH), qW = Math.min(status.length * meta * 0.55 + 24, w * 0.45), qX = w - qW;
  const top = headerH + 6;
  html += `<div class="air-preview-chip" style="left:${qX}px;top:0;width:${qW}px;height:${chipH}px;--chip:${color}"></div>`;
  html += box("air-preview-chip-text", qX, (chipH - meta) / 2, qW, meta, status, `color:${color}`);
  if (widgetShowsName(widget)) html += box("air-preview-name", 0, (headerH - label) / 2, Math.max(qX - 8, 0), label, `${widget.show_icon === true ? "🌬️ " : ""}${widget.title || t("air.title")}`);
  // Bottom: the advice chip, only when there is something to do.
  const showAdvice = hasAdvice && w >= 70;
  if (showAdvice) {
    html += `<div class="air-preview-chip" style="left:0;top:${chipY}px;width:${w}px;height:${chipH}px;--chip:${color}"></div>`;
    html += box("air-preview-chip-text", 6, chipY + (chipH - meta) / 2, w - 12, meta, advice, `color:${color}`);
  }
  // Middle: up to four readings as value over label, chosen by priority and shown in a fixed order.
  const keyOf = { humidity: "entity_id", co2: "co2_entity_id", pm25: "pm25_entity_id", temperature: "temperature_entity_id", voc: "voc_entity_id" };
  const maxCols = w >= 440 ? 4 : 3;
  const picked = ["humidity", "co2", "pm25", "temperature", "voc"].filter((role) => widget[keyOf[role]]).slice(0, maxCols);
  const order = ["co2", "humidity", "pm25", "temperature", "voc"].filter((role) => picked.includes(role));
  const zoneH = (showAdvice ? chipY - 6 : h) - top, blockH = value + meta + 2, withLabel = zoneH >= blockH;
  const hasRating = widget.native_rating_entity_id || widget.window_entity_id, ratingFits = hasRating && withLabel && zoneH >= blockH + meta + 6;
  const usedH = (withLabel ? blockH : value) + (ratingFits ? meta + 6 : 0), by = top + Math.max((zoneH - usedH) / 2, 0);
  if (zoneH >= value) order.forEach((role, i) => {
    const cw = w / order.length, x = i * cw, band = r.bands[role];
    html += box("air-preview-col-value", x, by, cw - 4, value, fmt(widget[keyOf[role]], role), band > 0 ? `color:${bandColor(band)}` : "");
    if (withLabel) html += box("air-preview-col-label", x, by + value + 2, cw - 4, meta, t(`air.${role}`));
  });
  if (ratingFits) {
    const windowState = () => {
      const state = editor.states.get(widget.window_entity_id);
      return state === "on" ? t("air.open") : state === "off" ? t("air.closed") : "—";
    };
    let extra = "";
    if (widget.native_rating_entity_id) {
      extra = `${t("air.rating")}: ${fmt(widget.native_rating_entity_id)}`;
      if (entityDisplayInfo(widget.native_rating_entity_id).device_class === "enum") {
        const state = editor.states.get(widget.native_rating_entity_id);
        extra = `${t("air.rating")}: ${t(`air.enum_${state}`, {}, state || "—")}`;
      }
      if (widget.window_entity_id) extra += ` · ${windowState()}`;
    } else extra = `${t("air.window")}: ${windowState()}`;
    html += box("air-preview-extra", 0, by + blockH + 6, w, meta, extra);
  }
  return `<div class="tp tp-air">${html}</div>`;
}

function airTranslateInspector() {
  for (const node of document.querySelectorAll("#airSources [data-i18n]")) node.textContent = t(node.dataset.i18n);
}
