// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The editor's schematic previews: the solar, car, Sonos and generic tile previews (tilePreviewMarkup) and the
// house page's grid, laid out like ui_house_page.c. Moved out of canvas.js; classic script, loaded before it.
// tools/compare_editor_previews.py puts them next to the simulator's renderings.

// Mirrors ui_style_action_button(): primary, selected and active controls use accent.
function previewControlClass(active = false, enabled = true, danger = false, primary = false) {
  return !enabled ? "control-disabled" : danger ? "control-danger" : primary ? "control-primary" : active ? "control-active" : "control-action";
}

function previewControlAccentStyle(widget) {
  const accent = [widget.button_accent_color, widget.slider_accent_color].find(v => /^#[0-9a-f]{6}$/i.test(v || ""));
  if (!accent) return "";
  const nav = document.getElementById("canvas")?.style?.getPropertyValue?.("--pv-nav-active") || "#343436";
  const rgb = hex => hex.slice(1).match(/../g).map(v => parseInt(v, 16));
  const a = rgb(accent), b = /^#[0-9a-f]{6}$/i.test(nav) ? rgb(nav) : [52, 52, 54];
  const fill = a.map((v, i) => Math.round((v * 180 + b[i] * 75) / 255));
  const fg = (fill[0] * 3 + fill[1] * 4 + fill[2]) / 8 > 150 ? "#101418" : "#ffffff";
  return `--pv-control-state:${accent};--pv-control-active-bg:rgb(${fill.join(",")});--pv-control-active-fg:${fg}`;
}

function previewControlAvailable(entityId, allowUnknown = false) {
  const raw = editor.states.get(entityId);
  return Boolean(entityId) && raw !== undefined && raw !== null && raw !== "" &&
    raw !== "unavailable" && (allowUnknown || raw !== "unknown");
}

function previewSupports(entityId, feature, unknown = true) {
  const features = editor.stateMeta.get(entityId)?.supported_features;
  return typeof features === "number" ? (features & feature) !== 0 : unknown;
}

// Mirrors util/room_extra.c. A running entity without a usable Pause/Stop action is not an active button.
function previewRoomAction(widget, direction = "toggle") {
  const id = widget.room_extra_entity_id, state = previewEntityState(id);
  const fallback = widget.room_extra_mode === "cover" ? "\u2191" : "\u25B6";
  let action = "";
  if (widget.room_extra_mode === "media_player") {
    if (state === "playing" && previewSupports(id, 1)) action = "pause";
    else if (["paused", "idle", "on", "standby"].includes(state) && previewSupports(id, 16384)) action = "play";
  } else {
    const pos = editor.stateMeta.get(id)?.current_position;
    if (direction === "up") {
      if (state === "opening" && previewSupports(id, 8)) action = "stop";
      else if (["closed", "closing"].includes(state) || state === "open" && typeof pos === "number" && pos < 100) action = "up";
    } else if (direction === "down") {
      if (state === "closing" && previewSupports(id, 8)) action = "stop";
      else if (["open", "opening"].includes(state)) action = "down";
    } else if (["opening", "closing"].includes(state) && previewSupports(id, 8)) action = "stop";
    else if (state === "closed") action = "up";
    else if (state === "open") action = "down";
  }
  return { symbol: ({pause:"\u23F8",play:"\u25B6",stop:"\u25A0",up:"\u2191",down:"\u2193"})[action] || fallback,
    role: previewControlClass(["pause", "stop"].includes(action), Boolean(action) && previewControlAvailable(id)) };
}

function previewPreconditionActive(widget) {
  const id = widget.precond_entity_id || "";
  const state = previewEntityState(id);
  if (/^(switch|input_boolean)\./.test(id)) return state === "on";
  return id.startsWith("climate.") && ["heat", "cool", "heat_cool", "auto", "dry", "fan_only"].includes(state);
}

function solarPreviewValue(entityId) {
  const raw = editor.states.get(entityId) ?? "";
  const parsed = Number(String(raw).replace(",", "."));
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : null;
}

function solarPreviewFormatValue(value, includeUnit = true) {
  if (value === null) return "--";
  const formatted = value.toFixed(0); // whole kWh, as solar_format_value() in w_solar_forecast.c
  return includeUnit ? `${formatted} kWh` : formatted;
}

function solarPreviewRows(widget) {
  const rows = [
    { label: t("preview.solar.rest"), entityId: widget.entity_id },
    // Two-letter weekday names from today on, as solar_day_label() ("solar.weekday_*") once the panel's clock is set.
    { label: previewWeekday(0).slice(0, 2), entityId: widget.forecast_today_entity_id },
    { label: previewWeekday(1).slice(0, 2), entityId: widget.forecast_tomorrow_entity_id },
    { label: previewWeekday(2).slice(0, 2), entityId: widget.forecast_day_3_entity_id },
    { label: previewWeekday(3).slice(0, 2), entityId: widget.forecast_day_4_entity_id },
    { label: previewWeekday(4).slice(0, 2), entityId: widget.forecast_day_5_entity_id },
    { label: previewWeekday(5).slice(0, 2), entityId: widget.forecast_day_6_entity_id },
    { label: previewWeekday(6).slice(0, 2), entityId: widget.forecast_day_7_entity_id },
  ];
  const values = rows.map((row) => solarPreviewValue(row.entityId));
  const configuredMax = Number(widget.solar_forecast_bar_max_kwh);
  const maxValue = Number.isFinite(configuredMax) && configuredMax > 0
    ? configuredMax
    : Math.max(1, ...values.filter((value) => value !== null));
  return rows.map((row, index) => {
    const value = values[index];
    const percent = value === null ? 0 : clamp(Math.round((value * 100) / maxValue), 0, 100);
    return { ...row, value, percent };
  });
}

function solarPreviewMarkup(widget) {
  const orientation = normalizeSolarForecastBarOrientation(widget.solar_forecast_bar_orientation);
  const allRows = solarPreviewRows(widget);
  // solar_visible_rows(): 30 px rows (on the 10-inch boards as tall as the LABEL line) with 3 px gaps from y 54, at least
  // three and at most seven (five while no sensor is set for day 6 or 7, as before those slots existed).
  const rowH = previewTextAdaptive() ? 30 : Math.max(30, previewLineHeight(textRoleSize("label", 24)));
  const fitting = clamp(Math.floor(((Number(widget.rect && widget.rect.h) || 310) - 68 + 3) / (rowH + 3)), 3, 7);
  // The remaining reading shares today's row: the part of today's total that is still to come stays bright, the rest is dimmed.
  const rest = allRows[0].value;
  const today = allRows[1].value;
  const aligned = widget.align_forecast === true && orientation === "vertical"; // as w_solar_forecast.c: columns only
  // w_solar_forecast.c keeps five days for an automatic tile without day 6 or 7 sensors; an explicit day count is honoured.
  const lateSensors = Boolean(widget.forecast_day_6_entity_id || widget.forecast_day_7_entity_id);
  const days = allRows.slice(1, lateSensors || widget.forecast_days > 0 ? undefined : 6);
  const columnCap = Math.max(1, Math.floor(((Number(widget.rect && widget.rect.w) || 0) - 28) / 34)); // narrowest column
  const rows = aligned ? days.slice(0, forecastPreviewCount(widget, 7))
    : orientation === "vertical" ? days.slice(0, columnCap) : days.slice(0, fitting);
  const rowMarkup = rows.map((row) => {
    const split = row === allRows[1] && rest !== null && today !== null && today > 0
      ? ` style="--split:${100 - clamp(Math.round((rest * 100) / today), 0, 100)}%"` : "";
    const remainingClass = split ? " has-rest" : "";
    const widthStyle = `width:${row.percent}%`;
    const heightStyle = `height:${row.percent}%`;
    if (orientation === "vertical") {
      return `
        <div class="solar-preview-col${remainingClass}">
          <div class="solar-preview-vbar"><i style="${heightStyle}"><u${split}></u></i></div>
          <strong>${escapeHtml(row.label)}</strong>
          <span>${escapeHtml(solarPreviewFormatValue(row.value, false))}</span>
        </div>
      `;
    }
    return `
      <div class="solar-preview-row${remainingClass}">
        <strong>${escapeHtml(row.label)}</strong>
        <div class="solar-preview-hbar"><i style="${widthStyle}"><u${split}></u></i></div>
        <span>${escapeHtml(solarPreviewFormatValue(row.value))}</span>
      </div>
    `;
  }).join("");
  return `
    <div class="solar-preview ${orientation}">
      ${widgetNamePreview(widget, "div", 'class="solar-preview-title"')}
      <div class="solar-preview-unit">kWh</div>
      <div class="solar-preview-body" style="--cols:${Math.max(1, rows.length)}">${rowMarkup}</div>
    </div>
    <div class="resize-handle"></div>
  `;
}

function carPreviewNumber(entityId, fallback = null) {
  const raw = entityId ? String(editor.states.get(entityId) ?? "").trim() : "";
  const parsed = raw === "" ? NaN : Number(raw.replace(",", "."));
  return Number.isFinite(parsed) ? parsed : fallback;
}

// The optional extras of the car tile (only from 330 px height and 200 px width, like the panel): a status line with the
// lock on the left and the interior temperature on the right, the pre-conditioning button across the card below it.
function carPreviewExtras(widget) {
  const w = Number(widget.rect && widget.rect.w) || 0;
  const h = Number(widget.rect && widget.rect.h) || 0;
  const status = [widget.lock_entity_id ? `<span class="car-x-ok">${pvText("car_locked")}</span>` : "",
    widget.interior_temp_entity_id ? "<span>21.5\u00B0C</span>" : ""].join("");
  const button = widget.precond_entity_id ? `<span class="car-x-btn ${previewControlClass(previewPreconditionActive(widget), previewControlAvailable(widget.precond_entity_id, /^(button|input_button)\./.test(widget.precond_entity_id)))}">${pvText("car_precond")}</span>` : "";
  if (h < 330 || w < 200 || (!status && !button)) return { markup: "", height: 0 };
  const line = previewLineHeight(textRoleSize("meta", 20));
  const height = (status ? line : 0) + (status && button ? 6 : 0) + (button ? 44 : 0);
  return { markup: `<div class="car-preview-extras">${status ? `<div class="car-x-status">${status}</div>` : ""}${button}</div>`, height };
}

// Always kilometres, as w_car_widget.c: some cars report metres (ID.4) or miles.
function carPreviewRangeText(entityId) {
  const range = carPreviewNumber(entityId, null);
  if (range === null) return "-- km";
  const unit = entityDisplayInfo(entityId).unit || "km";
  const factor = { km: 1, m: 0.001, mi: 1.609344 }[unit];
  return factor === undefined ? `${range.toFixed(0)} ${unit}` : `${(range * factor).toFixed(0)} km`;
}

// The ring and the texts placed on it like car_layout() in w_car_widget.c: the ring takes the card but the extras row, the
// range above and the charge in the middle, the name in the ring's open segment. Panel4 and Tab5 use larger roles from 190 px.
function carPreviewMarkup(widget) {
  const w = Number(widget.rect && widget.rect.w) || 220;
  const h = Number(widget.rect && widget.rect.h) || 260;
  const soc = clamp(Math.round(carPreviewNumber(widget.entity_id, 27)), 0, 100);
  const power = carPreviewNumber(widget.secondary_entity_id, null);
  const powerText = power === null ? "-- W" : (Math.abs(power) >= 10000 ? `${(power / 1000).toFixed(1)} kW` : `${power.toFixed(0)} W`);
  const extras = carPreviewExtras(widget);
  const minDim = Math.min(w, h);
  const size = Math.max(96, Math.min(minDim - 24, h - 24 - (extras.height ? extras.height + 12 : 0)));
  const r = size / 2;
  const cy = 12 + r;
  const large = previewTextAdaptive() && minDim >= 190;
  const role = (name) => textRoleSize(name, PREVIEW_ROLE_FALLBACK[name]);
  const titleRole = minDim >= 190 ? "label" : "meta";
  const at = (top, px, extra = "") => `style="top:${Math.round(top)}px;font-size:${px}px;${extra}"`;
  const socRole = large ? (widget.secondary_entity_id ? "value" : "key") : "value";
  return `
    <div class="car-preview${widget.secondary_entity_id ? " with-power" : ""}" style="--soc:${soc};">
      <div class="car-preview-gauge" style="width:${size}px;top:12px;left:${Math.round((w - size) / 2)}px"></div>
      <div class="car-preview-range" ${at(large ? cy - 61 : cy - (r * 3) / 5, role(large ? "value" : "meta"), `width:${w - 20}px`)}>${escapeHtml(carPreviewRangeText(widget.tertiary_entity_id))}</div>
      <div class="car-preview-soc" ${at(large ? cy - 18 : cy - 24, role(socRole))}>${escapeHtml(String(soc))}<span style="font-size:${role(large ? "label" : "meta")}px">%</span></div>
      ${widget.secondary_entity_id ? `<div class="car-preview-power" ${at(large ? cy + 20 : cy + (r * 3) / 10, role("meta"), `width:${w - 36}px`)}>${escapeHtml(powerText)}</div>` : ""}
      ${widgetNamePreview(widget, "div", `class="car-preview-title" ${at(cy + (r * 37) / 50 - previewLineHeight(role(titleRole)) / 2, role(titleRole), `width:${Math.round((r * 13) / 10)}px`)}`)}
      ${extras.markup}
    </div>
    <div class="resize-handle"></div>
  `;
}

function sonosFavoritesPreviewMarkup(widget) {
  const sampleRows = ["Absolut TOP", "SWR3", "Yoga"];
  // Name, the player's state and one button per favourite (w_sonos_favorites.c).
  const state = previewEntityState(widget.entity_id) || "playing";
  return `
    <div class="sonos-preview">
      ${widgetNamePreview(widget, "div", 'class="sonos-preview-title"')}
      <div class="sonos-preview-meta">${escapeHtml(t(`preview.media.state.${state}`, {}, state))}</div>
      <div class="sonos-preview-list">
        ${sampleRows.map((name) => `<div class="${previewControlClass(false, previewControlAvailable(widget.entity_id))}">\u25B6 ${escapeHtml(name)}</div>`).join("")}
      </div>
    </div>
    <div class="resize-handle"></div>
  `;
}

// Schematic previews of the tile layouts, so every widget type shows more than its name.
// The value a sensor or gauge preview shows: the entity's live state and unit when the panel has
// them, "--" with the unit before a first state, and the given sample only while no entity is set,
// so a power sensor does not read "23.4 °C".
function previewEntityValue(entityId, sample, format = (value) => value) {
  if (!entityId) return sample;
  const unit = entityDisplayInfo(entityId).unit || "";
  const raw = editor.states.get(entityId);
  const known = raw !== undefined && raw !== null && raw !== "" && !["unavailable", "unknown"].includes(String(raw));
  return escapeHtml(`${known ? format(String(raw)) : "--"}${unit ? ` ${unit}` : ""}`);
}

// A sensor state as the panel shows it by device class (w_sensor.c, ha_sensor_value.c, ha_timestamp_format.c):
// timestamps in local time or relative, dates, durations, readable enum values, binary sensor words, the unit
// choice and the decimal comma of the language. Null before a first state, so previewEntityValue shows "--".
const PREVIEW_COMMA_LANGUAGES = ["de", "fr", "it", "es", "nl", "pt", "pl", "cs", "da", "sv", "nb", "fi", "ru", "tr"];
const PREVIEW_UNIT_LADDERS = [["W", "kW", "MW", "GW"], ["Wh", "kWh", "MWh", "GWh"], ["VA", "kVA", "MVA", "GVA"],
  ["var", "kvar", "Mvar", "Gvar"]];
const PREVIEW_CURRENCY_SYMBOLS = { EUR: "€", USD: "$", GBP: "£", JPY: "¥" };

function previewLocalizeNumber(text) {
  const language = String(editor.i18nLanguage || "en").slice(0, 2);
  return PREVIEW_COMMA_LANGUAGES.includes(language) && /^[+-]?\d*\.?\d*$/.test(text) && /\d/.test(text)
    ? text.replace(".", ",") : text;
}

function previewSensorNumber(raw, unit, dc, mode) {
  let number = raw;
  const numeric = /^[+-]?\d*\.?\d*$/.test(raw) && /\d/.test(raw);
  if (numeric && mode === "auto") {
    const ladder = PREVIEW_UNIT_LADDERS.find((units) => units.includes(unit));
    let value = Number(raw);
    let index = ladder ? ladder.indexOf(unit) : -1;
    const start = index;
    while (ladder && Math.abs(value) >= 1000 && index < 3) { value /= 1000; index += 1; }
    if (index !== start) {
      const a = Math.abs(value);
      number = value.toFixed(a < 10 ? 2 : a < 100 ? 1 : 0);
      unit = ladder[index];
    }
  }
  // A currency code rounds like Home Assistant's frontend; the panel does so only without a display precision.
  if (numeric && dc === "monetary" && /^[A-Z]{3}$/.test(unit)) number = Number(raw).toFixed(unit === "JPY" ? 0 : 2);
  if (numeric) number = previewLocalizeNumber(number);
  if (mode === "hide" || !unit) return number;
  if (mode === "degree" && unit.startsWith("°")) return `${number}°`;
  if (mode !== "code" && dc === "monetary") return `${number} ${PREVIEW_CURRENCY_SYMBOLS[unit] || unit}`;
  return `${number} ${unit}`;
}

function previewSensorDuration(raw, unit, style) {
  const factor = { s: 1, ms: 0.001, min: 60, h: 3600, d: 86400 }[unit];
  if (!factor || !/^[+-]?\d*\.?\d+$/.test(raw)) return null;
  const total = Math.round(Number(raw) * factor);
  const sign = total < 0 ? "-" : "";
  const s = Math.abs(total);
  const [d, h, m, sec] = [Math.floor(s / 86400), Math.floor(s % 86400 / 3600), Math.floor(s % 3600 / 60), s % 60];
  const pad = (v) => String(v).padStart(2, "0");
  if (style === "clock") return `${sign}${Math.floor(s / 3600)}:${pad(m)}:${pad(sec)}`;
  const word = (key) => t(`preview.sensor.dur_${key}`);
  const pair = (a, ka, b, kb) => `${sign}${a} ${word(ka)}${b ? ` ${b} ${word(kb)}` : ""}`;
  if (s < 60) return `${sign}${sec} ${word("s")}`;
  if (s < 3600) return pair(m, "min", sec, "s");
  if (s < 86400) return pair(h, "h", m, "min");
  return pair(d, "d", h, "h");
}

// The Binary tile's word for the state of a binary sensor of this class (binary_kind_text() in w_binary.c).
function previewBinaryStateText(dc, on) {
  const pairs = [
    [["presence", "occupancy"], "home", "away"], [["motion", "moving", "vibration"], "motion", "clear"],
    [["door", "window", "opening", "garage_door"], "open", "closed"], [["lock"], "unlocked", "locked"],
    [["moisture"], "wet", "dry"],
    [["smoke", "gas", "carbon_monoxide", "safety", "problem", "tamper", "heat"], "alarm", "ok"],
    [["battery"], "low", "ok"], [["connectivity"], "connected", "disconnected"],
  ];
  const pair = pairs.find(([classes]) => classes.includes(dc));
  return t(`preview.binary_state.${pair ? (on ? pair[1] : pair[2]) : (on ? "on" : "off")}`);
}

function previewSensorTimestamp(raw, style) {
  const match = raw.match(/^\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$/);
  const instant = match ? new Date(raw.replace(" ", "T")) : null;
  if (!instant || Number.isNaN(instant.getTime())) return null;
  if (style === "relative") {
    const diff = Math.round((instant.getTime() - Date.now()) / 1000);
    const span = Math.abs(diff);
    const future = diff > 0;
    const [unit, n] = span < 60 ? ["", 0] : span < 3600 ? ["min", Math.floor(span / 60)]
      : span < 48 * 3600 ? ["hour", Math.floor(span / 3600)] : ["day", Math.floor(span / 86400)];
    const key = !unit ? (future ? "now" : "just_now") : `${future ? "in" : "ago"}_${unit}${n === 1 ? "_one" : ""}`;
    return t(`preview.sensor.${key}`, { n });
  }
  const pad = (v) => String(v).padStart(2, "0");
  const date = previewDateTimeValue(`${instant.getFullYear()}-${pad(instant.getMonth() + 1)}-${pad(instant.getDate())}`);
  const time = `${pad(instant.getHours())}:${pad(instant.getMinutes())}`;
  return style === "date" ? date : style === "time" ? time : `${date} ${time}`;
}

function previewSensorDate(raw, style) {
  const match = raw.match(/^(\d{4})-(\d{2})-(\d{2})$/);
  if (!match) return null;
  if (style !== "relative") return previewDateTimeValue(raw);
  const today = new Date();
  const days = Math.round((Date.UTC(+match[1], +match[2] - 1, +match[3]) -
    Date.UTC(today.getFullYear(), today.getMonth(), today.getDate())) / 86400000);
  if (days === 0) return t("preview.sensor.today");
  if (days === 1) return t("preview.sensor.tomorrow");
  if (days === -1) return t("preview.sensor.yesterday");
  return t(days > 0 ? "preview.sensor.in_day" : "preview.sensor.ago_day", { n: Math.abs(days) });
}

function previewSensorValue(widget) {
  const id = widget.entity_id;
  if (!id) return null;
  const info = entityDisplayInfo(id);
  const raw = String(editor.states.get(id) ?? "");
  if (!raw || ["unavailable", "unknown"].includes(raw)) return null;
  const dc = info.device_class || "";
  const unit = info.unit || "";
  const format = widget.sensor_format || "";
  const timestampStyles = ["datetime", "date", "time", "relative"];
  let text = null;
  if (dc === "timestamp" || (!dc && timestampStyles.includes(format))) {
    text = previewSensorTimestamp(raw, timestampStyles.includes(format) ? format : "datetime");
  }
  if (text === null && dc === "date") text = previewSensorDate(raw, format);
  if (text === null && id.startsWith("binary_sensor.") && (raw === "on" || raw === "off")) text = previewBinaryStateText(dc, raw === "on");
  if (text === null && dc === "duration" && format !== "raw") text = previewSensorDuration(raw, unit, format);
  if (text === null && dc === "enum" && /^[a-z0-9_]*[a-z][a-z0-9_]*$/.test(raw)) {
    text = raw.replace(/_/g, " ").replace(/^./, (c) => c.toUpperCase());
  }
  if (text === null) text = previewSensorNumber(raw, unit, dc, widget.sensor_unit || "");
  return escapeHtml(text);
}

function previewDateTimeValue(raw) {
  if (/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:Z|[+-]\d{2}:\d{2})$/.test(raw)) {
    const instant = new Date(raw);
    if (!Number.isNaN(instant.getTime())) {
      const pad = (n) => String(n).padStart(2, "0");
      raw = `${instant.getFullYear()}-${pad(instant.getMonth() + 1)}-${pad(instant.getDate())} ${pad(instant.getHours())}:${pad(instant.getMinutes())}:${pad(instant.getSeconds())}`;
    }
  }
  const match = raw.match(/^(\d{4})-(\d{2})-(\d{2})(?:[ T](\d{2}):(\d{2})(?::(\d{2}))?)?/);
  if (match) {
    const language = editor.i18nLanguage || "en";
    const date = language.startsWith("en") ? `${match[2]}/${match[3]}/${match[1]}`
      : language.startsWith("de") ? `${match[3]}.${match[2]}.${match[1]}`
        : `${match[3]}/${match[2]}/${match[1]}`;
    const time = match[4] ? `${match[4]}:${match[5]}${match[6] && match[6] !== "00" ? `:${match[6]}` : ""}` : "";
    return `${date}${time ? ` ${time}` : ""}`;
  }
  return /^\d{2}:\d{2}/.test(raw) ? raw.replace(/:00$/, "") : raw;
}

