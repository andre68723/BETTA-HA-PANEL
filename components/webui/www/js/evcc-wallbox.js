// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
// Explicit loadpoint bindings; discovery never rewrites existing bindings in the background.
const EVCC_BINDINGS = [["entity_id", "Mode", "select"], ["evcc_vehicle_entity_id", "Vehicle", "select"], ["evcc_target_soc_entity_id", "Target", "number,select"], ["evcc_power_entity_id", "Power", "sensor"], ["evcc_soc_entity_id", "Soc", "sensor"], ["evcc_connected_entity_id", "Connected", "binary_sensor"], ["evcc_charging_entity_id", "Charging", "binary_sensor"], ["evcc_disabled_entity_id", "Disabled", "binary_sensor"], ["evcc_effective_soc_entity_id", "Effective", "sensor"], ["evcc_energy_entity_id", "Energy", "sensor"], ["evcc_remaining_entity_id", "Remaining", "sensor"]];
let evccLoadpoints = [], evccLookupGeneration = 0;
function evccInspectorInputs() { return EVCC_BINDINGS.map(([, field]) => el[`fEvcc${field}`]).filter(Boolean); }
function evccResetInspector() {
  evccLookupGeneration++;
  el.evccFields?.classList.add("hidden");
  for (const input of evccInspectorInputs()) input.value = "";
}
function evccLoadInspector(widget) {
  const active = widget?.type === "wallbox_evcc";
  el.evccFields?.classList.toggle("hidden", !active);
  for (const [key, field] of EVCC_BINDINGS) if (el[`fEvcc${field}`]) el[`fEvcc${field}`].value = active ? String(widget[key] || "") : "";
  if (el.fEntityWrap && active) el.fEntityWrap.classList.add("hidden");
  if (el.fEvccLoadpoint) el.fEvccLoadpoint.value = active ? (widget.evcc_loadpoint_id || "") : "";
}
function evccSaveInspector(widget, type) {
  if (type === "wallbox_evcc") {
    for (const [,field,domains] of EVCC_BINDINGS) {
      const value = el[`fEvcc${field}`]?.value.trim() || "";
      if (value && (!domains.split(",").includes(value.split(".")[0]) || !/^[a-z0-9_]+\.[a-z0-9_]+$/.test(value) || value.length >= 96)) { setStatus(t("evcc.invalid"),true); return false; }
    }
  }
  for (const [key, field] of EVCC_BINDINGS.slice(1)) {
    const value = type === "wallbox_evcc" ? (el[`fEvcc${field}`]?.value.trim() || "") : "";
    if (value) widget[key] = value; else delete widget[key];
  }
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
        evccStatus(evccLoadpoints.length ? t("evcc.choose") : t("evcc.none_found"));
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
  for (const [,field] of EVCC_BINDINGS) setTextById(`fEvcc${field}Label`, `evcc.${field.toLowerCase()}`);
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
  return `<div class="evcc-preview"><div class="evcc-head">${widgetNamePreview(widget,"span")}<span class="evcc-link">${escapeHtml(t("evcc.sessions"))}</span></div>
  <div class="evcc-modes">${["off","smart","now"].map(value=>`<span class="${mode === value ? "selected" : ""}">${escapeHtml(t(`evcc.${value === "now" ? "fast" : value}`))}</span>`).join("")}</div>
  <div class="evcc-reading">${[["power_label",power === null ? "-- kW" : `${power.toFixed(1)} kW`],["charged",energy === null ? "-- kWh" : `${energy.toFixed(1)} kWh`],["remaining",minutes === null ? "--" : `${Math.floor(minutes/60)}:${String(minutes%60).padStart(2,"0")} h`]].map(([label,value])=>`<div><small>${escapeHtml(t(`evcc.${label}`))}</small><b>${escapeHtml(value)}</b></div>`).join("")}</div>
  <div class="evcc-control ${disconnected ? "disconnected" : ""}">${escapeHtml(vehicle)}${disconnected ? "" : " ▾"}</div>
  ${disconnected ? `<div class="evcc-last"><b>${escapeHtml(t("evcc.last"))}</b><span>${escapeHtml(t("evcc.sessions_preview"))}</span></div>` : `<div class="evcc-status">${escapeHtml(t(`evcc.${status}`))}</div><div class="evcc-gauge"><i style="clip-path:inset(0 ${valid ? 100-soc : 100}% 0 0)"></i></div><div class="evcc-soc"><div><small>${escapeHtml(t("evcc.soc_label"))}</small><b>${valid ? Math.round(soc) : "--"} %</b></div><div><small>${escapeHtml(t("evcc.target_label"))}</small><b class="evcc-link">${target !== null && target >= 0 && target <= 100 ? Math.round(target) : "--"} % ▾</b></div></div>`}</div><div class="resize-handle"></div>`;
}
