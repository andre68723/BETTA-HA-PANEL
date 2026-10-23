// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
// Explicit loadpoint bindings; discovery never rewrites existing bindings in the background.
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
  const body = row + 10 + meta + label + 12 + row + 10 + meta + 6 + 18 + meta + 2 + value;
  const head = header === "power" ? meta + 4 + 12 + 12 : header === "flow" ? meta + 4 + 10 + 3 + 10 + 4 + meta + 12 : 0;
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
  }
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
        evccStatus(!evccLoadpoints.length ? t("evcc.none_found") : widget.evcc_loadpoint_id ? "" : t("evcc.choose"));
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
  // the instance's site sensors come with the loadpoint; a manual binding stays where the search found none
  for (const [key, field] of EVCC_SITE_BINDINGS) if (el[`fEvcc${field}`] && point.entities?.[key]) el[`fEvcc${field}`].value = point.entities[key];
  if (el.fEntity) el.fEntity.value = point.entities?.entity_id || "";
  widget.evcc_loadpoint_id = id;
  autoApplyInspector({softEntityValidation:true});
  const missing = EVCC_BINDINGS.filter(([key]) => !point.entities?.[key]).map(([,field]) => t(`evcc.${field.toLowerCase()}`));
  evccStatus(missing.length ? `${t("evcc.missing")}: ${missing.join(", ")}` : t("evcc.bound"));
  return true;
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
// with sources above two bars on one scale and consumers below (live readings, samples where none are known).
function evccHeaderPreview(widget, power) {
  const header = EVCC_HEADERS.includes(widget.evcc_header) ? widget.evcc_header : "";
  if (!header || Number(widget.rect?.h) < evccHeaderNeededHeight(header)) return "";
  const max = Number.isFinite(widget.evcc_power_max_kw) ? widget.evcc_power_max_kw : EVCC_POWER_MAX_KW_DEFAULT;
  const car = power === null ? 3.7 : power;
  if (header === "power") {
    const share = Math.max(0, Math.min(1, car / max));
    return `<div class="evcc-header"><div class="evcc-header-row"><small>${escapeHtml(t("evcc.header_power"))}</small><small>${car.toFixed(1)} / ${Math.round(max)} kW</small></div>
      <div class="evcc-header-bar"><i style="width:${(share * 100).toFixed(1)}%;background:#43bf79"></i></div></div>`;
  }
  const kw = (id, sample) => {
    const value = bambuPreviewNumber(id), unit = editor.stateMeta.get(id)?.unit;
    const factor = { W: 0.001, kW: 1, MW: 1000 }[unit];
    return value === null || factor === undefined ? sample : value * factor;
  };
  const pv = kw(widget.evcc_pv_power_entity_id, 5.4), battery = kw(widget.evcc_battery_power_entity_id, -1.2);
  const grid = kw(widget.evcc_grid_power_entity_id, 0.3), home = kw(widget.evcc_home_power_entity_id, 0.8);
  const ins = [["pv", "#ff9800", Math.max(pv, 0)], ["battery", "#26a69a", Math.max(battery, 0)], ["grid", "#039bef", Math.max(grid, 0)]];
  const outs = [["home", "var(--pv-text)", Math.max(home, 0)], ["car", "#43bf79", Math.max(car, 0)], ["battery", "#e84393", Math.max(-battery, 0)],
    ["export", "#e45e65", Math.max(-grid, 0)]];
  const scale = Math.max(ins.reduce((a, [, , v]) => a + v, 0), outs.reduce((a, [, , v]) => a + v, 0), 0.001);
  const bar = (items) => `<div class="evcc-header-bar">${items.map(([, c, v]) => `<i style="width:${(v / scale * 100).toFixed(1)}%;background:${c}"></i>`).join("")}</div>`;
  // As the panel: word and value per item, only the value where the row is full, nothing once even that does not fit.
  const meta = textRoleSize("meta", 20);
  const row = (items, unit) => {
    const end = Number(widget.rect?.w || 360) - 24 - (unit ? previewTextWidth("kW", meta) + 10 : 0);
    let x = 0;
    const parts = [];
    for (const [key, c, v] of items) {
      if (v < 0.05) continue;
      let text = `${t(`evcc.flow_${key}`)} ${v.toFixed(1)}`;
      if (x + 12 + previewTextWidth(text, meta) > end) text = v.toFixed(1);
      const w = previewTextWidth(text, meta);
      if (x + 12 + w > end) continue;
      parts.push(`<span><i style="background:${c}"></i>${escapeHtml(text)}</span>`);
      x += 12 + w + 10;
    }
    return `<div class="evcc-header-row evcc-flow-row">${parts.join("")}${unit ? '<small class="evcc-flow-unit">kW</small>' : ""}</div>`;
  };
  return `<div class="evcc-header evcc-flow">${row(ins, true)}${bar(ins)}${bar(outs)}${row(outs, false)}</div>`;
}
function evccPreviewMarkup(widget) {
  const soc = bambuPreviewNumber(widget.evcc_soc_entity_id), valid = soc !== null && soc >= 0 && soc <= 100;
  const number = (id, units) => { const value = bambuPreviewNumber(id), unit = editor.stateMeta.get(id)?.unit; return value !== null && value >= 0 && Object.hasOwn(units, unit) ? value * units[unit] : null; };
  const disconnected = editor.states.get(widget.evcc_connected_entity_id) === "off";
  const power = disconnected ? 0 : number(widget.evcc_power_entity_id, {W:.001,kW:1});
  const energy = disconnected ? null : number(widget.evcc_energy_entity_id, {Wh:.001,kWh:1});
  const seconds = disconnected ? null : number(widget.evcc_remaining_entity_id, {s:1,min:60,h:3600});
  const minutes = seconds === null || seconds >= 36000000 ? null : Math.ceil(seconds/60);
  const mode = editor.states.get(widget.entity_id), target = bambuPreviewNumber(widget.evcc_target_soc_entity_id);
  const raw = editor.states.get(widget.evcc_vehicle_entity_id);
  const vehicle = disconnected || raw === "null" ? t("evcc.none") : editor.stateMeta.get(widget.evcc_vehicle_entity_id)?.vehicle_name || raw || "--";
  const status = editor.states.get(widget.evcc_charging_entity_id) === "on" ? "charging" : editor.states.get(widget.evcc_connected_entity_id) === "on" ? "connected" : "unavailable";
  return `<div class="evcc-preview"><div class="evcc-head">${widgetNamePreview(widget,"span")}<span class="evcc-link">${escapeHtml(t("evcc.sessions"))}</span></div>${evccHeaderPreview(widget, power)}
  <div class="evcc-modes">${["off","smart","now"].map(value=>`<span class="${mode === value ? "selected" : ""}">${escapeHtml(t(`evcc.${value === "now" ? "fast" : value}`))}</span>`).join("")}</div>
  <div class="evcc-reading">${[["power_label",power === null ? "-- kW" : `${power.toFixed(1)} kW`],["charged",energy === null ? "-- kWh" : `${energy.toFixed(1)} kWh`],["remaining",minutes === null ? "--" : `${Math.floor(minutes/60)}:${String(minutes%60).padStart(2,"0")} h`]].map(([label,value])=>`<div><small>${escapeHtml(t(`evcc.${label}`))}</small><b>${escapeHtml(value)}</b></div>`).join("")}</div>
  <div class="evcc-control ${disconnected ? "disconnected" : ""}">${escapeHtml(vehicle)}${disconnected ? "" : " ▾"}</div>
  ${disconnected ? `<div class="evcc-last"><b>${escapeHtml(t("evcc.last"))}</b><span>${escapeHtml(t("evcc.sessions_preview"))}</span></div>` : `<div class="evcc-status">${escapeHtml(t(`evcc.${status}`))}</div><div class="evcc-gauge"><i style="clip-path:inset(0 ${valid ? 100-soc : 100}% 0 0)"></i></div><div class="evcc-soc"><div><small>${escapeHtml(t("evcc.soc_label"))}</small><b>${valid ? Math.round(soc) : "--"} %</b></div><div><small>${escapeHtml(t("evcc.target_label"))}</small><b class="evcc-link">${target !== null && target >= 0 && target <= 100 ? Math.round(target) : "--"} % ▾</b></div></div>`}</div><div class="resize-handle"></div>`;
}