function previewWeatherIcon(condition) {
  const icons = { sunny: "☀️", "clear-night": "🌙", partlycloudy: "⛅", cloudy: "☁️",
    rainy: "🌧️", pouring: "🌧️", snowy: "❄️", lightning: "⛈️", "lightning-rainy": "⛈️" };
  return icons[condition] || "⛅";
}

function previewWeatherCondition(widget) {
  const state = editor.states.get(widget.entity_id) || "partlycloudy";
  return { icon: previewWeatherIcon(state), text: escapeHtml(t(`preview.weather.${state}`, {}, state)) };
}

// Sample texts of the schematic tile previews, in the editor language.
function pvText(key, vars = {}) {
  return escapeHtml(t(`layout.preview.${key}`, vars));
}

// The state of an entity the preview may show instead of a fixed sample ("" until the panel reported one).
function previewEntityState(entityId) {
  const raw = entityId ? editor.states.get(entityId) : "";
  return raw === undefined || raw === null || ["unavailable", "unknown"].includes(String(raw)) ? "" : String(raw);
}

// What a room card summarises from the states of its entities (room_evaluate() in w_room.c): the temperature, the
// humidity and one chip each for windows and doors, lights, presence and low batteries. Without a state the samples apply.
function roomPreviewSummary(entityId, extraIds) {
  const info = (id) => entityDisplayInfo(id);
  const state = (id) => previewEntityState(id);
  const out = { temp: null, hum: null, contacts: 0, open: 0, lights: 0, lightsOn: 0, presence: 0, present: 0, battery: 0,
    switches: 0, switchesOn: 0, covers: 0, coversOpen: 0 };
  for (const id of [entityId, ...(Array.isArray(extraIds) ? extraIds : [])]) {
    if (!id) continue;
    const dc = info(id).device_class || "";
    const st = state(id);
    const on = st === "on" || st === "open";
    const domain = String(id).split(".")[0];
    if (domain === "light") {
      out.lights += 1;
      if (on) out.lightsOn += 1;
    } else if (domain === "switch") {
      out.switches += 1;
      if (on) out.switchesOn += 1;
    } else if (domain === "cover") {
      out.covers += 1;
      if (on) out.coversOpen += 1;
    } else if (domain === "binary_sensor") {
      if (["window", "door", "opening", "garage_door"].includes(dc)) {
        out.contacts += 1;
        if (on) out.open += 1;
      } else if (dc === "battery") {
        if (on) out.battery += 1;
      } else if (["motion", "occupancy", "presence", "moving"].includes(dc)) {
        out.presence += 1;
        if (on) out.present += 1;
      }
    } else if (domain === "climate") {
      // Like room_evaluate(): a climate entity gives the temperature when no sensor of the card did.
      const current = editor.stateMeta?.get(id)?.current_temperature;
      if (out.temp === null && st !== "" && Number.isFinite(current)) out.temp = `${current}\u00B0C`;
    } else if (domain === "sensor") {
      const value = st === "" ? NaN : Number(String(st).replace(",", "."));
      if (!Number.isFinite(value)) continue;
      if (dc === "battery") {
        if (value <= 20) out.battery += 1;
      } else if (out.temp === null && (dc === "temperature" || /[CF]/.test(info(id).unit || ""))) {
        out.temp = `${st}${info(id).unit ? (/^°/.test(info(id).unit) ? "" : " ") + info(id).unit : "\u00B0C"}`;
      } else if (out.hum === null && dc === "humidity") {
        out.hum = `${Math.round(value)} %`;
      }
    }
  }
  return out;
}

