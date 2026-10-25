// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
// Bindings come from the selected evcc instance; successful discovery synchronizes the bindings of the selected evcc instance.
const EVCC_BINDINGS = [["entity_id", "Mode", "select"], ["evcc_vehicle_entity_id", "Vehicle", "select"], ["evcc_target_soc_entity_id", "Target", "number,select"], ["evcc_power_entity_id", "Power", "sensor"], ["evcc_soc_entity_id", "Soc", "sensor"], ["evcc_connected_entity_id", "Connected", "binary_sensor"], ["evcc_charging_entity_id", "Charging", "binary_sensor"], ["evcc_disabled_entity_id", "Disabled", "binary_sensor"], ["evcc_effective_soc_entity_id", "Effective", "sensor"], ["evcc_energy_entity_id", "Energy", "sensor"], ["evcc_remaining_entity_id", "Remaining", "sensor"]];
// The optional header's site power sensors (layout/evcc_slots.c k_evcc_site_bindings): only the energy flow reads them.
const EVCC_SITE_BINDINGS = [["evcc_pv_power_entity_id", "PvPower", "sensor"], ["evcc_battery_power_entity_id", "BatteryPower", "sensor"],
  ["evcc_grid_power_entity_id", "GridPower", "sensor"], ["evcc_home_power_entity_id", "HomePower", "sensor"]];
