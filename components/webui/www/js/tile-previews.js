// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// The editor's schematic previews: the solar, car, Sonos and generic tile previews (tilePreviewMarkup) and the
// house page's grid, laid out like ui_house_page.c. Moved out of canvas.js; classic script, loaded before it.
// tools/compare_editor_previews.py puts them next to the simulator's renderings.

function solarPreviewValue(entityId) {
  const raw = editor.states.get(entityId) ?? "";
  const parsed = Number(String(raw).replace(",", "."));
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : null;
}

function solarPreviewFormatValue(value, includeUnit = true) {
  if (value === null) return "--";
  const formatted = value >= 100 ? value.toFixed(0) : value.toFixed(1);
  return includeUnit ? `${formatted} kWh` : formatted;
}

function solarPreviewRows(widget) {
  const rows = [
    { label: "Rest", entityId: widget.entity_id },
    { label: "Heute", entityId: widget.forecast_today_entity_id },
    { label: "Morgen", entityId: widget.forecast_tomorrow_entity_id },
    { label: "Tag 3", entityId: widget.forecast_day_3_entity_id },
    { label: "Tag 4", entityId: widget.forecast_day_4_entity_id },
    { label: "Tag 5", entityId: widget.forecast_day_5_entity_id },
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
  const fitting = Math.max(2, Math.min(5, Math.floor(((Number(widget.rect && widget.rect.h) || 310) - 60) / 38)));
  // The remaining reading shares today's row: the part of today's total that is still to come stays bright, the rest is dimmed.
  const rest = allRows[0].value;
  const today = allRows[1].value;
  const days = allRows.slice(1);
  const rows = widget.align_forecast ? days.slice(0, forecastPreviewCount(widget, 5)) : orientation === "vertical" ? days : days.slice(0, fitting);
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
      <div class="solar-preview-unit">${widget.align_forecast ? `Rest ${escapeHtml(solarPreviewFormatValue(allRows[0].value))}` : "kWh"}</div>
      <div class="solar-preview-body">${rowMarkup}</div>
    </div>
    <div class="resize-handle"></div>
  `;
}

function carPreviewNumber(entityId, fallback = null) {
  const raw = editor.states.get(entityId) ?? "";
  const parsed = Number(String(raw).replace(",", "."));
  return Number.isFinite(parsed) ? parsed : fallback;
}

// The optional bottom row of the car tile (only drawn from 330 px height, like the panel).
function carPreviewExtrasMarkup(widget) {
  const h = Number(widget.rect && widget.rect.h) || 0;
  if (h < 330) return "";
  const cells = [];
  if (widget.lock_entity_id) cells.push(`<span class="car-x-ok">${pvText("car_locked")}</span>`);
  if (widget.interior_temp_entity_id) cells.push("<span>21.5\u00B0C</span>");
  if (widget.precond_entity_id) cells.push(`<span class="car-x-btn">${pvText("car_precond")}</span>`);
  return cells.length ? `<div class="car-preview-extras">${cells.join("")}</div>` : "";
}

function carPreviewMarkup(widget) {
  const soc = clamp(Math.round(carPreviewNumber(widget.entity_id, 27)), 0, 100);
  const range = carPreviewNumber(widget.tertiary_entity_id, null);
  const power = carPreviewNumber(widget.secondary_entity_id, null);
  const rangeText = range === null ? "-- km" : `${range.toFixed(range >= 100 ? 0 : 1)} km`;
  const powerText = power === null ? "-- W" : (Math.abs(power) >= 10000 ? `${(power / 1000).toFixed(1)} kW` : `${power.toFixed(0)} W`);
  return `
    <div class="car-preview" style="--soc:${soc};">
      <div class="car-preview-gauge">
        <div class="car-preview-range">${escapeHtml(rangeText)}</div>
        <div class="car-preview-soc">${escapeHtml(String(soc))}<span>%</span></div>
        <div class="car-preview-power">${escapeHtml(powerText)}</div>
      </div>
      ${carPreviewExtrasMarkup(widget)}
      ${widgetNamePreview(widget, "div", 'class="car-preview-title"')}
    </div>
    <div class="resize-handle"></div>
  `;
}

function sonosFavoritesPreviewMarkup(widget) {
  const title = widget.title || "Sonos Favorites";
  const player = widget.entity_id || "media_player.*";
  const sensor = widget.secondary_entity_id || DEFAULT_SONOS_FAVORITES_SENSOR;
  const sampleRows = ["Absolut TOP", "SWR3", "Yoga"];
  return `
    <div class="sonos-preview">
      ${widgetNamePreview(widget, "div", 'class="sonos-preview-title"')}
      <div class="sonos-preview-meta">${escapeHtml(player)}</div>
      <div class="sonos-preview-list">
        ${sampleRows.map((name) => `<div>\u25B6 ${escapeHtml(name)}</div>`).join("")}
      </div>
      <div class="w-hint">${escapeHtml(sensor)}</div>
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
  const unit = editor.entities.find((entity) => entity?.id === entityId)?.unit || "";
  const raw = editor.states.get(entityId);
  const known = raw !== undefined && raw !== null && raw !== "" && !["unavailable", "unknown"].includes(String(raw));
  return escapeHtml(`${known ? format(String(raw)) : "--"}${unit ? ` ${unit}` : ""}`);
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
  const info = (id) => editor.entities.find((e) => e?.id === id) || editor.stateMeta?.get(id) || {};
  const state = (id) => previewEntityState(id);
  const out = { temp: null, hum: null, contacts: 0, open: 0, lights: 0, lightsOn: 0, presence: 0, present: 0, battery: 0 };
  for (const id of [entityId, ...(Array.isArray(extraIds) ? extraIds : [])]) {
    if (!id) continue;
    const dc = info(id).device_class || "";
    const st = state(id);
    const on = st === "on" || st === "open";
    const domain = String(id).split(".")[0];
    if (domain === "light") {
      out.lights += 1;
      if (on) out.lightsOn += 1;
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

function roomPreviewChips(summary, cardWidth) {
  const chips = [];
  if (summary.contacts > 0) chips.push(["contacts", summary.open ? "c-amber" : "c-green", summary.open ? t("layout.preview.room_open_n", { n: summary.open }) : t("layout.preview.room_closed")]);
  if (summary.lights > 0) {
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

// Weekday names in the editor language, from the day after today.
function previewWeekday(offset) {
  const date = new Date(2026, 9, 5 + offset);
  try {
    return date.toLocaleDateString(editor.i18nLanguage || "en", { weekday: "short" }).replace(/\.$/, "");
  } catch (_) {
    return ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"][offset % 7];
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
      const days = [t("preview.weather.today"), ...[1, 2, 3, 4, 5].map(previewWeekday)];
      const rain = widget.show_precipitation !== false && h >= 200;
      const rainValues = [["5 %", "0 mm"], ["20 %", "0.2 mm"], ["80 %", "6.5 mm"], ["40 %", "1.4 mm"], ["10 %", "0 mm"], ["0 %", "0 mm"]];
      const conditions = [editor.states.get(widget.entity_id) || "partlycloudy", "sunny", "rainy", "sunny", "partlycloudy", "sunny"];
      const cards = days.slice(0, cols).map((d, i) =>
        `<div class="tp-day"><span>${d}</span><i>${previewWeatherIcon(conditions[i])}</i><b>${22 - i}°</b><small>${11 + i}°</small>${rain ? `<em class="tp-rain">${rainValues[i][0]}<br>${rainValues[i][1]}</em>` : ""}</div>`).join("");
      return `<div class="tp tp-weather3">${name('div', 'class="tp-name"')}
        <div class="tp-wx-head"><i>${current.icon}</i><div><b>21.5 °C</b><small>${current.text}</small></div></div>
        <div class="tp-days" style="--cols:${cols}">${cards}</div></div>${handle}`;
    }
    case "weather_tile": {
      const current = previewWeatherCondition(widget);
      const extras = h >= 200 ? `<div class="tp-weather-extras"><small>${escapeHtml(t("preview.weather.low"))} 12°C</small><small>0.8 mm</small></div><small class="tp-weather-hum">${escapeHtml(t("preview.weather.humidity"))} 38 %</small>` : "";
      return `<div class="tp tp-weather${h < 222 ? " tp-weather-short" : ""}">${name('div', 'class="tp-name"')}
        <div class="tp-weather-main"><i class="tp-big">${current.icon}</i><b>21.5 °C</b></div><small>${current.text}</small>${extras}</div>${handle}`;
    }
    case "media_player": {
      // Like the panel (w_media_player.c): the cover takes what is left after the text, progress, controls and volume
      // rows (about 226 px plus the name line), so a tile below ~330 px shows none; shuffle and repeat need ~280 px.
      const showCover = h - 28 - (widgetShowsName(widget) ? 22 : 0) - 226 >= 72;
      const extras = w >= 280;
      const showArtist = showCover || h >= 234;
      return `<div class="tp tp-media${showCover ? "" : " tp-media-nocover"}">${name('div', 'class="tp-name"')}
        ${showCover ? '<div class="tp-cover"></div>' : ""}<div class="tp-track"><b>Song title</b>${showArtist ? "<small>Artist</small>" : ""}</div>
        <div class="tp-bar"><i style="width:35%"></i></div>
        <div class="tp-btns">${extras ? "<span>\u21C4</span>" : ""}<span>\u23EE</span><span class="tp-primary">\u23F5</span><span>\u23ED</span>${extras ? "<span>\u21BB</span>" : ""}</div>
        <div class="tp-vol"><span>\u2212</span><div class="tp-bar"><i style="width:55%"></i></div><span>+</span></div></div>${handle}`;
    }
    case "vacuum_tile": {
      const state = previewEntityState(widget.entity_id);
      const cleaning = ["cleaning", "returning", "mopping"].includes(state);
      const wide = w >= h * 1.8;
      const robot = h >= 260 || wide;
      return `<div class="tp tp-vac${wide ? " tp-vac-wide" : ""}">${robot ? '<div class="tp-robot"><i></i></div>' : ""}<div class="tp-vac-body">${name('div', 'class="tp-name"')}<span class="tp-vac-battery">82 %</span>
        <small>${state ? escapeHtml(state) : pvText("vacuum_docked")}</small><div class="tp-chip">Mode \u25BE</div>
        <div class="tp-btns"><span class="tp-primary">${cleaning ? "\u23F8" : "\u23F5"}</span><span>\u23F9</span><span>\u2302</span>${wide ? "" : "<span>\u25CE</span>"}</div></div></div>${handle}`;
    }
    case "cover_tile":
      return `<div class="tp tp-blind"><div class="tp-blind-text">${name('b', '')}<small>${pvText("open_pct", { pct: 70 })}</small><div class="tp-bar"><i style="width:70%"></i></div></div>
        <div class="tp-btns"><span>\u2191</span><span>\u25A0</span><span>\u2193</span></div></div>${handle}`;
    case "fan_tile":
      if (h >= 220 && w >= 200) {
        return `<div class="tp tp-fanbig"><div class="tp-fanbig-top">${name('b', '')}<small>${pvText("fan_on", { pct: 60 })} auto</small><span class="tp-fan-power">\u23FB</span></div><div class="tp-rotor"><svg viewBox="0 0 100 100" aria-hidden="true">
          <circle class="tp-fan-guard" cx="50" cy="50" r="46"/>
          <g class="tp-fan-blades">${[0, 120, 240].map(angle => `<path transform="rotate(${angle} 50 50)" d="M47 46 C34 39 30 25 38 16 C43 10 57 12 66 18 C75 26 67 40 53 47 Z"/>`).join("")}</g>
          <circle class="tp-fan-hub" cx="50" cy="50" r="8"/></svg></div>
          <div class="tp-chips"><i>1</i><i>2</i><i class="on">3</i><i>4</i><i>5</i></div><div class="tp-chips"><i class="on">auto</i><i>sleep</i><i>breeze</i></div></div>${handle}`;
      }
      return `<div class="tp tp-blind"><div class="tp-blind-text">${name('b', '')}<small>${pvText("fan_on", { pct: 60 })}</small><div class="tp-bar"><i style="width:60%"></i></div></div>
        <div class="tp-btns"><span>\u2212</span><span class="tp-primary">\u23FB</span><span>+</span></div></div>${handle}`;
    case "camera_tile":
      return `<div class="tp tp-camera"><div class="tp-cam-img">${widget.camera_live ? escapeHtml(t("layout.preview.camera_connecting")) : "\uD83D\uDCF7"}</div>${name('div', 'class="tp-cam-strip"')}</div>${handle}`;
    case "contact_tile": {
      const open = previewEntityState(widget.entity_id) !== "off";
      return `<div class="tp tp-contact"><div class="tp-door${open ? "" : " closed"}"><i></i></div><div class="tp-blind-text">${name('b', '')}<small class="${open ? "tp-amber" : "tp-green"}">${pvText(open ? "contact_open" : "contact_closed")}</small></div></div>${handle}`;
    }
    case "lock_tile": {
      const state = editor.states.get(widget.entity_id) || "locked";
      return `<div class="tp tp-contact"><div class="tp-icon-circle">${["locked", "locking"].includes(state) ? "\uD83D\uDD12" : "\uD83D\uDD13"}</div><div class="tp-blind-text">${name('b', '')}<small>${escapeHtml(state)}</small></div></div>${handle}`;
    }
    case "alarm_tile": {
      const state = editor.states.get(widget.entity_id) || "disarmed";
      const armed = state.startsWith("armed");
      return `<div class="tp tp-contact"><div class="tp-icon-circle">${armed ? "\uD83D\uDD12" : "\uD83D\uDD13"}</div><div class="tp-blind-text">${name('b', '')}<small>${escapeHtml(state.replace(/_/g, " "))}</small></div></div>${handle}`;
    }
    case "energy_timeline": {
      // The counters as rings (or nested rings with a legend), a bar chart with the forecast line below (w_energy_timeline.c).
      const nested = widget.style_variant === "nested";
      const palette = energyTimelinePalette(widget);
      const counters = [["home", palette.grid, 70, "23.1"], ["feed_in", palette.export, 100, "7.5"], ["solar", palette.solar_dest, 55, "31.5"], ["battery", palette.battery_out, 64, "64"]];
      const labels = { home: t("layout.energy.home"), feed_in: "Feed-in", solar: t("layout.energy.solar"), battery: t("layout.energy.battery") };
      const top = nested
        ? `<div class="tp-etl-nested"><i style="--r:100%;--p:64;--c:${palette.grid}"></i><i style="--r:76%;--p:90;--c:${palette.solar}"></i><i style="--r:52%;--p:55;--c:${palette.battery_out}"></i></div><span class="tp-etl-legend"><em class="e-solar">${escapeHtml(labels.solar)}</em><em class="e-grid">Grid</em><em class="e-out">Feed-in</em></span>`
        : counters.map(([id, color, pct, value]) => `<span class="tp-etl-counter"><i style="--p:${pct};--c:${color}"><b>${value}</b></i><small>${escapeHtml(labels[id])}</small></span>`).join("");
      return `<div class="tp tp-etl${nested ? " tp-etl-is-nested" : ""}" style="${Object.entries(palette).map(([key, value]) => `--etl-${key}:${value}`).join(";")}">${name('small', '')}<div class="tp-etl-top">${top}</div><div class="tp-etl-bars">${Array.from({ length: 48 }, (_, i) => `<u style="height:${12 + Math.round(38 * Math.abs(Math.sin(i / 7.6)))}%;background:linear-gradient(var(--etl-grid) 0 50%,var(--etl-battery_out) 50% 65%,var(--etl-solar) 65%)"></u>`).join("")}<svg class="tp-etl-line" viewBox="0 0 100 40" preserveAspectRatio="none"><path d="M0,8 C12,10 22,30 40,32 S70,32 80,24 S92,6 100,10" fill="none" stroke="${palette.forecast}" stroke-width="1.6" vector-effect="non-scaling-stroke"/></svg></div></div>${handle}`;
    }
    case "price_forecast": {
      // A day of bars like the panel (w_price_forecast.c): below the day's average green, above it orange, hours that are over dimmed,
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
      const daySwitch = widget.price_day_switch === true;
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
        ? `<span class="tp-price-days"><em class="on">${escapeHtml(t("preview.price.today"))}</em>${Number(widget.rect?.w) < 360 ? "" : `<em>${escapeHtml(t("preview.price.tomorrow"))}</em>`}</span>` : "";
      const cheap = widget.price_cheap_times === true;
      // the cheap window 11:00-16:00 (slots 44..64) at its place in the view
      const bandStart = ((44 - shift) % 96 + 96) % 96;
      const bandWidth = Math.min(20, 96 - bandStart);
      const band = cheap ? `<u class="tp-price-band" style="left:${(bandStart / 96 * 100).toFixed(1)}%;width:${(bandWidth / 96 * 100).toFixed(1)}%"></u>` : "";
      const foot = cheap ? `<div class="tp-price-cheap">${escapeHtml(t("preview.price.cheap"))} 11:00-16:00</div>` : "";
      // ticks at 00, 06, 12 and 18 o'clock where they fall in the view
      const ticks = [0, 6, 12, 18].map((h) => ({ h, at: ((h * 4 - shift) % 96 + 96) % 96 })).filter((x) => daySwitch || x.at < 96)
        .map((x) => `<span style="left:${(x.at / 96 * 100).toFixed(1)}%">${String(x.h).padStart(2, "0")}</span>`).join("");
      return `<div class="tp tp-price">${name('div', 'class="tp-name"')}<div class="tp-price-head"><b>${price}</b><small>ct/kWh</small>${days}</div>
        <div class="tp-price-slot">${interval}</div>
        <div class="tp-price-bars${widget.price_line === true ? " line" : ""}">${band}${bars}</div><div class="tp-price-foot">${ticks}</div>${foot}</div>${handle}`;
    }
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
      const selector = widget.parqet_selector === true;
      const holdingsView = widget.parqet_view === "holdings";
      const heightPx = Number(widget.rect?.h) || 0;
      const limit = heightPx - 6;
      const chipsOn = selector && heightPx >= 200;
      const tabsOn = selector && heightPx >= 260;
      let y = 40;
      if (tabsOn) y += 34;
      if (chipsOn) y += 34;
      const chipEm = (label, on) => `<em class="${on ? "on" : ""}">${label}</em>`;
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
      return `<div class="tp tp-select"><div class="tp-blind-text">${name('small', '')}<b>${previewEntityValue(widget.entity_id, "pv")}</b></div><span class="tp-select-chevron">\u2304</span></div>${handle}`;
    case "datetime_tile": {
      return `<div class="tp tp-datetime">${name('small', '')}<b>${previewEntityValue(widget.entity_id, "06:30", previewDateTimeValue)}</b></div>${handle}`;
    }
    case "scene_tile":
      return `<div class="tp tp-contact"><div class="tp-icon-circle">\u25B6</div><div class="tp-blind-text">${name('b', '')}<small>${pvText("scene")}</small></div></div>${handle}`;
    case "binary_tile":
      if (widget.display_mode === "door") {
        const open = previewEntityState(widget.entity_id) !== "off";
        return `<div class="tp tp-contact"><div class="tp-door${open ? "" : " closed"}"><i></i></div><div class="tp-blind-text">${name('b', '')}<small class="${open ? "tp-amber" : "tp-green"}">${pvText(open ? "contact_open" : "contact_closed")}</small></div></div>${handle}`;
      }
      return `<div class="tp tp-contact"><div class="tp-icon-dot"></div><div class="tp-blind-text">${name('b', '')}<small class="tp-blue">${pvText("home")}</small></div></div>${handle}`;
    case "room_tile": {
      // Ring with the room temperature (only on a card big enough to hold the reading in it, like w_room.c), the extra
      // field beside it and the status chips below; the
      // chips follow the states of the card's entities, the samples apply to a card without any.
      const extra = ROOM_EXTRA_MODES.includes(widget.room_extra_mode) ? widget.room_extra_mode : "default";
      const summary = roomPreviewSummary(widget.entity_id, widget.room_entities);
      const known = summary.temp !== null || summary.lights || summary.contacts || summary.presence;
      const chips = known ? roomPreviewChips(summary, w)
        : `<i class="c-green">${pvText("room_closed")}</i><i class="c-amber">${pvText("room_light_on")}</i><i class="c-teal">${pvText("room_present")}</i>`;
      const temp = escapeHtml(summary.temp ?? (widget.entity_id ? "--" : "21.8\u00B0C"));
      const side = extra === "humidity" ? `<b class="tp-room-side">${escapeHtml(summary.hum ?? "45 %")}</b>`
        : extra === "cover" ? `<span class="tp-room-action">\u2193 ${pvText("room_open")}</span>`
          : extra === "media_player" ? `<span class="tp-room-action">\u23F8 ${pvText("room_playing")}</span>`
            : summary.hum !== null ? `<b class="tp-room-side tp-room-hum">${escapeHtml(summary.hum)}</b>` : "";
      return `<div class="tp tp-room">${name('div', 'class="tp-name" style="text-align:left"')}<div class="tp-room-main">${w >= 230 && h >= 180 ? `<i class="tp-room-ring"><b>${temp}</b></i>` : `<b class="tp-room-temp">${temp}</b>`}${side}</div>
        <div class="tp-room-chips">${chips}</div></div>${handle}`;
    }
    case "climate_tile":
      return `<div class="tp tp-climate">${name('div', 'class="tp-name" style="text-align:left"')}<small>${pvText("climate_now", { temp: "23.0\u00B0" })}</small>
        <div class="tp-clim-mid"><span>\u2212</span><b>22.0\u00B0</b><span>+</span></div>
        <div class="tp-clim-modes"><i>${pvText("climate_heat")}</i></div><div class="tp-clim-chips"><i>${pvText("climate_fan_auto")}</i><i>${pvText("climate_swing_off")}</i></div></div>${handle}`;
    case "light_tile":
      // Like the panel: state and brightness on top, colour wheel, the lamp, the name and the brightness slider.
      return `<div class="tp tp-light"><div class="tp-light-top"><span>${escapeHtml(t("layout.preview.on", {}, "ON"))}</span><span class="tp-light-pct">70 %<i class="tp-wheel"></i></span></div><svg class="tp-bulb-svg" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 21h6M10 18.5h4M12 2.8a6.2 6.2 0 0 0-3.6 11.2c.3.3.6.7.6 1.2v1.3h6v-1.3c0-.5.3-.9.6-1.2A6.2 6.2 0 0 0 12 2.8z" fill="none" stroke="#ff9d3d" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>${name('b', '')}<div class="tp-bar tp-bar-knob"><i style="width:70%"></i><u></u></div><div class="tp-switch tp-matte-switch"><i></i></div></div>${handle}`;
    case "heating_tile": {
      // The default style shows target, current temperature and the action under the ring (like w_heating_tile.c);
      // only the semi arc has the plus and minus buttons.
      const semi = widget.style_variant === "arc_semi";
      const opening = ["left", "right", "top", "bottom"].includes(widget.arc_opening) ? widget.arc_opening : "left";
      return `<div class="tp tp-heat${semi ? ` tp-heat-semi open-${opening}` : " tp-heat-arc"}"><div class="tp-ring"><small class="tp-heat-target">${pvText("heating_target", { temp: "22.5\u00B0" })}</small><b>21.0\u00B0</b>${name('small', '')}</div>${semi ? '<div class="tp-btns"><span>\u2212</span><span>+</span></div>' : `<small class="tp-heat-status">${pvText("heating_active")}</small>`}</div>${handle}`;
    }
    case "button":
      return `<div class="tp tp-button">${h >= 140 ? `<small class="tp-button-state">${pvText("on_state")}</small>` : ""}<div class="tp-switch tp-switch-big"><i></i></div>${name('b', '')}</div>${handle}`;
    case "slider": {
      // Vertical when the direction says so (blinds), horizontal otherwise; like the panel it shows state and value.
      const dir = widget.slider_direction || "auto";
      const vertical = dir === "bottom_to_top" || dir === "top_to_bottom" || (dir === "auto" && h > w);
      const isCover = String(widget.entity_id || "").startsWith("cover.");
      const state = escapeHtml(isCover ? t("layout.preview.open", {}, "OPEN") : t("layout.preview.on", {}, "ON"));
      const fromTop = dir === "top_to_bottom";
      if (vertical) {
        return `<div class="tp tp-slider tp-slider-vertical2"><div class="tp-vbar2"><i style="height:60%;${fromTop ? "top:0" : "bottom:0"}"></i><span>60 %</span></div>${name('b', '')}</div>${handle}`;
      }
      return `<div class="tp tp-slider tp-slider-h2${isCover ? " tp-slider-cover" : ""}"><div class="tp-hbar2"><i style="width:60%"></i><span>60 %</span></div>${name('b', '')}</div>${handle}`;
    }
    case "sensor": {
      // The panel shows the value in its largest font when it is all the tile shows.
      const showLabel = widgetShowsName(widget);
      const valueOnly = !showLabel && widget.sensor_show_age === false;
      return `<div class="tp tp-sensor${valueOnly ? " tp-sensor-value-only" : ""}">${showLabel ? `${name('small', '')}` : ""}<b>${previewEntityValue(widget.entity_id, "23.4 \u00B0C")}</b></div>${handle}`;
    }
    case "graph": {
      // Like the panel: the name and the current value on top, the chart below; the window sits after the value.
      const windowMin = Number(widget.graph_time_window_min) || 120;
      const window = windowMin % 60 === 0 ? `${windowMin / 60}h` : `${windowMin} min`;
      return `<div class="tp tp-graph"><div class="tp-graph-head">${name('small', '')}<b>${previewEntityValue(widget.entity_id, "2450 W")}</b></div><small class="tp-graph-window">${window}</small>${previewGraphMarkup(widget.graph_display_mode)}</div>${handle}`;
    }
    case "gauge": {
      const gaugeValue = previewEntityValue(widget.entity_id, "6.7 kW");
      const segmented = widget.gauge_bar_style === "segmented";
      if (widget.style_variant === "radial") {
        return `<div class="tp tp-gauge-radial"><div class="tp-arc"><b>${gaugeValue}</b></div>${name('small', '')}</div>${handle}`;
      }
      if (widget.style_variant === "horizontal") {
        const extras = [widget.secondary_entity_id, widget.tertiary_entity_id].filter(Boolean)
          .map((id) => `<span>${previewEntityValue(id, "")}</span>`).join("");
        const bar = segmented ? previewSegments(Math.min(Number(widget.gauge_segment_count) || 20, 30), 12, "h")
          : '<div class="tp-bar tall"><i style="width:60%"></i></div>';
        return `<div class="tp tp-gauge-h"><div class="tp-light-top">${name('span', '')}<span>${gaugeValue}</span></div>${bar}${extras ? `<small class="tp-gauge-extras">${extras}</small>` : ""}</div>${handle}`;
      }
      // Vertical: the value on top, the bar, then the name (w_gauge.c).
      const bar = segmented ? previewSegments(Math.min(Number(widget.gauge_segment_count) || 10, 30), 7, "v")
        : '<div class="tp-vbar"><i style="height:60%;bottom:0"></i></div>';
      return `<div class="tp tp-gauge-v"><b class="tp-gauge-value">${gaugeValue}</b>${bar}${name('small', '')}</div>${handle}`;
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