// The control row of a room card with room_controls (w_room_controls.c): one toggle each for its lights, switches and
// covers, worded like rc_text(); the lights then leave the chip row.
function roomPreviewControls(summary) {
  const word = (on, one, many, off) => (on === 0 ? t(off) : on === 1 ? t(one) : t(many, { n: on }));
  const controls = [];
  if (summary.lights) controls.push([summary.lightsOn > 0, word(summary.lightsOn, "layout.preview.room_light_on_one", "layout.preview.room_lights_on_n", "layout.preview.room_lights_off")]);
  if (summary.switches) controls.push([summary.switchesOn > 0, word(summary.switchesOn, "layout.preview.room_switch_on", "layout.preview.room_switches_on_n", "layout.preview.room_switches_off")]);
  if (summary.covers) controls.push([summary.coversOpen > 0, t(summary.coversOpen ? "layout.preview.room_open" : "layout.preview.room_closed")]);
  return controls.map(([on, text]) => `<i class="${previewControlClass(on)} tp-room-ctl">${escapeHtml(text)}</i>`).join("");
}

function roomPreviewChips(summary, cardWidth, withControls = false) {
  const chips = [];
  if (summary.contacts > 0) chips.push(["contacts", summary.open ? "c-amber" : "c-green", summary.open ? t("layout.preview.room_open_n", { n: summary.open }) : t("layout.preview.room_closed")]);
  if (summary.lights > 0 && !withControls) {
    const text = summary.lightsOn === 0 ? t("layout.preview.room_lights_off") : summary.lightsOn === 1 ? t("layout.preview.room_light_on_one")
      : t("layout.preview.room_lights_on_n", { n: summary.lightsOn });
    chips.push(["lights", summary.lightsOn ? "c-amber" : "c-muted", text]);
  }
  if (summary.presence > 0) chips.push(["presence", summary.present ? "c-teal" : "c-muted", summary.present ? t("layout.preview.room_present") : t("layout.preview.room_empty")]);
  if (summary.battery > 0) chips.push(["battery", "c-amber", t("layout.preview.room_battery_n", { n: summary.battery })]);
  // Like room_layout() in w_room.c: the chips share the row (card width minus 2 x 14 px padding, 8 px gaps); while more
  // than two do not get 100 px each, presence and then lights give way.
  const chipsW = (Number(cardWidth) || 300) - 28;
  let visible = chips.length;
  for (const drop of ["presence", "lights"]) {
    const at = chips.findIndex(([key]) => key === drop);
    if (at >= 0 && visible > 2 && (chipsW - 8 * (visible - 1)) / visible < 100) {
      chips.splice(at, 1);
      visible -= 1;
    }
  }
  return chips.map(([, cls, text]) => `<i class="${cls}">${escapeHtml(text)}</i>`).join("");
}

// Line height of a panel text size: k_text_metrics in text_scale.c, interpolated between its sizes.
const PREVIEW_LINE_METRICS = [[16, 21], [20, 25], [24, 32], [28, 35], [32, 39], [36, 44], [42, 53], [48, 60], [56, 69], [64, 80]];
function previewLineHeight(px) {
  const rows = PREVIEW_LINE_METRICS;
  if (px <= rows[0][0]) return Math.round((px * rows[0][1]) / rows[0][0]);
  for (let i = 1; i < rows.length; i += 1) {
    const [p1, l1] = rows[i];
    const [p0, l0] = rows[i - 1];
    if (px <= p1) return Math.round(l0 + ((px - p0) * (l1 - l0)) / (p1 - p0));
  }
  const [pn, ln] = rows[rows.length - 1];
  return Math.round((px * ln) / pn);
}

// Width of a preview text in the panel font; a rough estimate where the canvas is unavailable (tests).
function previewTextWidth(text, px) {
  try {
    return readabilityTextWidth(text, px);
  } catch (_) {
    return Math.ceil(String(text).length * px * 0.58);
  }
}

// The default heating arc (open at the bottom, 160 to 20 degrees), placed like heating_apply_layout() in w_heating_tile.c:
// the radius fills the room between the name and the status line, the target and the reading sit slightly low in the
// ring (the reading drops from KEY to VALUE when it would reach the status), the status at the bottom.
function heatingArcPreview(widget, w, h) {
  const role = (key) => textRoleSize(key, PREVIEW_ROLE_FALLBACK[key]);
  const line = previewLineHeight;
  const cw = Math.max(1, w - 32);
  const ch = Math.max(1, h - 32);
  const arcW = w >= 300 ? 16 : 15;
  const areaTop = widgetShowsName(widget) ? line(role("meta")) + 6 : 0;
  const statusH = line(role("meta"));
  const areaBottom = ch - statusH - 4;
  let radius = Math.floor((areaBottom - areaTop - arcW / 2) / 1.342);
  radius = Math.min(radius, Math.floor(cw / 2), 170);
  let cy;
  for (;;) {
    const used = radius + Math.floor(radius * 0.342) + arcW / 2;
    cy = areaTop + (areaBottom - areaTop - used) / 2 + radius;
    if (cy + radius <= ch + 15 || radius <= 35) break;
    radius -= 1;
  }
  const cx = cw / 2;
  const statusTop = ch - statusH;
  let actualPx = role("key");
  let stackH = line(role("meta")) + 2 + line(actualPx);
  if (cy - radius + arcW + stackH > statusTop - 2) {
    actualPx = role("value");
    stackH = line(role("meta")) + 2 + line(actualPx);
  }
  let stackCy = cy + radius / 6;
  if (stackCy + stackH / 2 > statusTop - 2) stackCy = statusTop - 2 - stackH / 2;
  const top = stackCy - stackH / 2;
  const size = radius * 2;
  const r = radius - arcW / 2;
  const span = (2 * Math.PI * r * 220) / 360;
  const ring = `<svg class="tp-semi-ring" style="left:${Math.round(cx - radius)}px;top:${Math.round(cy - radius)}px;width:${size}px;height:${size}px" viewBox="0 0 ${size} ${size}">
    <g transform="rotate(160 ${radius} ${radius})" fill="none" stroke-width="${arcW}" stroke-linecap="round">
    <circle class="track" cx="${radius}" cy="${radius}" r="${r}" stroke-dasharray="${span} ${4 * Math.PI * r}"/>
    <circle class="fill" cx="${radius}" cy="${radius}" r="${r}" stroke-dasharray="${span * 0.5} ${4 * Math.PI * r}"/></g></svg>`;
  const centred = (cls, text, px, y) => `<${cls ? "small" : "b"} ${cls ? `class="${cls}" ` : ""}style="left:${Math.round(cx)}px;top:${Math.round(y)}px;font-size:${px}px">${escapeHtml(text)}</${cls ? "small" : "b"}>`;
  return `<div class="tp-semi">${ring}${centred("tp-heat-target", t("layout.preview.heating_target", { temp: "22.5\u00B0C" }), role("meta"), top)}${centred("", "21.0\u00B0C", actualPx, top + line(role("meta")) + 2)}${centred("tp-heat-status", t("layout.preview.heating_active"), role("meta"), statusTop)}</div>`;
}

// The semi arc of a heating tile, placed like heating_apply_layout() in w_heating_tile.c (16 px card padding): the ring
// against the edge opposite its opening, plus and minus in the opening, target, reading and status between them, and the
// scale ends just outside the ring's ends. The reading drops from KEY to VALUE and the block loses "Target" and the status
// when they do not fit.
function heatingSemiPreview(widget, w, h, opening) {
  const role = (key) => textRoleSize(key, PREVIEW_ROLE_FALLBACK[key]);
  const line = previewLineHeight;
  const cw = Math.max(1, w - 32);
  const ch = Math.max(1, h - 32);
  const titleReserve = widgetShowsName(widget) ? line(role("meta")) + 6 : 0;
  const arcW = w >= 300 ? 16 : 15;
  const btn = 48;
  const gap = 8;
  const sideways = opening === "left" || opening === "right";
  const availH = ch - titleReserve;
  const size = Math.max(1, Math.min(cw, availH, 340));
  const cx = opening === "left" ? cw - size / 2 : opening === "right" ? size / 2 : cw / 2;
  const cy = opening === "bottom" ? titleReserve + size / 2 : opening === "top" ? ch - size / 2 : titleReserve + availH / 2;
  const rInner = size / 2 - arcW - arcW / 2;
  let textCx = cx;
  let textCy = cy;
  let textW;
  let textH;
  const buttons = [];
  if (sideways) {
    const bx = opening === "left" ? 0 : cw - btn;
    buttons.push(["+", bx, cy - gap / 2 - btn], ["\u2212", bx, cy + gap / 2]);
    const lo = opening === "left" ? btn + gap : cx - rInner + gap;
    const hi = opening === "left" ? cx + rInner - gap : cw - btn - gap;
    textCx = (lo + hi) / 2;
    textW = hi - lo;
    textH = 2 * rInner;
  } else {
    const by = opening === "top" ? titleReserve : ch - btn;
    buttons.push(["\u2212", cx - gap / 2 - btn, by], ["+", cx + gap / 2, by]);
    const lo = opening === "top" ? by + btn + gap : cy - rInner + gap;
    const hi = opening === "top" ? cy + rInner - gap : by - gap;
    textCy = (lo + hi) / 2;
    textW = (rInner * 3) / 2;
    textH = hi - lo;
  }
  const actual = "21.0\u00B0C";
  const actualPx = previewTextWidth(actual, role("key")) > textW ? role("value") : role("key");
  let target = t("layout.preview.heating_target", { temp: "22.5\u00B0C" });
  const status = t("layout.preview.heating_active");
  let withStatus = previewTextWidth(status, role("meta")) <= textW;
  const stack = line(role("meta")) + 2 + line(actualPx) + (withStatus ? 2 + line(role("meta")) : 0);
  if (previewTextWidth(target, role("meta")) > textW || stack > textH) {
    target = "22.5\u00B0C";
    withStatus = false;
  }
  const total = line(role("meta")) + 2 + line(actualPx) + (withStatus ? 2 + line(role("meta")) : 0);
  const top = textCy - total / 2;
  // Scale ends: min at the ring's start, max at its end, pushed out towards the opening and kept inside the card.
  const [start, stop] = { left: [270, 90], right: [90, 270], top: [0, 180], bottom: [180, 360] }[opening];
  const dirY = opening === "top" ? -1 : sideways ? 0 : 1;
  const rMid = size / 2 - arcW / 2;
  const endLabel = (text, angle) => {
    const rad = (angle * Math.PI) / 180;
    const dirX = sideways ? (opening === "left" ? -1 : 1) : (Math.cos(rad) >= 0 ? 1 : -1);
    const lw = previewTextWidth(text, role("meta"));
    const lh = line(role("meta"));
    const x = cx + rMid * Math.cos(rad) + dirX * (arcW / 2 + 4 + lw / 2) - lw / 2;
    const y = cy + rMid * Math.sin(rad) + dirY * (arcW / 2 + 4 + lh / 2) - lh / 2;
    return `<em class="tp-scale" style="left:${Math.round(clamp(x, 0, cw - lw))}px;top:${Math.round(clamp(y, 0, ch - lh))}px">${text}</em>`;
  };
  const r = size / 2 - arcW / 2;
  const half = Math.PI * r;
  const ring = `<svg class="tp-semi-ring" style="left:${Math.round(cx - size / 2)}px;top:${Math.round(cy - size / 2)}px;width:${size}px;height:${size}px" viewBox="0 0 ${size} ${size}">
    <g transform="rotate(${start} ${size / 2} ${size / 2})" fill="none" stroke-width="${arcW}" stroke-linecap="round">
    <circle class="track" cx="${size / 2}" cy="${size / 2}" r="${r}" stroke-dasharray="${half} ${2 * half}"/>
    <circle class="fill" cx="${size / 2}" cy="${size / 2}" r="${r}" stroke-dasharray="${half * 0.5} ${2 * half}"/></g></svg>`;
  const centred = (cls, text, px, y) => `<${cls === "b" ? "b" : "small"} class="${cls === "b" ? "" : cls}" style="left:${Math.round(textCx)}px;top:${Math.round(y)}px;font-size:${px}px">${escapeHtml(text)}</${cls === "b" ? "b" : "small"}>`;
  return `<div class="tp-semi">${ring}${endLabel("5", start)}${endLabel("30", stop)}
    ${buttons.map(([text, x, y]) => `<span class="tp-semi-btn ${previewControlClass(false, previewControlAvailable(widget.entity_id))}" style="left:${Math.round(x)}px;top:${Math.round(y)}px">${text}</span>`).join("")}
    ${centred("tp-heat-target", target, role("meta"), top)}${centred("b", actual, actualPx, top + line(role("meta")) + 2)}
    ${withStatus ? centred("tp-heat-status", status, role("meta"), top + line(role("meta")) + 2 + line(actualPx) + 2) : ""}</div>`;
}