const EVCC_HEADERS = ["power", "flow"];
const EVCC_POWER_MAX_KW_DEFAULT = 11;
let evccLoadpoints = [], evccLookupGeneration = 0, evccSearched = false;
function evccInspectorInputs() { return [...EVCC_BINDINGS, ...EVCC_SITE_BINDINGS].map(([, field]) => el[`fEvcc${field}`]).filter(Boolean); }
function evccResetInspector() {
  evccLookupGeneration++;
  el.evccFields?.classList.add("hidden");
  el.evccHeaderOptions?.classList.add("hidden");
  for (const input of evccInspectorInputs()) input.value = "";
  if (el.fEvccHeader) el.fEvccHeader.value = "";
  if (el.fEvccPowerMax) el.fEvccPowerMax.value = "";
}
// The card height the header needs, like layout() in w_wallbox_evcc.c: the name row, the header and every row below it.
function evccHeaderNeededHeight(header) {
  const line = (role, fallback) => previewLineHeight(textRoleSize(role, fallback));
  const meta = line("meta", 20), label = line("label", 24), value = line("value", 28);
  const row = Math.max(label + 12, 42);
  const body = row + 10 + meta + label + 12 + row + 10 + meta + 6 + 16 + 8 + meta + 2 + value;
  // the panel's 20 px power bar; the flow's icon rows (the 26 px symbol font, 25 px lines) around a bar of one value line
  const head = header === "power" ? meta + 4 + 20 + 12 : header === "flow" ? 25 + 2 + meta + 8 + 2 + 25 + 12 : 0;
  return 12 + meta + 12 + head + body + 8;
}
// The header fields follow the choice: the maximum for the power bar, the site sensors for the flow; a header the card is
// too short for is named with the height it needs.
function evccHeaderVisibility(widget = selectedWidget()) {
  const header = el.fEvccHeader?.value || "";
  el.fEvccPowerMaxWrap?.classList.toggle("hidden", header !== "power");
  el.evccSiteFields?.classList.toggle("hidden", header !== "flow");
  if (!el.evccHeaderHint) return;
  const needed = header ? evccHeaderNeededHeight(header) : 0;
  const short = widget?.type === "wallbox_evcc" && header && Number(widget.rect?.h) < needed;
  el.evccHeaderHint.textContent = short ? t("evcc.header_size", { h: Math.ceil(needed / 10) * 10, current: widget.rect.h }) : "";
}
function evccLoadInspector(widget) {
  const active = widget?.type === "wallbox_evcc";
  el.evccFields?.classList.toggle("hidden", !active);
  for (const [key, field] of [...EVCC_BINDINGS, ...EVCC_SITE_BINDINGS]) if (el[`fEvcc${field}`]) el[`fEvcc${field}`].value = active ? String(widget[key] || "") : "";
  el.evccHeaderOptions?.classList.toggle("hidden", !active);
  if (el.fEvccHeader) el.fEvccHeader.value = active && EVCC_HEADERS.includes(widget.evcc_header) ? widget.evcc_header : "";
  if (el.fEvccPowerMax) el.fEvccPowerMax.value = active && Number.isFinite(widget.evcc_power_max_kw) ? String(widget.evcc_power_max_kw) : "";
  evccHeaderVisibility(widget);
  if (el.fEntityWrap && active) el.fEntityWrap.classList.add("hidden");
  const select = el.fEvccLoadpoint;
  if (active && typeof select?.replaceChildren === "function" && !evccLoadpoints.length) {
    /* Before the first search the list holds only the saved loadpoint, or says the search is running. */
    const option = document.createElement("option");
    option.value = widget.evcc_loadpoint_id || "";
    option.textContent = widget.evcc_loadpoint_id ? t("evcc.saved_loadpoint") : t("evcc.finding");
    select.replaceChildren(option);
  }
  if (select) select.value = active ? (widget.evcc_loadpoint_id || "") : "";
  /* the loadpoints are looked up once per editor session; Find loadpoints asks again */
  if (active && !evccSearched) {
    evccSearched = true;
    void evccDetectLoadpoints();
  } else if (active && evccLoadpoints.length) {
    // Rendering the inspector must finish before an automatic completion edits and renders the model again.
    Promise.resolve().then(() => {
      if (selectedWidget() === widget && evccSyncBindings(widget)) autoApplyInspector({softEntityValidation:true});
    });
  }
}
// Choosing the energy flow needs the site sensors: take them from the loadpoint's lookup at once (or look it up),
// as selecting the wallbox does, instead of only on the next loadpoint choice.
function evccHeaderChanged() {
  evccHeaderVisibility();
  const widget = selectedWidget();
  if (widget?.type !== "wallbox_evcc" || el.fEvccHeader?.value !== "flow") return;
  if (EVCC_SITE_BINDINGS.every(([, field]) => el[`fEvcc${field}`]?.value.trim())) return;
  if (evccLoadpoints.length) evccSyncBindings(widget);
  else if (widget.evcc_loadpoint_id || widget.entity_id) void evccDetectLoadpoints();
}
function evccSaveInspector(widget, type) {
  const maxText = el.fEvccPowerMax?.value.trim() || "";
  const max = Number(maxText);
  if (type === "wallbox_evcc") {
    for (const [,field,domains] of [...EVCC_BINDINGS, ...EVCC_SITE_BINDINGS]) {
      const value = el[`fEvcc${field}`]?.value.trim() || "";
      if (value && (!domains.split(",").includes(value.split(".")[0]) || !/^[a-z0-9_]+\.[a-z0-9_]+$/.test(value) || value.length >= 96)) { setStatus(t("evcc.invalid"),true); return false; }
    }
    if (maxText && !(max >= 1 && max <= 50)) { setStatus(t("evcc.header_max_invalid"), true); return false; }
  }
  for (const [key, field] of [...EVCC_BINDINGS.slice(1), ...EVCC_SITE_BINDINGS]) {
    const value = type === "wallbox_evcc" ? (el[`fEvcc${field}`]?.value.trim() || "") : "";
    if (value) widget[key] = value; else delete widget[key];
  }
  // The header and its maximum; the site sensors stay while another header is chosen, so switching back keeps them.
  const header = type === "wallbox_evcc" ? (el.fEvccHeader?.value || "") : "";
  if (EVCC_HEADERS.includes(header)) widget.evcc_header = header; else delete widget.evcc_header;
  if (type === "wallbox_evcc" && maxText) widget.evcc_power_max_kw = max; else delete widget.evcc_power_max_kw;
  if (type === "wallbox_evcc") { delete widget.secondary_entity_id; delete widget.tertiary_entity_id; }
  else delete widget.evcc_loadpoint_id;
  return true;
}
function evccFillOptions() {
  for (const domain of ["select", "number", "sensor", "binary_sensor"]) setEntityOptionsList(document.getElementById(`evcc${domain}Options`), listEntitiesByDomain(domain).slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS));
}
function evccStatus(text) { if (el.evccStatus) el.evccStatus.textContent = text; }
// Use the complete, successful registry result of this loadpoint. Old manual or other-instance bindings must not survive it.
function evccSyncBindings(widget) {
  if (widget?.type !== "wallbox_evcc") return false;
  let point = evccLoadpoints.find(candidate => candidate.id === widget?.evcc_loadpoint_id);
  let changed = false;
  // Older layouts may contain the detected mode but no saved loadpoint ID. Resolve only an exact, unique match.
  if (!point && !widget.evcc_loadpoint_id && widget.entity_id) {
    const matches = evccLoadpoints.filter(candidate => candidate.entities?.entity_id === widget.entity_id);
    if (matches.length === 1) {
      point = matches[0]; widget.evcc_loadpoint_id = point.id;
      if (el.fEvccLoadpoint) el.fEvccLoadpoint.value = point.id;
      changed = true;
    }
  }
  if (!point) return false;
  for (const [key, field] of [...EVCC_BINDINGS, ...EVCC_SITE_BINDINGS]) {
    const input = el[`fEvcc${field}`];
    const value = point.entities?.[key] || "";
    if (input && (input.value.trim() !== value || String(widget[key] || "") !== value)) {
      input.value = value;
      if (key === "entity_id" && el.fEntity) el.fEntity.value = input.value;
      changed = true;
    }
  }
  return changed;
}
async function evccDetectLoadpoints() {
  const widget = selectedWidget(), generation = ++evccLookupGeneration;
  if (widget?.type !== "wallbox_evcc") return;
  evccStatus(t("evcc.finding"));
  try {
    for (let poll = 0; poll < 60; poll++) {
      const data = await apiGet("/api/ha/evcc" + (poll === 0 ? "?refresh=1" : ""));
      if (generation !== evccLookupGeneration || selectedWidget() !== widget) return;
      if (["error", "disconnected"].includes(data.status)) throw new Error(t("evcc.failed"));
      if (data.status === "ready") {
        evccLoadpoints = Array.isArray(data.loadpoints) ? data.loadpoints : [];
        const select = el.fEvccLoadpoint;
        if (select) {
          select.innerHTML = "";
          for (const point of [{id:"", name:t("evcc.choose")}, ...evccLoadpoints]) {
            const option = document.createElement("option"); option.value = point.id;
            option.textContent = point.instance ? `${point.instance} / ${point.name}` : point.name;
            select.appendChild(option);
          }
          select.value = widget.evcc_loadpoint_id || "";
        }
        if (evccSyncBindings(widget)) autoApplyInspector({softEntityValidation:true});
        // A new blank wallbox with one loadpoint needs no extra choice; existing configurations are never replaced here.
        if (!widget.evcc_loadpoint_id && evccLoadpoints.length === 1 && ![...EVCC_BINDINGS, ...EVCC_SITE_BINDINGS].some(([key]) => widget[key])) {
          evccChooseLoadpoint(evccLoadpoints[0].id);
          return;
        }
        if (!evccLoadpoints.length) evccStatus(t("evcc.none_found"));
        else if (widget.evcc_loadpoint_id) evccBindingStatus(widget);
        else evccStatus(t("evcc.choose"));
        return;
      }
      await new Promise(resolve => setTimeout(resolve, 400));
    }
    throw new Error(t("evcc.failed"));
  } catch (error) { if (generation === evccLookupGeneration && selectedWidget() === widget) evccStatus(String(error.message || error)); }
}
function evccChooseLoadpoint(id) {
  const widget = selectedWidget(), point = evccLoadpoints.find(candidate => candidate.id === id);
  if (widget?.type !== "wallbox_evcc" || !point) return false;
  const changed = EVCC_BINDINGS.some(([key]) => widget[key] && widget[key] !== (point.entities?.[key] || ""));
  if (changed && !window.confirm(t("evcc.replace"))) { if (el.fEvccLoadpoint) el.fEvccLoadpoint.value = widget.evcc_loadpoint_id || ""; return false; }
  for (const [key, field] of EVCC_BINDINGS) if (el[`fEvcc${field}`]) el[`fEvcc${field}`].value = point.entities?.[key] || "";
  // The complete site binding set belongs to this instance; a missing role clears an old instance's value.
  for (const [key, field] of EVCC_SITE_BINDINGS) if (el[`fEvcc${field}`]) el[`fEvcc${field}`].value = point.entities?.[key] || "";
  if (el.fEntity) el.fEntity.value = point.entities?.entity_id || "";
  widget.evcc_loadpoint_id = id;
  autoApplyInspector({softEntityValidation:true});
  evccBindingStatus(widget);
  return true;
}
function evccBindingStatus(widget) {
  const bindings = widget.evcc_header === "flow" ? [...EVCC_BINDINGS, ...EVCC_SITE_BINDINGS] : EVCC_BINDINGS;
  const missing = bindings.filter(([key]) => !widget[key]).map(([,field]) => t(`evcc.${field.toLowerCase()}`));
  evccStatus(missing.length ? `${t("evcc.missing")}: ${missing.join(", ")}` : t("evcc.bound"));
}
async function addEvccWallboxWidget() { const widget = addWidget("wallbox_evcc", {entityId:""}); if (widget) await evccDetectLoadpoints(); }
function evccTranslate() {
  setTextById("addEvccWallboxBtn", "evcc.add");
  for (const [id,key] of [["fEvccLoadpointLabel","loadpoint"],["fEvccDetectBtn","find"],["evccDetailsSummary","entities"],["evccHint","hint"]]) setTextById(id, `evcc.${key}`);
  for (const [,field] of [...EVCC_BINDINGS, ...EVCC_SITE_BINDINGS]) setTextById(`fEvcc${field}Label`, `evcc.${field.toLowerCase()}`);
  setTextById("fEvccHeaderLabel", "evcc.header");
  setTextById("fEvccPowerMaxLabel", "evcc.header_max");
  for (const opt of el.fEvccHeader?.options || []) opt.textContent = t(`evcc.header_${opt.value || "off"}`);
}
// The header preview like render_header() in w_wallbox_evcc.c: the charging power against its maximum, or the energy flow
// with sources above two bars on one scale and consumers below. Missing readings follow the panel's empty bars.
// Material Design Icons (Apache-2.0, main/ui/fonts/MDI-APACHE-2.0.txt): the panel's flow symbols as 24-unit SVG paths,
// read from the committed materialdesignicons-webfont.ttf.
const EVCC_FLOW_ICONS = {"solar": "M11.43 2.01V5.53L15.0 3.75ZM10.45 8.01 8.01 10.45 11.76 11.71ZM2.01 11.43 3.75 15.0 5.53 11.43ZM9.98 2.01H2.01V9.98Q2.85 10.26 3.75 10.26Q5.53 10.26 7.03 9.39Q8.53 8.53 9.39 7.03Q10.26 5.53 10.26 3.75Q10.26 2.85 9.98 2.01ZM17.01 21.98V15.98H14.01L18.98 6.98V12.98H21.98Z", "battery": "M15.98 20.01H8.01V6.0H15.98ZM16.68 3.98H15.0V2.01H9.0V3.98H7.31Q6.79 3.98 6.39 4.38Q6.0 4.78 6.0 5.34V20.67Q6.0 21.23 6.39 21.60Q6.79 21.98 7.31 21.98H16.68Q17.20 21.98 17.60 21.60Q18.0 21.23 18.0 20.67V5.34Q18.0 4.78 17.60 4.38Q17.20 3.98 16.68 3.98ZM15.0 15.98H9.0V18.98H15.0ZM15.0 6.98H9.0V9.98H15.0ZM15.0 11.48H9.0V14.48H15.0Z", "grid": "M8.29 5.43 6.51 4.54 7.78 2.01H16.21L17.48 4.54L15.70 5.43L15.0 3.98H9.0ZM18.60 8.01H14.10L13.31 5.01H10.68L9.89 8.01H5.39L4.07 10.54L5.90 11.43L6.60 9.98H17.39L18.09 11.43L19.87 10.54ZM17.76 21.98H15.70L15.46 21.09L12.0 15.89L8.53 21.09L8.29 21.98H6.23L9.14 11.01H11.20L10.82 12.32L12.0 14.10L13.17 12.32L12.79 11.01H14.85ZM11.39 15.0 10.5 13.64 9.32 18.14ZM14.67 18.14 13.5 13.64 12.60 15.0Z", "home": "M9.98 20.01V14.01H14.01V20.01H18.98V12.0H21.98L12.0 3.0L2.01 12.0H5.01V20.01Z", "car": "M5.01 11.01 6.51 6.51H17.48L18.98 11.01ZM17.48 15.98Q16.87 15.98 16.42 15.56Q15.98 15.14 15.98 14.50Q15.98 13.87 16.42 13.42Q16.87 12.98 17.50 12.98Q18.14 12.98 18.56 13.42Q18.98 13.87 18.98 14.50Q18.98 15.14 18.56 15.56Q18.14 15.98 17.48 15.98ZM6.51 15.98Q5.85 15.98 5.43 15.56Q5.01 15.14 5.01 14.50Q5.01 13.87 5.43 13.42Q5.85 12.98 6.49 12.98Q7.12 12.98 7.57 13.42Q8.01 13.87 8.01 14.50Q8.01 15.14 7.57 15.56Q7.12 15.98 6.51 15.98ZM18.93 6.0Q18.75 5.57 18.37 5.29Q18.0 5.01 17.48 5.01H6.51Q6.0 5.01 5.62 5.29Q5.25 5.57 5.06 6.0L3.0 12.0V20.01Q3.0 20.43 3.28 20.71Q3.56 21.0 3.98 21.0H5.01Q5.43 21.0 5.71 20.71Q6.0 20.43 6.0 20.01V18.98H18.0V20.01Q18.0 20.43 18.28 20.71Q18.56 21.0 18.98 21.0H20.01Q20.43 21.0 20.71 20.71Q21.0 20.43 21.0 20.01V12.0Z"};
function evccHeaderPreview(widget, power) {
  const header = EVCC_HEADERS.includes(widget.evcc_header) ? widget.evcc_header : "";
  if (!header || Number(widget.rect?.h) < evccHeaderNeededHeight(header)) return "";
  const max = Number.isFinite(widget.evcc_power_max_kw) ? widget.evcc_power_max_kw : EVCC_POWER_MAX_KW_DEFAULT;
  const car = power === null ? 0 : power;
  if (header === "power") {
    const share = Math.max(0, Math.min(1, car / max));
    return `<div class="evcc-header"><div class="evcc-header-row"><small>${escapeHtml(t("evcc.header_power"))}</small><small>${power === null ? "--" : car.toFixed(1)} / ${Math.round(max)} kW</small></div>
      <div class="evcc-header-bar"><i style="width:${(share * 100).toFixed(1)}%;background:#43bf79"></i></div></div>`;
  }
  const kw = (id) => {
    const value = bambuPreviewNumber(id), unit = editor.stateMeta.get(id)?.unit;
    const factor = { W: 0.001, kW: 1, MW: 1000 }[unit];
    return value === null || factor === undefined ? 0 : value * factor;
  };
  const pv = kw(widget.evcc_pv_power_entity_id), battery = kw(widget.evcc_battery_power_entity_id);
  const grid = kw(widget.evcc_grid_power_entity_id), home = kw(widget.evcc_home_power_entity_id);
  // As the panel (w_wallbox_evcc.c): one bar of the sources with their values, brackets with icons above for the
  // sources and below for the consumers in their share of the same length.
  const ins = [["solar", "#ff9800", Math.max(pv, 0)], ["battery", "#26a69a", Math.max(battery, 0)], ["grid", "#039bef", Math.max(grid, 0)]];
  const outs = [["home", "var(--pv-text)", Math.max(home, 0)], ["car", "#43bf79", Math.max(car, 0)], ["battery", "#e84393", Math.max(-battery, 0)],
    ["grid", "#e45e65", Math.max(-grid, 0)]];
  const scale = Math.max(ins.reduce((a, [, , v]) => a + v, 0), outs.reduce((a, [, , v]) => a + v, 0), 0.001);
  const width = Math.max(1, Number(widget.rect?.w || 360) - 24);
  const meta = textRoleSize("meta", 20);
  const px = (v) => Math.round(width * v / scale);
  let largest = 0;
  ins.forEach(([, , v], i) => { if (v > ins[largest][2]) largest = i; });
  let x = 0;
  const segments = ins.map(([, color, v], i) => {
    const w = px(v);
    let text = i === largest ? `${v.toFixed(1)} kW` : v.toFixed(1);
    if (previewTextWidth(text, meta) + 8 > w) text = v.toFixed(1);
    const label = v >= 0.05 && previewTextWidth(text, meta) + 8 <= w ? `<b>${escapeHtml(text)}</b>` : "";
    const markup = w >= 1 ? `<i style="left:${x}px;width:${w}px;background:${color}">${label}</i>` : "";
    x += w;
    return markup;
  }).join("");
  const brackets = (items, above, positions) => items.map(([key, color, v], i) => {
    const [left, w] = positions(v, i);
    const inner = w - 4;
    if (v < 0.05 || inner < 8) return "";
    const side = above ? "above" : "below";
    if (inner < 26 + 16) return `<span class="evcc-arm ${side} both" style="left:${left + 2}px;width:${inner}px"></span>`;
    const gap = 26 + 8, armW = Math.floor((inner - gap) / 2);
    return `<span class="evcc-arm ${side} start" style="left:${left + 2}px;width:${armW}px"></span>`
      + `<svg viewBox="0 0 24 24" style="left:${left + 2 + armW + 4}px;color:${color}" aria-hidden="true"><path d="${EVCC_FLOW_ICONS[key]}"/></svg>`
      + `<span class="evcc-arm ${side} end" style="left:${left + 2 + armW + gap}px;width:${inner - armW - gap}px"></span>`;
  }).join("");
  let inX = 0, outX = 0;
  const top = brackets(ins, true, (v) => { const w = px(v), left = inX; inX += w; return [left, w]; });
  const bottom = brackets(outs, false, (v) => { const w = px(v), left = outX; outX += w; return [left, w]; });
  return `<div class="evcc-header evcc-flow" style="--flow-bar:${Math.round(meta * 1.25) + 8}px">
    <div class="evcc-flow-brackets">${top}</div><div class="evcc-flow-bar">${segments}</div><div class="evcc-flow-brackets">${bottom}</div></div>`;
}
function evccPreviewMarkup(widget) {
  const soc = bambuPreviewNumber(widget.evcc_soc_entity_id), valid = soc !== null && soc >= 0 && soc <= 100;
  const number = (id, units) => { const value = bambuPreviewNumber(id), unit = editor.stateMeta.get(id)?.unit; return value !== null && value >= 0 && Object.hasOwn(units, unit) ? value * units[unit] : null; };
  const disconnected = editor.states.get(widget.evcc_connected_entity_id) === "off";
  const power = disconnected ? 0 : number(widget.evcc_power_entity_id, {W:.001,kW:1});
  const energy = disconnected ? null : number(widget.evcc_energy_entity_id, {Wh:.001,kWh:1});
  const seconds = disconnected ? null : number(widget.evcc_remaining_entity_id, {s:1,min:60,h:3600});
  const minutes = seconds === null || seconds >= 36000000 ? null : Math.ceil(seconds/60);
  const modeOptions = editor.stateMeta.get(widget.entity_id)?.options;
  const standardModes = !modeOptions || modeOptions.length === 3 && modeOptions.includes("smart") && modeOptions.includes("now");
  const mode = editor.states.get(widget.entity_id), target = bambuPreviewNumber(widget.evcc_target_soc_entity_id);
  const raw = editor.states.get(widget.evcc_vehicle_entity_id);
  const vehicle = disconnected ? t("evcc.none") : raw === "null" ? t("evcc.guest") : editor.stateMeta.get(widget.evcc_vehicle_entity_id)?.vehicle_name || raw || "--";
  const status = editor.states.get(widget.evcc_charging_entity_id) === "on" ? "charging" : editor.states.get(widget.evcc_connected_entity_id) === "on" ? "connected" : "unavailable";
  return `<div class="evcc-preview"><div class="evcc-head">${widgetNamePreview(widget,"span")}<span class="evcc-link">${escapeHtml(t("evcc.sessions"))}</span></div>${evccHeaderPreview(widget, power)}
  <div class="evcc-modes">${["off","smart","now"].map(value=>`<span class="${previewControlClass(mode === value && (value !== "smart" || standardModes), previewControlAvailable(widget.entity_id) && (!modeOptions || value === "smart" || modeOptions.includes(value)))}">${escapeHtml(value === "smart" && !standardModes ? mode || t("evcc.mode") : t(`evcc.${value === "now" ? "fast" : value}`))}</span>`).join("")}</div>
  <div class="evcc-reading">${[["power_label",power === null ? "-- kW" : `${power.toFixed(1)} kW`],["charged",energy === null ? "-- kWh" : `${energy.toFixed(1)} kWh`],["remaining",minutes === null ? "--" : `${Math.floor(minutes/60)}:${String(minutes%60).padStart(2,"0")} h`]].map(([label,value])=>`<div><small>${escapeHtml(t(`evcc.${label}`))}</small><b>${escapeHtml(value)}</b></div>`).join("")}</div>
  <div class="evcc-control ${disconnected ? "disconnected" : ""}">${escapeHtml(vehicle)}${disconnected ? "" : " ▾"}</div>
  ${disconnected ? `<div class="evcc-last"><b>${escapeHtml(t("evcc.last"))}</b><span>${escapeHtml(t("evcc.sessions_preview"))}</span></div>` : `<div class="evcc-status">${escapeHtml(t(`evcc.${status}`))}</div><div class="evcc-gauge"><i style="clip-path:inset(0 ${valid ? 100-soc : 100}% 0 0)"></i></div><div class="evcc-soc"><div><small>${escapeHtml(t("evcc.soc_label"))}</small><b>${valid ? Math.round(soc) : "--"} %</b></div><div><small>${escapeHtml(t("evcc.target_label"))}</small><b class="evcc-link">${target !== null && target >= 0 && target <= 100 ? Math.round(target) : "--"} % ▾</b></div></div>`}</div><div class="resize-handle"></div>`;
}