// A gauge's extra reading (gauge_format_info() in w_gauge.c): a distance in metres from 1 km on reads as kilometres.
function previewGaugeInfo(entityId) {
  const info = entityDisplayInfo(entityId);
  const metres = Number(editor.states.get(entityId));
  if (info.device_class === "distance" && info.unit === "m" && Number.isFinite(metres) && Math.abs(metres) >= 1000) {
    return escapeHtml(`${previewLocalizeNumber((metres / 1000).toFixed(1).replace(/\.0$/, ""))} km`);
  }
  return previewEntityValue(entityId, "");
}

// A cleaning mode as the panel names it (w_rr_mode_label() in w_vacuum.c): the panel's "vacmode" translation, otherwise the
// raw option with spaces and a capital.
function previewVacuumMode(raw) {
  if (!raw) return "";
  const key = raw.toLowerCase().replace(/[ -]/g, "_");
  const translated = editor.i18nEffective?.lvgl?.vacmode?.[key];
  return typeof translated === "string" && translated ? translated : raw.replace(/_/g, " ").replace(/^[a-z]/, (c) => c.toUpperCase());
}

// The line under a scene tile's name: what a tap does, by domain (scene_kind_text() in w_scene.c).
function previewSceneKind(entityId) {
  const id = String(entityId || "");
  if (id.startsWith("script.")) return "scene_script";
  if (id.startsWith("button.") || id.startsWith("input_button.")) return "scene_button";
  if (id.startsWith("lock.")) return "scene_lock";
  return "scene";
}

// Lock and alarm tiles from 100 px height write the name in LABEL (VALUE from 240 px width) and the state in LABEL.
function previewRoomyClass(w, h) {
  return h >= 100 ? (w >= 240 ? " tp-roomy tp-roomy-wide" : " tp-roomy") : "";
}

// The round symbol of a lock, alarm or scene tile: as in w_lock.c, larger from 100 px height and 260 px width.
function previewCircleSize(w, h) {
  const roomy = h >= 100;
  const max = roomy ? (w >= 260 ? 72 : 56) : 52;
  return `width:${clamp(h - 28, 30, max)}px`;
}

// The room tile's ring diameter, or 0 when the card shows the plain number (w_room.c): the ring must hold "88.8°C" in the
// meta text between its strokes and needs 140 px of width.
function roomPreviewRing(widget, w, h) {
  const meta = textRoleSize("meta", 20);
  const line = Math.round(meta * 1.5);
  const ringTop = 10 + (widgetShowsName(widget) ? line + 6 : 0);
  const chipTop = h - (line + 10) - 12;
  const availW = w - 28;
  const ring = Math.min(chipTop - ringTop - 6, Math.floor((availW * 42) / 100));
  return ring >= Math.max(Math.round(meta * 3.2) + 22, 56) && availW >= 140 ? Math.min(ring, 108) : 0;
}

// The unit's blocks of a segmented gauge preview; `filled` of `count` are lit.
function previewSegments(count, filled, direction) {
  return `<div class="tp-segs ${direction}">${Array.from({ length: count }, (_, i) => `<u${i < filled ? ' class="on"' : ""}></u>`).join("")}</div>`;
}

// The graph line of a preview: the polyline, a smoothed path, the same with points, or bars (graph_display_mode).
function previewGraphMarkup(mode) {
  const ys = [30, 22, 26, 12, 18, 8, 14];
  const xs = ys.map((_, i) => Math.round((i * 100) / (ys.length - 1)));
  if (mode === "bars") {
    const bars = [8, 14, 24, 33, 28, 20, 12, 16];
    return `<svg viewBox="0 0 100 40" preserveAspectRatio="none">${bars.map((v, i) => `<rect x="${i * 12.5 + 1.5}" y="${40 - v}" width="9.5" height="${v}" fill="#6fe8ff"/>`).join("")}</svg>`;
  }
  const smooth = mode === "line_smooth" || mode === "line_smooth_points";
  let d = `M${xs[0]},${ys[0]}`;
  for (let i = 1; i < ys.length; i += 1) {
    const mid = (xs[i - 1] + xs[i]) / 2;
    d += smooth ? ` C${mid},${ys[i - 1]} ${mid},${ys[i]} ${xs[i]},${ys[i]}` : ` L${xs[i]},${ys[i]}`;
  }
  const points = mode === "line_smooth_points" ? ys.map((y, i) => `<circle cx="${xs[i]}" cy="${y}" r="1.6" fill="#6fe8ff"/>`).join("") : "";
  return `<svg viewBox="0 0 100 40" preserveAspectRatio="none"><path d="${d}" fill="none" stroke="#6fe8ff" stroke-width="2" vector-effect="non-scaling-stroke"/>${points}</svg>`;
}

// Short weekday names in the editor language, `offset` days from today.
function previewWeekday(offset) {
  const date = new Date();
  date.setDate(date.getDate() + offset);
  try {
    return date.toLocaleDateString(editor.i18nLanguage || "en", { weekday: "short" }).replace(/\.$/, "");
  } catch (_) {
    return ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"][date.getDay()];
  }
}

function tilePreviewMarkup(widget) {
  if (widget.type === "wallbox_evcc") return evccPreviewMarkup(widget);
  const name = (tag, attributes = "") => widgetNamePreview(widget, tag, attributes);
  const w = Number(widget.rect && widget.rect.w) || 0;
  const h = Number(widget.rect && widget.rect.h) || 0;
  const handle = '<div class="resize-handle"></div>';
  switch (widget.type) {
    case "weather_3day": {
      const cols = forecastPreviewCount(widget);
      const current = previewWeatherCondition(widget);
      const days = [t("preview.weather.today"), ...[1, 2, 3, 4, 5, 6].map(previewWeekday)];
      const rainValues = [["5 %", "0 mm"], ["20 %", "0.2 mm"], ["80 %", "6.5 mm"], ["40 %", "1.4 mm"], ["10 %", "0 mm"], ["0 %", "0 mm"], ["30 %", "0.8 mm"]];
      const conditions = [editor.states.get(widget.entity_id) || "partlycloudy", "sunny", "rainy", "sunny", "partlycloudy", "sunny", "cloudy"];
      // The 10-inch boards (weather_tile_render.c without UI_TEXT_ADAPTIVE): a large icon with temperature and a two-line
      // condition, the report beside them, cards at most 96 px wide whose height decides the low and rain lines.
      // A short tile gets a one-line header with a 40 px icon and the temperature in META.
      const fixed = !previewTextAdaptive();
      const line = (key) => previewLineHeight(textRoleSize(key, PREVIEW_ROLE_FALLBACK[key]));
      const headY = widgetShowsName(widget) ? 36 : 8;
      const cardFixed = line("label") + 4 + line("value") + 12;
      const large = w - 32 >= 260 && h - 32 - (headY + 122) >= cardFixed + 64;
      const shortHead = !large && h - 32 - (headY + 74) < cardFixed + 24;
      const iconPx = large ? 112 : shortHead ? 40 : 64;
      const textH = shortHead ? 2 * line("meta") : line("value") + 4 + 2 * line("meta");
      const cardH = Math.min(h - 32 - (headY + Math.max(iconPx, textH) + 10), 236);
      const rainLines = widget.show_precipitation === false ? 0 : fixed ? (cardH >= 210 ? 2 : cardH >= 180 ? 1 : 0) : (h >= 200 ? 2 : 0);
      const showLow = !fixed || cardH >= 150;
      const cards = days.slice(0, cols).map((d, i) =>
        `<div class="tp-day"><span>${d}</span><i>${previewWeatherIcon(conditions[i])}</i><b>${22 - i}°</b>${showLow ? `<small>${11 + i}°</small>` : ""}${rainLines ? `<em class="tp-rain">${rainValues[i].slice(0, rainLines).join("<br>")}</em>` : ""}</div>`).join("");
      const report = fixed && widget.secondary_entity_id ? t("preview.weather.report") : ""; // the panel shows the report attribute "s"
      const head = fixed
        ? `<div class="tp-wx-head tp-wx-fixed${shortHead ? " tp-wx-short" : ""}" style="--icon:${iconPx}px"><i>${current.icon}</i><div><b>21.5 °C</b><small>${current.text}${shortHead ? " | " : "<br>"}38 %</small></div>${report && w - 32 - iconPx - 218 >= 150 ? `<p>${escapeHtml(report)}</p>` : ""}</div>`
        : `<div class="tp-wx-head"><i>${current.icon}</i><b>21.5 °C</b><small>${current.text} | 38 %</small></div>`;
      return `<div class="tp tp-weather3${fixed ? " tp-weather3-fixed" : ""}">${name('div', 'class="tp-name"')}
        ${head}<div class="tp-days" style="--cols:${cols}">${cards}</div></div>${handle}`;
    }
    case "weather_tile": {
      const current = previewWeatherCondition(widget);
      if (widget.weather_hourly === true) {
        const meta = textRoleSize("meta", 20), value = textRoleSize("value", 32);
        const innerH = h - 28, titleH = widgetShowsName(widget) ? Math.round(meta * 1.5) + 6 : 0;
        const large = w - 28 >= 400 && innerH >= titleH + 112 + meta * 3 + 72;
        const icon = large ? 112 : 64, headH = Math.max(icon, Math.round((value + meta) * 1.5) + 4);
        const iconHour = innerH - titleH - headH - 8 >= meta * 3 + 64 ? 48 : 24;
        const cols = Array.from({length: 24}, (_, i) => `<div class="tp-weather-hour"><small>${String((10+i)%24).padStart(2,"0")}:00</small><i style="height:${iconHour}px">${current.icon}</i><small>${12+i%4}°C</small></div>`).join("");
        return `<div class="tp tp-weather-hourly">${name('div', 'class="tp-name"')}<div class="tp-weather-hourly-head" style="height:${headH}px"><i style="width:${icon}px;height:${icon}px">${current.icon}</i><div><b>21.5 °C</b><small>${current.text}</small></div></div><div class="tp-weather-hours">${cols}</div></div>${handle}`;
      }
      const extras = h >= 200 ? `<div class="tp-weather-extras"><small>${escapeHtml(t("preview.weather.low"))} 12°C</small><small>0.8 mm</small></div><small class="tp-weather-hum">${escapeHtml(t("preview.weather.humidity"))} 38 %</small>` : "";
      return `<div class="tp tp-weather${h < 222 ? " tp-weather-short" : ""}">${name('div', 'class="tp-name"')}
        <div class="tp-weather-main"><i class="tp-big">${current.icon}</i><b>21.5 °C</b></div><small>${current.text}</small>${extras}</div>${handle}`;
    }
    case "media_player": {
      // Like the panel (w_media_player.c): the cover takes what is left after the text, progress, controls and volume
      // rows (about 226 px plus the name line), so a tile below ~330 px shows none; shuffle and repeat need ~280 px.
      const showCover = h - 28 - (widgetShowsName(widget) ? 22 : 0) - 226 >= 72;
      const extras = w >= 280;
      const playing = previewEntityState(widget.entity_id) === "playing";
      const controls = previewControlClass(false, previewControlAvailable(widget.entity_id), false, true);
      const mediaMeta = editor.stateMeta.get(widget.entity_id) || {};
      const available = previewControlAvailable(widget.entity_id), action = previewControlClass(false, available);
      const shuffle = extras && previewSupports(widget.entity_id, 32768);
      const repeat = extras && previewSupports(widget.entity_id, 262144);
      const showArtist = showCover || h >= 234;
      // Position and duration beside the progress bar, the larger play button in the middle and the volume bar with its
      // speaker symbol, or between minus and plus in the glow and glass styles (w_media_player.c).
      return `<div class="tp tp-media${showCover ? "" : " tp-media-nocover"}" style="${previewControlAccentStyle(widget)}">${name('div', 'class="tp-name"')}
        ${showCover ? '<div class="tp-cover"></div>' : ""}<div class="tp-track"><b>${pvText("song")}</b>${showArtist ? `<small>${pvText("artist")}</small>` : ""}</div>
        <div class="tp-media-progress"><small>1:24</small><div class="tp-bar"><i style="width:35%"></i></div><small>3:30</small></div>
        <div class="tp-btns">${shuffle ? `<span class="${action}${available && mediaMeta.shuffle ? " control-selected-icon" : ""}">\u21C4</span>` : ""}<span class="${action}">\u23EE</span><span class="tp-primary ${controls}">${playing ? "\u23F8" : "\u23F5"}</span><span class="${action}">\u23ED</span>${repeat ? `<span class="${action}${available && mediaMeta.repeat && mediaMeta.repeat !== "off" ? " control-selected-icon" : ""}">\u21BB</span>` : ""}</div>
        <div class="tp-vol"><span class="tp-vol-btn">\u2212</span><i>\uD83D\uDD08</i><div class="tp-bar tp-bar-knob"><i style="width:45%"></i><u></u></div><span class="tp-vol-btn">+</span></div></div>${handle}`;
    }
    case "vacuum_tile": {
      const state = String(editor.states.get(widget.entity_id) ?? "");
      const cleaning = ["cleaning", "returning", "mopping"].includes(state);
      const wide = w >= h * 1.8;
      const robot = h >= 260 || wide;
      // The robot (or the map picture of the second entity), name and state, the suction level as a select field and three
      // square buttons (w_vacuum.c); a cleaning robot lights the card.
      const art = widget.secondary_entity_id ? '<div class="tp-vac-map"><i></i><i></i><i></i><i></i></div>' : '<div class="tp-robot"><i></i></div>';
      // Without the picture (or beside it) name and state are left-aligned next to the charge chip; the cleaning mode of the
      // select entity and the suction level share the chip row.
      const battery = Number(previewEntityState(widget.battery_entity_id));
      const batteryText = widget.battery_entity_id && previewEntityState(widget.battery_entity_id) !== "" && Number.isFinite(battery) ? `${Math.round(battery)} %` : "82 %";
      const mode = widget.tertiary_entity_id ? `<div class="tp-vac-select ${previewControlClass(false, previewControlAvailable(widget.tertiary_entity_id))}">${escapeHtml(previewVacuumMode(previewEntityState(widget.tertiary_entity_id)) || "--")}</div>` : "";
      return `<div class="tp tp-vac${wide ? " tp-vac-wide" : ""}${robot && !wide ? "" : " tp-vac-beside"}${cleaning ? " active" : ""}">${robot ? art : ""}<div class="tp-vac-body">${name('div', 'class="tp-name"')}<span class="tp-vac-battery">${batteryText}</span>
        <small>${state ? escapeHtml(t(`preview.vacuum.state.${state}`, {}, state)) : pvText("vacuum_docked")}</small><div class="tp-vac-chips">${mode}<div class="tp-vac-select ${previewControlClass(false, previewControlAvailable(widget.entity_id))}">balanced</div></div>
        <div class="tp-vac-btns"><span class="${previewControlClass(cleaning, previewControlAvailable(widget.entity_id))}">${cleaning ? "\u23F8" : "\u23F5"}</span>${previewSupports(widget.entity_id, 8, false) ? `<span class="${previewControlClass(false, previewControlAvailable(widget.entity_id))}">\u23F9</span>` : ""}<span class="${previewControlClass(false, previewControlAvailable(widget.entity_id))}">\u2302</span></div></div></div>${handle}`;
    }
    case "cover_tile":
      // Buttons as tall as the tile allows (h - 24 px, 32-44 px, 40 below 300 px width), like w_cover.c.
      return `<div class="tp tp-blind" style="--btn:${clamp(Math.min(h - 24, w < 300 ? 40 : 44), 32, 44)}px"><div class="tp-blind-text">${name('b', '')}<small>${pvText("open_pct", { pct: 70 })}</small><div class="tp-bar"><i style="width:70%"></i></div></div>
        <div class="tp-btns"><span class="${previewControlClass(previewEntityState(widget.entity_id) === "opening", previewControlAvailable(widget.entity_id))}">\u2191</span>${previewSupports(widget.entity_id, 8) ? `<span class="${previewControlClass(false, previewControlAvailable(widget.entity_id))}">\u25A0</span>` : ""}<span class="${previewControlClass(previewEntityState(widget.entity_id) === "closing", previewControlAvailable(widget.entity_id))}">\u2193</span></div></div>${handle}`;
    case "fan_tile":
      if (h >= 220 && w >= 200) {
        const meta = editor.stateMeta.get(widget.entity_id);
        const on = previewEntityState(widget.entity_id) === "on";
        const enabled = previewControlAvailable(widget.entity_id);
        const step = meta?.percentage_step > 0 ? meta.percentage_step : 100 / 3;
        const count = clamp(Math.round(100 / step), 2, 6);
        const current = Math.round((meta?.percentage ?? 60) / (count === 6 && step < 100 / 6 ? 100 / 6 : step));
        const presets = meta?.preset_modes || [t("layout.preview.fan_auto"), t("layout.preview.fan_sleep"), t("layout.preview.fan_breeze")];
        const speeds = Array.from({ length: count }, (_, i) => `<i class="${previewControlClass(current === i + 1, enabled)}">${i + 1}</i>`).join("");
        const choices = presets.map((preset, i) => `<i class="${previewControlClass(meta ? meta.preset_mode === preset : i === 0, enabled)}">${escapeHtml(preset)}</i>`).join("");
        return `<div class="tp tp-fanbig${on ? "" : " fan-off"}"><div class="tp-fanbig-top">${name('b', '')}<small>${on ? pvText("fan_on", { pct: meta?.percentage ?? 60 }) : escapeHtml(t("layout.preview.off"))}${meta?.preset_mode ? ` · ${escapeHtml(meta.preset_mode)}` : ""}</small><span class="tp-fan-power ${previewControlClass(on, enabled)}">\u23FB</span></div><div class="tp-rotor"><svg viewBox="0 0 100 100" aria-hidden="true">
          <circle class="tp-fan-guard" cx="50" cy="50" r="46"/>
          <g class="tp-fan-blades">${[0, 120, 240].map(angle => `<path transform="rotate(${angle} 50 50)" d="M47 46 C34 39 30 25 38 16 C43 10 57 12 66 18 C75 26 67 40 53 47 Z"/>`).join("")}</g>
          <circle class="tp-fan-hub" cx="50" cy="50" r="8"/></svg></div>
          <div class="tp-chips">${speeds}</div>${choices ? `<div class="tp-chips">${choices}</div>` : ""}</div>${handle}`;
      }
      {
        // A fan that is off reads OFF with an empty bar; buttons h - 24 px within 32-44 px (w_fan.c).
        const off = !previewControlAvailable(widget.entity_id) || previewEntityState(widget.entity_id) === "off";
        return `<div class="tp tp-blind" style="--btn:${clamp(h - 24, 32, 44)}px"><div class="tp-blind-text">${name('b', '')}<small>${off ? escapeHtml(t("layout.preview.off", {}, "OFF")) : pvText("fan_on", { pct: 60 })}</small><div class="tp-bar"><i style="width:${off ? 0 : 60}%"></i></div></div>
        <div class="tp-btns"><span class="${previewControlClass(false, previewControlAvailable(widget.entity_id))}">\u2212</span><span class="tp-primary ${previewControlClass(!off, previewControlAvailable(widget.entity_id))}">\u23FB</span><span class="${previewControlClass(false, previewControlAvailable(widget.entity_id))}">+</span></div></div>${handle}`;
      }
    case "camera_tile":
      return `<div class="tp tp-camera"><div class="tp-cam-img">${widget.camera_live ? escapeHtml(t("layout.preview.camera_connecting")) : "\uD83D\uDCF7"}</div>${name('div', 'class="tp-cam-strip"')}</div>${handle}`;
    case "contact_tile": {
      const open = previewEntityState(widget.entity_id) !== "off";
      return `<div class="tp tp-contact"><div class="tp-door${open ? "" : " closed"}"><i></i></div><div class="tp-blind-text">${name('b', '')}<small class="${open ? "tp-amber" : "tp-green"}">${pvText(open ? "contact_open" : "contact_closed")}</small></div></div>${handle}`;
    }
    case "lock_tile": {
      const state = editor.states.get(widget.entity_id) || "locked";
      return `<div class="tp tp-contact${previewRoomyClass(w, h)}"><div class="tp-icon-circle ${state === "jammed" && previewControlAvailable(widget.entity_id) ? "control-alert" : previewControlClass(["unlocked", "unlocking", "open", "opening"].includes(state), previewControlAvailable(widget.entity_id))}" style="${previewCircleSize(w, h)}">${["locked", "locking"].includes(state) ? "\uD83D\uDD12" : "\uD83D\uDD13"}</div><div class="tp-blind-text">${name('b', '')}<small>${escapeHtml(t(`preview.lock.state.${state}`, {}, state))}</small></div></div>${handle}`;
    }
    case "alarm_tile": {
      const state = editor.states.get(widget.entity_id) || "disarmed";
      const armed = state.startsWith("armed");
      return `<div class="tp tp-contact${previewRoomyClass(w, h)}"><div class="tp-icon-circle ${["pending", "triggered"].includes(state) && previewControlAvailable(widget.entity_id) ? "control-alert" : previewControlClass(armed, previewControlAvailable(widget.entity_id))}" style="${previewCircleSize(w, h)}">${armed ? "\uD83D\uDD12" : "\uD83D\uDD13"}</div><div class="tp-blind-text">${name('b', '')}<small>${escapeHtml(t(`preview.alarm.state.${state}`, {}, state.replace(/_/g, " ")))}</small></div></div>${handle}`;
    }
    case "energy_timeline": {
      // As w_energy_timeline.c: name and the time of the last update on top; four counter rings with unit and caption and a
      // legend row, or nested rings with a list of readings; the 24 hours as bars above (solar, battery, grid) and below
      // (feed-in, charging) a zero line with the forecast line and four times underneath.
      const nested = widget.style_variant === "nested";
      const palette = energyTimelinePalette(widget);
      const label = (key) => escapeHtml(t(`preview.etl.${key}`));
      const head = `<div class="tp-etl-head">${name('span', '')}<span>${escapeHtml(t("preview.etl.updated", { time: "14:35" }))}</span></div>`;
      let top;
      let withPlot = true;
      if (nested) {
        // etl_layout_nested(): the timeline only from 330 px inside the card; the rows beside the rings only for the
        // configured meters, in short words when the column is under 210 px, the three nested-only rows not then.
        const metaLine = previewLineHeight(textRoleSize("meta", 20));
        const innerH = h - (10 + metaLine + 4) - 10;
        withPlot = innerH >= 330;
        const blockH = withPlot ? (innerH * 55) / 100 : innerH;
        const d = Math.max(60, Math.min(blockH, (w * (w < 400 ? 40 : 46)) / 100));
        const narrow = w - (10 + d + 20) - 10 < 210;
        const solar = !!widget.tertiary_entity_id, battery = !!(widget.battery_charge_entity_id || widget.battery_discharge_entity_id);
        const rows = [["home_use", "home", "#ffffff", "23.1 kWh", true], ["from_solar", "solar_short", palette.solar, "8.9 kWh", solar],
          ["from_grid", "grid", palette.grid, "1.2 kWh", true], ["feed_in", "feed_in", palette.export, "7.5 kWh", !!widget.secondary_entity_id],
          ["from_battery", "battery", palette.battery_out, "13.0 kWh", battery], ["solar_meter", "", palette.solar_dest, "31.5 kWh", !narrow && solar],
          ["charge", "", palette.battery_in, "5.3 kWh", !narrow && battery], ["soc", "battery", palette.battery_out, "64 %", !!widget.battery_soc_entity_id]]
          .filter((row) => row[4]).slice(0, Math.floor(blockH / (metaLine + 2)));
        const rings = [[100, palette.battery_out, 64], [82, palette.solar, 70], [64, palette.battery_in, 55], [46, palette.battery_out, 40]]
          .map(([r, c, p]) => `<i style="--r:${r}%;--p:${p};--c:${c}"></i>`).join("");
        const list = rows.map(([key, short, c, v]) => `<li style="--c:${c}">${label(narrow && short ? short : key)} ${v}</li>`).join("");
        top = `<div class="tp-etl-top"><div class="tp-etl-nested" style="width:${Math.round(d)}px">${rings}<b>23.1</b></div><ul class="tp-etl-list">${list}</ul></div>`;
      } else {
        const counters = [["home", palette.solar, 70, "23.1", "kWh"], ["feed_in", palette.solar, 100, "7.5", "kWh"],
          ["solar_short", palette.solar_dest, 55, "31.5", "kWh"], ["battery", palette.battery_out, 64, "64", "%"]];
        const legend = [["solar_short", palette.solar], ["battery", palette.battery_out], ["grid", palette.grid], ["feed_in", palette.export],
          ["charging", palette.battery_in], ["forecast", palette.forecast]].map(([key, c]) => `<em style="--c:${c}">${label(key)}</em>`).join("");
        top = `<div class="tp-etl-top">${counters.map(([key, color, pct, value, unit]) =>
          `<span class="tp-etl-counter"><i style="--p:${pct};--c:${color}"><b>${value}<small>${unit}</small></b></i><small>${label(key)}</small></span>`).join("")}</div>
          <div class="tp-etl-legend">${legend}</div>`;
      }
      const bars = Array.from({ length: 48 }, (_, i) => {
        const day = Math.max(0, Math.sin(((i - 6) / 48) * Math.PI * 2));
        const up = 12 + Math.round(70 * day), down = Math.round(60 * Math.max(0, Math.sin((i / 48) * Math.PI * 2 + 2.2)));
        return `<u><b style="height:${up}%;background:linear-gradient(0deg,${palette.solar} 0 ${day > 0.2 ? 70 : 0}%,${palette.battery_out} 0)"></b><i style="height:${down}%;background:${i % 3 ? palette.battery_in : palette.export}"></i></u>`;
      }).join("");
      const plot = withPlot ? `<div class="tp-etl-bars">${bars}<svg class="tp-etl-line" viewBox="0 0 100 40" preserveAspectRatio="none"><path d="M0,6 C10,8 14,16 22,18 S70,18 76,14 S88,2 100,4" fill="none" stroke="${palette.forecast}" stroke-width="1.6" vector-effect="non-scaling-stroke"/></svg></div>
        <div class="tp-etl-axis"><small>14:45</small><small>20:45</small><small>02:45</small><small>08:45</small></div>` : "";
      return `<div class="tp tp-etl${nested ? " tp-etl-is-nested" : ""}${withPlot ? "" : " tp-etl-noplot"}">${head}${top}${plot}</div>${handle}`;
    }
    case "energy_price_forecast": {
      // A day of bars like the panel (w_energy_price_forecast.c): below the day's average green, above it orange, hours that are over dimmed,
      // the running hour outlined; the entity's state is the price now (EUR/kWh), the profile is a typical day.
      const raw = Number(String(editor.states.get(widget.entity_id) ?? "").replace(",", "."));
      const price = Number.isFinite(raw) && raw > 0 && raw < 5 ? (raw * 100).toFixed(1).replace(".", ",") : "20,1";
      // 96 quarter hours of a typical day-ahead curve (ct/kWh)
      const profile = [21, 21, 20, 19, 20, 19, 18, 17, 17, 17, 16, 16, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 15, 15, 15, 15, 15, 15, 16, 16, 17, 17, 19, 19, 19, 18, 18, 17, 17, 16, 17, 16, 16, 15, 15, 15, 14, 14, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 13, 13, 14, 14, 15, 15, 15, 16, 16, 20, 17, 20, 21, 22, 21, 21, 22, 24, 25, 24, 24, 22, 21, 20, 20, 20, 21, 20, 18, 17, 18, 17, 18, 16, 16, 16, 16, 16];
      const avg = profile.reduce((a, b) => a + b, 0) / profile.length;
      const peak = Math.max(...profile);
      const now = new Date();
      const slotNow = now.getHours() * 4 + Math.floor(now.getMinutes() / 15);
      const clock = (slot) => `${String(Math.floor(slot / 4) % 24).padStart(2, "0")}:${String((slot % 4) * 15).padStart(2, "0")}`;
      const interval = `${clock(slotNow)}-${clock(slotNow + 1)}`;
      const daySwitch = widget.price_selector === true;
      // Without the day buttons the view is the 24 hours around now (the running interval in the middle, as on the panel); with them it is the day.
      const shift = daySwitch ? 0 : slotNow - 48;
      const slotAt = (i) => (((i + shift) % 96) + 96) % 96;
      const shown = profile.map((_, i) => slotAt(i));
      const bars = shown.map((s, i) => {
        const v = profile[s];
        const cls = (v > avg ? "" : "cheap") + (s < slotNow ? " past" : "") + (s === slotNow ? " now" : "");
        return `<i class="${cls}" style="height:${Math.round((v / peak) * 100)}%"></i>`;
      }).join("");
      // Options of the price timeline card: day_switch shows the day buttons, cheap_times shades the cheap hours and names the next window.
      const days = daySwitch
        ? `<span class="tp-price-days"><em class="control-active">${escapeHtml(t("preview.price.today"))}</em>${Number(widget.rect?.w) < 360 ? "" : `<em class="control-action">${escapeHtml(t("preview.price.tomorrow"))}</em>`}</span>` : "";
      const cheap = widget.price_cheap_times === true;
      // the cheap window 11:00-16:00 (slots 44..64) at its place in the view
      const bandStart = ((44 - shift) % 96 + 96) % 96;
      const bandWidth = Math.min(20, 96 - bandStart);
      const band = cheap ? `<u class="tp-price-band" style="left:${(bandStart / 96 * 100).toFixed(1)}%;width:${(bandWidth / 96 * 100).toFixed(1)}%"></u>` : "";
      // The bottom line: the next cheap window on the left (cheap_times), the day's lowest and highest price on the right.
      const low = Math.min(...profile).toFixed(1).replace(".", ","), high = peak.toFixed(1).replace(".", ",");
      const stats = w >= 360 ? `<span class="tp-price-stats">min ${low}  max ${high}</span>` : "";
      const foot = cheap || stats ? `<div class="tp-price-cheap">${cheap ? `<span>${escapeHtml(t("preview.price.cheap"))} 11:00-16:00</span>` : "<span></span>"}${stats}</div>` : "";
      // ticks at 00, 06, 12 and 18 o'clock where they fall in the view
      const ticks = [0, 6, 12, 18].map((h) => ({ h, at: ((h * 4 - shift) % 96 + 96) % 96 })).filter((x) => daySwitch || x.at < 96)
        .map((x) => `<span style="left:${(x.at / 96 * 100).toFixed(1)}%">${String(x.h).padStart(2, "0")}</span>`).join("");
      return `<div class="tp tp-price">${name('div', 'class="tp-name"')}<div class="tp-price-head"><b>${price}</b><small>ct/kWh</small>${days}</div>
        <div class="tp-price-slot">${interval}</div>
        <div class="tp-price-bars${widget.price_line === true ? " line" : ""}">${band}${bars}</div><div class="tp-price-foot">${ticks}</div>${foot}</div>${handle}`;
    }
    case "stock_quotes": return stockPreviewMarkup(widget, name, h) + handle;
    case "parqet_portfolio": {
      const number = (id) => {
        const raw = Number(String(editor.states.get(id) ?? "").replace(",", "."));
        return id && Number.isFinite(raw) && String(editor.states.get(id) ?? "") !== "" ? raw : null;
      };
      const locale = t("preview.parqet.locale");
      const fmt = (v, digits = 0, plus = false) => (plus && v > 0 ? "+" : "") + v.toLocaleString(locale, { minimumFractionDigits: digits, maximumFractionDigits: digits });
      const value = number(widget.entity_id) ?? 52841;
      const gain = widget.secondary_entity_id ? (number(widget.secondary_entity_id) ?? 8425) : null;
      const xirr = widget.tertiary_entity_id ? (number(widget.tertiary_entity_id) ?? 9.8) : null;
      const dividends = widget.dividends_entity_id ? (number(widget.dividends_entity_id) ?? 1820) : null;
      const hide = widget.parqet_hide_amounts === true;
      const pct = widget.return_entity_id ? (number(widget.return_entity_id) ?? 27.7) : null;
      const sign = (v) => (v > 0 ? "up" : v < 0 ? "down" : "");
      // The panel's layout (pq_layout in w_parqet_portfolio.c), with its line heights in tile pixels: the same rows appear
      // or drop out as the tile gets smaller. MAX uses the sensors; the other intervals show a sample of the integration's answer.
      const intervalCode = ["1d", "1w", "1m", "ytd"].includes(widget.parqet_interval) ? widget.parqet_interval : "max";
      const ws = intervalCode !== "max";
      const selector = widget.parqet_selector !== false;
      const holdingsView = widget.parqet_view === "holdings";
      const heightPx = Number(widget.rect?.h) || 0;
      const limit = heightPx - 6;
      const chipsOn = selector && heightPx >= 200;
      const tabsOn = selector && heightPx >= 260;
      let y = 40;
      if (tabsOn) y += 34;
      if (chipsOn) y += 34;
      const chipEm = (label, on) => `<em class="${previewControlClass(on)}">${label}</em>`;
      const tabsRow = tabsOn
        ? `<div class="tp-pq-chips">${chipEm(escapeHtml(t("layout.option.parqet_view.performance")), !holdingsView)}${chipEm(escapeHtml(t("layout.option.parqet_view.holdings")), holdingsView)}</div>` : "";
      const chipsRow = chipsOn
        ? `<div class="tp-pq-chips">${["1d", "1w", "1m", "ytd", "max"].map((code) => chipEm(code.toUpperCase(), code === intervalCode)).join("")}</div>` : "";
      const sortsOn = tabsOn && holdingsView; // the choice of holdings, like the panel's sort chips
      const sortRow = sortsOn
        ? `<div class="tp-pq-chips">${chipEm(escapeHtml(t("preview.parqet.largest")), true)}${chipEm("Top 5", false)}${chipEm("Flop 5", false)}</div>` : "";
      if (sortsOn) y += 34;
      const intervalText = chipsOn ? "" : `<small class="tp-pq-interval">${intervalCode === "max" ? "Max" : intervalCode.toUpperCase()}</small>`;
      const sample = [["Alpha Utilities", 6.0, 12.4], ["Beta Global Dividend ETF", 5.5, 8.1], ["Gamma Telecom", 4.8, -2.3],
        ["Delta Consumer Holdings", 4.4, 5.6], ["Epsilon Industrial", 4.0, 3.0]];
      const rowsHtml = (startY) => {
        let rows = Math.floor((limit - startY) / 38);
        rows = Math.min(rows, sample.length);
        if (rows < 2) return "";
        const top = Math.max(...sample.slice(0, rows).map((h) => h[1]));
        return sample.slice(0, rows).map(([n, wgt, ret]) => {
          const right = ws ? `<small class="${sign(ret)}">${fmt(ret, 1, true)} %</small>` : `<small>${fmt(wgt, 1)} %</small>`;
          return `<div class="tp-pq-row"><span>${n}</span>${right}<i style="width:${Math.max(2, Math.round((wgt / top) * 100))}%"></i></div>`;
        }).join("");
      };
      let body = "";
      if (holdingsView) {
        body = rowsHtml(y);
      } else {
        const wsValue = number(widget.entity_id) ?? 52841;
        const wsGain = 1240, wsPct = 2.4;
        let hero, gainText = "", gainSign = 0, kpiItems = [];
        const kpi = (label, text, cls) => `<span><small>${escapeHtml(label)}</small><em class="${cls || ""}">${text}</em></span>`;
        if (ws) {
          if (hide) { hero = `<b class="${sign(wsPct)}">${fmt(wsPct, 1, true)} %</b>`; }
          else {
            hero = `<b>${fmt(wsValue)} €</b>`;
            gainText = `${fmt(wsGain, 0, true)} €  ${fmt(wsPct, 1, true)} %`; gainSign = wsGain;
          }
          kpiItems.push(kpi(t("preview.parqet.xirr"), `${fmt(9.8, 1, true)} %`, "up"), kpi(t("preview.parqet.ttwror"), `${fmt(8.1, 1, true)} %`, "up"));
          if (!hide) kpiItems.push(kpi(t("preview.parqet.dividends"), `${fmt(410)} €`), kpi(t("preview.parqet.realized"), `${fmt(120, 0, true)} €`, "up"));
        } else {
          if (hide) hero = pct !== null ? `<b class="${sign(pct)}">${fmt(pct, 1, true)} %</b>` : xirr !== null ? `<b class="${sign(xirr)}">${fmt(xirr, 1, true)} %</b>` : "<b>•••</b>";
          else {
            hero = `<b>${fmt(value)} €</b>`;
            if (gain !== null || pct !== null) gainText = `${gain !== null ? `${fmt(gain, 0, true)} €` : ""}${gain !== null && pct !== null ? "  " : ""}${pct !== null ? `${fmt(pct, 1, true)} %` : ""}`;
            gainSign = gain !== null ? gain : pct;
          }
          if (xirr !== null) kpiItems.push(kpi(t("preview.parqet.xirr"), `${fmt(xirr, 1, true)} %`, sign(xirr)));
          if (dividends !== null && !hide) kpiItems.push(kpi(t("preview.parqet.dividends"), `${fmt(dividends)} €`));
        }
        y += 52;
        let gainHtml = "";
        if (gainText && y + 28 <= limit) { gainHtml = `<div class="tp-pq-gain ${sign(gainSign)}">${gainText}</div>`; y += 34; }
        const kpiRows = [];
        for (let i = 0; i < kpiItems.length; i += 2) {
          if (y + 56 > limit) break;
          kpiRows.push(`<div class="tp-pq-kpis">${kpiItems.slice(i, i + 2).join("")}</div>`);
          y += 60;
        }
        let tail = "";
        if (ws) {
          if (limit - y >= 28) tail = `<svg class="tp-pq-spark" viewBox="0 0 100 30" preserveAspectRatio="none"><polyline points="0,26 6,20 10,22 16,17 22,18 28,21 34,16 40,14 46,15 52,11 58,12 64,9 70,12 76,8 82,9 88,5 94,6 100,4" /></svg>`;
        } else {
          tail = rowsHtml(y);
        }
        body = `<div class="tp-pq-head">${hero}</div>${gainHtml}${kpiRows.join("")}${tail}`;
      }
      return `<div class="tp tp-pq">${name('div', 'class="tp-name"')}${intervalText}${tabsRow}${chipsRow}${sortRow}${body}</div>${handle}`;
    }
    case "select_tile":
      return `<div class="tp tp-select${h >= 110 ? " tp-select-tall" : ""}"><div class="tp-blind-text">${name('small', '')}<b>${previewEntityValue(widget.entity_id, "pv")}</b></div><span class="tp-select-chevron">\u2304</span></div>${handle}`;
    case "datetime_tile": {
      return `<div class="tp tp-datetime">${name('small', '')}<b>${previewEntityValue(widget.entity_id, "06:30", previewDateTimeValue)}</b></div>${handle}`;
    }
    case "scene_tile":
      return `<div class="tp tp-contact"><div class="tp-icon-circle ${previewControlClass(false, previewControlAvailable(widget.entity_id, true))}" style="${previewCircleSize(w, h)}">\u25B6</div><div class="tp-blind-text">${name('b', '')}<small>${pvText(previewSceneKind(widget.entity_id))}</small></div></div>${handle}`;
    case "binary_tile":
      if (widget.display_mode === "door") {
        const open = previewEntityState(widget.entity_id) !== "off";
        return `<div class="tp tp-contact"><div class="tp-door${open ? "" : " closed"}"><i></i></div><div class="tp-blind-text">${name('b', '')}<small class="${open ? "tp-amber" : "tp-green"}">${pvText(open ? "contact_open" : "contact_closed")}</small></div></div>${handle}`;
      }
      {
        // The state in the words of the device class (w_binary.c); "since" adds the age, "icon" shows the symbol only, an
        // event its last time. The presence sample remains for a tile without a state yet.
        const raw = previewEntityState(widget.entity_id);
        const info = entityDisplayInfo(widget.entity_id);
        let text = pvText("home");
        if (String(widget.entity_id || "").startsWith("event.")) text = escapeHtml(previewSensorTimestamp(raw, "relative") || raw || "--");
        else if (raw === "on" || raw === "off") text = escapeHtml(previewBinaryStateText(info.device_class || "", raw === "on"));
        if (widget.display_mode === "since" && raw) text += ` \u00B7 ${escapeHtml(t("preview.sensor.just_now"))}`;
        const stateLine = widget.display_mode === "icon" ? "" : `<small class="tp-blue">${text}</small>`;
        return `<div class="tp tp-contact"><div class="tp-icon-dot"></div><div class="tp-blind-text">${name('b', '')}${stateLine}</div></div>${handle}`;
      }
    case "room_tile": {
      // Ring with the room temperature (only on a card big enough to hold the reading in it, like w_room.c), the extra
      // field beside it and the status chips below; the
      // chips follow the states of the card's entities, the samples apply to a card without any.
      const extra = ROOM_EXTRA_MODES.includes(widget.room_extra_mode) ? widget.room_extra_mode : "default";
      const summary = roomPreviewSummary(widget.entity_id, widget.room_entities);
      const known = summary.temp !== null || summary.lights || summary.contacts || summary.presence;
      const controls = widget.room_controls === true ? roomPreviewControls(summary) : "";
      const chips = known ? roomPreviewChips(summary, w, !!controls)
        : `<i class="c-green">${pvText("room_closed")}</i><i class="c-amber">${pvText("room_light_on")}</i><i class="c-teal">${pvText("room_present")}</i>`;
      const temp = escapeHtml(summary.temp ?? (widget.entity_id ? "--" : "21.8\u00B0C"));
      const ring = roomPreviewRing(widget, w, h);
      // A cover or media action (w_room.c): beside a ring with 130 px to its right the status word and the buttons (a cover
      // gets up and down when the word still fits), otherwise one button with icon and word in the bottom right corner.
      const action = extra === "cover" || extra === "media_player";
      const rightW = w - 28 - ring - 10;
      const expanded = action && ring >= 56 && rightW >= 130;
      const compact = action && !expanded && h >= 100 && w >= 200;
      const extraState = previewEntityState(widget.room_extra_entity_id);
      const extraActive = extra === "media_player" ? extraState === "playing" : ["opening", "closing"].includes(extraState);
      const primary = previewRoomAction(widget);
      const extraRole = primary.role;
      const word = extra === "cover" ? pvText("room_open") : extraState === "paused" ? escapeHtml(t("layout.preview.room_paused")) : pvText("room_playing");
      const buttons = extra === "cover" && rightW - 2 * 44 - 16 >= 56 ? [previewRoomAction(widget, "up"), previewRoomAction(widget, "down")] : [primary];
      const side = extra === "humidity" ? `<b class="tp-room-side${ring >= 90 ? " big" : ""}">${escapeHtml(summary.hum ?? "45 %")}</b>`
        : expanded ? `<span class="tp-room-extra">${word}</span>${buttons.map((b) => `<span class="tp-room-btn ${b.role}">${b.symbol}</span>`).join("")}`
          : !expanded && summary.hum !== null ? `<b class="tp-room-side tp-room-hum">${escapeHtml(summary.hum)}</b>` : "";
      // The corner button is icon, word and padding wide (at most 52 % of the card); the chips keep clear of it.
      const cornerW = compact ? Math.min(Math.max(44, 8 + 24 + 4 + previewTextWidth(word, textRoleSize("meta", 20)) + 8), (w - 28) * 0.52) : 0;
      const corner = compact ? `<span class="tp-room-action ${extraRole}" style="width:${Math.round(cornerW)}px"><i>${primary.symbol}</i>${word}</span>` : "";
      return `<div class="tp tp-room${compact ? " tp-room-cornered" : ""}" style="--corner:${Math.round(cornerW)}px">${corner}${name('div', 'class="tp-name" style="text-align:left"')}<div class="tp-room-main">${ring ? `<i class="tp-room-ring" style="width:${ring}px;height:${ring}px"><b>${temp}</b></i>` : `<b class="tp-room-temp">${temp}</b>`}${side}</div>
        ${controls ? `<div class="tp-room-chips tp-room-controls">${controls}</div>` : ""}<div class="tp-room-chips">${chips}</div></div>${handle}`;
    }
    case "climate_tile":
      return `<div class="tp tp-climate">${name('div', 'class="tp-name" style="text-align:left"')}<small>${pvText("climate_now", { temp: "23.0\u00B0" })}</small>
        <div class="tp-clim-mid" style="--btn:${h >= 220 ? 64 : 48}px"><span>\u2212</span><b>22.0\u00B0</b><span>+</span></div>
        <div class="tp-clim-modes"><i>${pvText("climate_heat")}</i></div><div class="tp-clim-chips"><i>${pvText("climate_fan_auto")}</i><i>${pvText("climate_swing_off")}</i></div></div>${handle}`;
    case "light_tile":
      // Like the panel: state and brightness on top, colour wheel, the lamp, the name and the brightness slider; a light that
      // is off reads OFF and 0 % with a grey lamp (the brightness itself is not known to the editor).
      {
        const off = previewEntityState(widget.entity_id) === "off";
        const pct = off ? 0 : 70;
        return `<div class="tp tp-light${off ? " tp-light-off" : ""}"><div class="tp-light-top"><span>${escapeHtml(off ? t("layout.preview.off", {}, "OFF") : t("layout.preview.on", {}, "ON"))}</span><span class="tp-light-pct">${pct} %<i class="tp-wheel"></i></span></div><svg class="tp-bulb-svg" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 21h6M10 18.5h4M12 2.8a6.2 6.2 0 0 0-3.6 11.2c.3.3.6.7.6 1.2v1.3h6v-1.3c0-.5.3-.9.6-1.2A6.2 6.2 0 0 0 12 2.8z" fill="none" stroke="${off ? "#8a9aa8" : "#ff9d3d"}" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>${name('b', '')}<div class="tp-bar tp-bar-knob"><i style="width:${pct}%"></i><u style="left:${pct}%"></u></div><div class="tp-switch tp-matte-switch"><i></i></div></div>${handle}`;
      }
    case "heating_tile": {
      // The default style shows target, current temperature and the action under the ring (like w_heating_tile.c);
      // only the semi arc has the plus and minus buttons.
      const semi = widget.style_variant === "arc_semi";
      const opening = ["left", "right", "top", "bottom"].includes(widget.arc_opening) ? widget.arc_opening : "left";
      // The name with the power symbol on top, the ring below (w_heating_tile.c): target, reading and action inside the arc;
      // the semi arc has its scale ends and the plus and minus buttons on the open side.
      const head = `<div class="tp-heat-head"><i>\u23FB</i>${name('span', '')}</div>`;
      if (semi) return `<div class="tp tp-heat tp-heat-semi open-${opening}">${head}${heatingSemiPreview(widget, w, h, opening)}</div>${handle}`;
      return `<div class="tp tp-heat tp-heat-arc">${head}${heatingArcPreview(widget, w, h)}</div>${handle}`;
    }
    case "button": {
      // A media mode shows its 48 px transport symbol over the name instead of the switch (w_button.c).
      const symbols = { play_pause: previewEntityState(widget.entity_id) === "playing" ? "\u23F8" : "\u25B6", stop: "\u25A0", previous: "\u23EE", next: "\u23ED" };
      if (symbols[widget.button_mode]) return `<div class="tp tp-button tp-button-icon" style="${previewControlAccentStyle(widget)}"><i class="${previewControlClass(widget.button_mode === "play_pause" && previewEntityState(widget.entity_id) === "playing", previewControlAvailable(widget.entity_id))}">${symbols[widget.button_mode]}</i>${name('b', '')}</div>${handle}`;
      // The state line needs room between it and the name above the switch (w_button.c): about 150 px of height.
      const on = ["on", "open", "playing", "home"].includes(previewEntityState(widget.entity_id));
      return `<div class="tp tp-button" style="${previewControlAccentStyle(widget)}">${h >= 150 ? `<small class="tp-button-state">${escapeHtml(t(on ? "layout.preview.on" : "layout.preview.off"))}</small>` : ""}<div class="tp-switch tp-switch-big ${on ? "" : "off "}${previewControlClass(on, previewControlAvailable(widget.entity_id))}"><i></i></div>${name('b', '')}</div>${handle}`;
    }
    case "slider": {
      // Vertical when the direction says so (blinds), horizontal otherwise; like the panel it shows state and value.
      const dir = widget.slider_direction || "auto";
      const vertical = dir === "bottom_to_top" || dir === "top_to_bottom" || (dir === "auto" && h > w);
      const isCover = String(widget.entity_id || "").startsWith("cover.");
      const state = escapeHtml(isCover ? t("layout.preview.open", {}, "OPEN") : t("layout.preview.on", {}, "ON"));
      const fromTop = dir === "top_to_bottom";
      // An entity that is off (light, media player) stands at 0 %, as on the panel; otherwise the 60 % sample.
      const sliderPct = ["off", "unavailable"].includes(previewEntityState(widget.entity_id)) ? 0 : 60;
      if (vertical) {
        return `<div class="tp tp-slider tp-slider-vertical2"><div class="tp-vbar2"><i style="height:${sliderPct}%;${fromTop ? "top:0" : "bottom:0"}"></i><span>${sliderPct} %</span></div>${name('b', '')}</div>${handle}`;
      }
      return `<div class="tp tp-slider tp-slider-h2${isCover ? " tp-slider-cover" : ""}"><div class="tp-hbar2"><i style="width:${sliderPct}%"></i><span>${sliderPct} %</span></div>${name('b', '')}</div>${handle}`;
    }
    case "sensor": {
      if (widget.sensor_view === "list") return sensorListPreview(widget, name) + handle;
      // The panel shows the value in its largest font when it is all the tile shows.
      const showLabel = widgetShowsName(widget);
      const valueOnly = !showLabel && widget.sensor_show_age === false;
      const value = previewSensorValue(widget) ?? previewEntityValue(widget.entity_id, "23.4 \u00B0C");
      const age = widget.sensor_show_age !== false ? `<small class="tp-sensor-age">${escapeHtml(t("preview.sensor.just_now"))}</small>` : "";
      // On the 10-inch boards the reading follows the tile's shorter side (sensor_pick_value_font()): META below 140 px,
      // VALUE below 190 px, KEY above; Panel4/Tab5 fit KEY to the tile.
      const minDim = Math.min(w, h);
      const fixedPx = !previewTextAdaptive() && !valueOnly
        ? textRoleSize(minDim < 140 ? "meta" : minDim < 190 ? "value" : "key", 28) : 0;
      return `<div class="tp tp-sensor${valueOnly ? " tp-sensor-value-only" : ""}">${showLabel ? `${name('small', '')}` : ""}<b${fixedPx ? ` style="font-size:${fixedPx}px"` : ""}>${value}</b>${age}</div>${handle}`;
    }
    case "graph": {
      // Like the panel: the name and the current value on top, the chart below; the window sits after the value.
      const windowMin = Number(widget.graph_time_window_min) || 120;
      const window = windowMin % 60 === 0 ? `${windowMin / 60}h` : `${windowMin} min`;
      // The line under the value: the window's lowest and highest reading and the window (w_graph.c), from the shown value.
      const graphValue = previewEntityValue(widget.entity_id, "2450 W");
      const reading = /^(-?[\d.,]+)\s*(.*)$/.exec(String(graphValue).replace(/<[^>]*>/g, ""));
      const number = reading ? Number(reading[1].replace(",", ".")) : NaN;
      const digits = reading && /[.,]/.test(reading[1]) ? reading[1].split(/[.,]/)[1].length : 0;
      const range = Number.isFinite(number)
        ? `min ${previewLocalizeNumber((number * 0.77).toFixed(digits))} ${reading[2]}   max ${previewLocalizeNumber((number * 1.15).toFixed(digits))} ${reading[2]} | ` : "";
      return `<div class="tp tp-graph"><div class="tp-graph-head">${name('small', '')}<b>${graphValue}</b></div><small class="tp-graph-window">${escapeHtml(range)}${window}</small>${previewGraphMarkup(widget.graph_display_mode)}</div>${handle}`;
    }
    case "gauge": {
      const gaugeValue = previewEntityValue(widget.entity_id, "6.7 kW");
      const segmented = widget.gauge_bar_style === "segmented";
      // The bar follows the live value within the configured range; the sample fills 60 %.
      const gaugeMin = Number.isFinite(widget.gauge_min) ? widget.gauge_min : 0;
      const gaugeMax = Number.isFinite(widget.gauge_max) && widget.gauge_max > gaugeMin ? widget.gauge_max : gaugeMin + 100;
      const live = carPreviewNumber(widget.entity_id, null);
      const fill = live === null ? 0.6 : clamp((live - gaugeMin) / (gaugeMax - gaugeMin), 0, 1);
      const segments = (fallback) => { const count = Math.min(Number(widget.gauge_segment_count) || fallback, 30); return [count, Math.round(count * fill)]; };
      if (widget.style_variant === "radial") {
        // gauge_apply_layout_radial(): the ring takes the card (10 px padding), the name in its open segment or, on a narrow
        // tile, below it. The reading is KEY fitted to the ring on Panel4/Tab5, VALUE (META below 140 px) on the 10-inch boards.
        const role = (key) => textRoleSize(key, PREVIEW_ROLE_FALLBACK[key]);
        const cw = Math.max(1, w - 20);
        const ch = Math.max(1, h - 20);
        const arc = Math.min(cw, ch);
        const r = arc / 2;
        const metaLine = previewLineHeight(role("meta"));
        const below = widgetShowsName(widget) && arc < 130 && ch >= arc + metaLine + 4;
        const valuePx = previewTextAdaptive() ? Math.min(role("key"), Math.floor(arc / 4.5)) : role(arc >= 140 ? "value" : "meta");
        const titlePx = below ? role("meta") : role("label");
        // The reading and the extra readouts (META, one line each) as one block centred in the ring; the name below them.
        const infos = [widget.secondary_entity_id, widget.tertiary_entity_id].filter(Boolean);
        const valueLine = previewLineHeight(valuePx);
        const valueY = r - (valueLine + infos.length * metaLine) / 2;
        const infoBottom = valueY + valueLine + infos.length * metaLine;
        let titleTop = below ? arc + 4 : r + (r * 37) / 50 - previewLineHeight(titlePx) / 2;
        if (!below && infos.length && titleTop < infoBottom) titleTop = infoBottom;
        if (!below) titleTop = Math.min(titleTop, ch - previewLineHeight(titlePx));
        const left = 10 + (cw - arc) / 2;
        const centred = (top, px, html, cls = "") => `<span class="tp-arc-text${cls}" style="top:${10 + Math.round(top)}px;left:${left + 8}px;width:${arc - 16}px;font-size:${px}px">${html}</span>`;
        return `<div class="tp tp-gauge-radial" style="--arc:${arc}px"><div class="tp-arc" style="width:${arc}px;left:${left}px"></div>${centred(valueY, valuePx, `<b>${gaugeValue}</b>`)}${infos.map((id, i) => centred(valueY + valueLine + i * metaLine, role("meta"), previewGaugeInfo(id), " tp-arc-info")).join("")}${name('small', `style="top:${10 + Math.round(titleTop)}px;font-size:${titlePx}px;width:${below ? cw : Math.round((r * 13) / 10)}px"`)}</div>${handle}`;
      }
      if (widget.style_variant === "horizontal") {
        // gauge_apply_layout_horizontal(): on Panel4/Tab5 a tile under 80 px content height puts the extra readings in the
        // top row beside the value; otherwise they get a row under the bar when it has room.
        const infos = [widget.secondary_entity_id, widget.tertiary_entity_id].filter(Boolean).map((id) => `<span>${previewGaugeInfo(id)}</span>`);
        const compact = previewTextAdaptive() && h - 20 < 80 && infos.length > 0;
        const rowH = previewLineHeight(textRoleSize(previewTextAdaptive() ? "label" : "meta", 24));
        const infoH = previewLineHeight(textRoleSize("value", 28));
        const gap = compact ? 4 : 8;
        const areaTop = rowH + gap;
        let areaH = Math.max(6, h - 20 - areaTop);
        const infoRow = !compact && infos.length > 0 && areaH >= 14 + 6 + infoH;
        const extras = infoRow ? infos.join("") : "";
        // The bar: half the free area (14-40 px), centred in it, above the extras row (VALUE) at the card's bottom.
        if (infoRow) areaH -= infoH + 6;
        const barH = Math.min(40, areaH / 2 < 14 ? Math.min(areaH, 14) : areaH / 2);
        const barStyle = `style="top:${Math.round(10 + areaTop + (areaH - barH) / 2)}px;height:${Math.round(barH)}px"`;
        const bar = segmented ? previewSegments(...segments(20), "h").replace('class="tp-segs h"', `class="tp-segs h" ${barStyle}`)
          : `<div class="tp-bar tall" ${barStyle}><i style="width:${Math.round(fill * 100)}%"></i></div>`;
        return `<div class="tp tp-gauge-h${compact ? " tp-gauge-compact" : ""}"><div class="tp-light-top">${name('span', '')}${compact ? infos.map((i) => i.replace("<span>", '<span class="tp-gauge-info">')).join("") : ""}<span>${gaugeValue}</span></div>${bar}${extras ? `<small class="tp-gauge-extras">${extras}</small>` : ""}</div>${handle}`;
      }
      // Vertical: the value on top, the bar, then the name (w_gauge.c).
      const bar = segmented ? previewSegments(...segments(10), "v")
        : `<div class="tp-vbar"><i style="height:${Math.round(fill * 100)}%;bottom:0"></i></div>`;
      // The unit goes to a second line when the reading does not fit the narrow bar in META (w_gauge.c, every board).
      const plain = String(gaugeValue).replace(/<[^>]*>/g, "");
      const split = previewTextWidth(plain, textRoleSize("meta", 20)) > w - 20 ? String(gaugeValue).replace(" ", "<br>") : gaugeValue;
      // ...and fit the reading from KEY down in 2 px steps to the board's floor (ui_text_fit()).
      let valuePx = textRoleSize("meta", 20);
      if (previewTextAdaptive()) {
        const longest = split === gaugeValue ? plain : plain.split(" ").reduce((a, b) => (b.length > a.length ? b : a), "");
        const floor = Number(editor.appText?.floor) || 16;
        for (valuePx = textRoleSize("key", 36); valuePx > floor && previewTextWidth(longest, valuePx) > w - 20; valuePx -= 2);
      }
      return `<div class="tp tp-gauge-v"><b class="tp-gauge-value" style="font-size:${valuePx}px">${split}</b>${bar}${name('small', '')}</div>${handle}`;
    }
    case "todo_list":
      return `<div class="tp tp-todo"><div class="tp-todo-head">${name('div', 'class="tp-name"')}<span class="tp-badge">3</span></div>${["Milk", "Bread", "Call"].map((t2, i) => `<div class="tp-row"><i class="${i === 1 ? "done" : ""}"></i>${t2}</div>`).join("")}</div>${handle}`;
    case "empty_tile":
      // A plain card whose only content is its optional name.
      return `<div class="tp tp-empty">${name('div', 'class="tp-name"')}</div>${handle}`;
    default:
      return null;
  }
}

function renderHouseCanvasPreview(page) {
  // The same grid as ui_house_page.c: columns from the content width (one per 230 px, 2 to 6), 10 px gaps, a 30 px
  // heading per floor and room cards of 150 px that shrink to 120 to fit; every card is a room tile preview.
  const rooms = page.house?.rooms || [];
  if (!rooms.length) {
    const hint = document.createElement("div");
    hint.style.cssText = "padding:14px;color:#cfd8e3;font-size:14px;";
    hint.textContent = t("house.empty");
    el.canvas.appendChild(hint);
    return;
  }
  const gap = 10;
  const headerH = 30;
  const cols = clamp(Math.floor(CANVAS_WIDTH / 230), 2, 6);
  const cellW = Math.floor((CANVAS_WIDTH - gap * (cols - 1) - 2 * gap) / cols);
  const floors = [];
  for (const room of rooms) {
    const floor = room.floor || "";
    if (!floors.includes(floor)) floors.push(floor);
  }
  const showHeaders = floors.length > 1 || (floors.length === 1 && floors[0] !== "");
  let cardRows = 0;
  for (const floor of floors) cardRows += Math.ceil(rooms.filter((r) => (r.floor || "") === floor).length / cols);
  const headers = showHeaders ? floors.length : 0;
  let cellH = 150;
  const availH = CANVAS_HEIGHT - 2 * gap;
  while (cellH > 120 && cardRows * cellH + (cardRows - 1) * gap + headers * headerH > availH) cellH -= 5;
  let y = gap;
  let index = 0;
  for (const floor of floors) {
    const inFloor = rooms.filter((r) => (r.floor || "") === floor);
    if (showHeaders) {
      const heading = document.createElement("div");
      heading.className = "house-floor";
      heading.textContent = floor || "-";
      heading.style.cssText = `left:${gap + 4}px;top:${y}px`;
      el.canvas.appendChild(heading);
      y += headerH;
    }
    inFloor.forEach((room, i) => {
      const col = i % cols;
      if (i > 0 && col === 0) y += cellH + gap;
      const widget = {
        type: "room_tile", id: `house_room_${index}`, title: room.name, entity_id: room.entity_id,
        room_entities: room.room_entities, room_extra_mode: room.room_extra_mode, room_extra_entity_id: room.room_extra_entity_id,
        rect: { x: gap + col * (cellW + gap), y, w: cellW, h: cellH },
      };
      index += 1;
      const box = document.createElement("div");
      box.className = "widget-box house-room matte-wide";
      box.style.cssText = `left:${widget.rect.x}px;top:${y}px;width:${cellW}px;height:${cellH}px`;
      box.innerHTML = tilePreviewMarkup(widget).replace('<div class="resize-handle"></div>', "");
      el.canvas.appendChild(box);
    });
    y += cellH + gap;
  }
}
