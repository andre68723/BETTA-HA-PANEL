// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
//
// Bambu printer widget (bambu_printer): the Inspector fields for its explicit entity bindings and the canvas preview.
// The primary entity (entity_id) is the print status sensor; every other binding is a named field. The firmware maps
// the names to its generic entity slots (main/layout/bambu_printer_slots.c); the Inspector never writes the generic
// secondary/tertiary fields for this type.

// [widget key, Inspector element, kind of entity]
const BAMBU_BINDINGS = [
  ["nozzle_temp_entity_id", "fBambuNozzle", "sensor"],
  ["bed_temp_entity_id", "fBambuBed", "sensor"],
  ["progress_entity_id", "fBambuProgress", "sensor"],
  ["remaining_time_entity_id", "fBambuRemaining", "sensor"],
  ["stage_entity_id", "fBambuStage", "sensor"],
  ["job_name_entity_id", "fBambuJob", "sensor"],
  ["pause_entity_id", "fBambuPause", "button"],
  ["resume_entity_id", "fBambuResume", "button"],
  ["stop_entity_id", "fBambuStop", "button"],
  ["camera_entity_id", "fBambuCamera", "camera"],
];

function bambuInspectorInputs() {
  return BAMBU_BINDINGS.map(([, elementKey]) => el[elementKey]);
}

// Reset every field and hide the block (no widget selected, or another type).
function bambuResetInspector() {
  if (el.bambuFields) el.bambuFields.classList.add("hidden");
  for (const input of bambuInspectorInputs()) {
    if (input) input.value = "";
  }
}

function bambuLoadInspector(widget) {
  const isBambu = widget?.type === "bambu_printer";
  if (el.bambuFields) el.bambuFields.classList.toggle("hidden", !isBambu);
  for (const [key, elementKey] of BAMBU_BINDINGS) {
    if (el[elementKey]) el[elementKey].value = isBambu ? String(widget[key] || "") : "";
  }
  if (isBambu && el.bambuHint) el.bambuHint.textContent = t("layout.inspector.bambu_hint");
}

// Writes the fields back. An emptied field removes its key; a field that does not belong to the type never stays.
function bambuSaveInspector(widget, widgetType) {
  for (const [key, elementKey] of BAMBU_BINDINGS) {
    const value = widgetType === "bambu_printer" ? (el[elementKey]?.value.trim() ?? "") : "";
    if (value) widget[key] = value;
    else delete widget[key];
  }
  if (widgetType === "bambu_printer") {
    delete widget.secondary_entity_id;
    delete widget.tertiary_entity_id;
  }
}

// ---- finding the printers in Home Assistant ----------------------------------------------------------------------

// The panel asks Home Assistant for the Bambu Lab printers and the entity of each binding (GET /api/ha/printers).
async function bambuFetchPrinters() {
  for (let poll = 0; poll < 60; poll++) {
    const data = await apiGet("/api/ha/printers" + (poll === 0 ? "?refresh=1" : ""));
    if (data.status === "ready") return Array.isArray(data.printers) ? data.printers : [];
    if (data.status === "disconnected") throw new Error(t("entity_picker.disconnected"));
    if (data.status === "error") throw new Error(t("layout.inspector.bambu_failed"));
    await new Promise((resolve) => setTimeout(resolve, 400));
  }
  throw new Error(t("entity_picker.pending"));
}

function bambuSetDetectStatus(text) {
  if (el.bambuDetectStatus) el.bambuDetectStatus.textContent = text;
}

// Writes the entities of a printer into the selected widget: the status sensor and every named binding. A binding the
// printer has no entity for is cleared, so nothing of another printer stays.
function bambuApplyPrinterEntities(entities, name = "") {
  const widget = selectedWidget();
  if (!widget || widget.type !== "bambu_printer") return false;
  if (el.fEntity) el.fEntity.value = String(entities.entity_id || "");
  for (const [key, elementKey] of BAMBU_BINDINGS) {
    if (el[elementKey]) el[elementKey].value = String(entities[key] || "");
  }
  // A widget still carrying its default title takes the printer's name.
  if (name && el.fTitle && (!el.fTitle.value.trim() || el.fTitle.value.trim() === "Bambu printer")) el.fTitle.value = name;
  autoApplyInspector({ softEntityValidation: true });
  return true;
}

let bambuPrinters = [];

function bambuChoosePrinter(id) {
  const printer = bambuPrinters.find((candidate) => candidate.id === id);
  if (!printer) return;
  if (bambuApplyPrinterEntities(printer.entities || {}, printer.name || "")) {
    bambuSetDetectStatus(t("layout.inspector.bambu_filled", { name: printer.name || "" }));
  }
}

// Looks the printers up and fills the Printer list; with autoPick a single printer is chosen at once.
async function bambuDetectPrinters({ autoPick = false } = {}) {
  bambuSetDetectStatus(t("layout.inspector.bambu_detecting"));
  try {
    bambuPrinters = await bambuFetchPrinters();
  } catch (error) {
    bambuSetDetectStatus(String(error.message || error));
    return;
  }
  const select = el.fBambuPrinter;
  if (select) {
    select.innerHTML = "";
    const first = document.createElement("option");
    first.value = "";
    first.textContent = t("layout.inspector.bambu_choose");
    select.appendChild(first);
    for (const printer of bambuPrinters) {
      const option = document.createElement("option");
      option.value = printer.id;
      option.textContent = printer.name || printer.id;
      select.appendChild(option);
    }
  }
  if (!bambuPrinters.length) {
    bambuSetDetectStatus(t("layout.inspector.bambu_none"));
  } else if (autoPick && bambuPrinters.length === 1) {
    if (select) select.value = bambuPrinters[0].id;
    bambuChoosePrinter(bambuPrinters[0].id);
  } else {
    bambuSetDetectStatus(t("layout.inspector.bambu_choose"));
  }
}

// The Add menu entry: a new card with its bindings found by the printer lookup, no entity picker first.
async function addBambuPrinterWidget() {
  const widget = addWidget("bambu_printer", { entityId: "" });
  if (widget) await bambuDetectPrinters({ autoPick: true });
}

function bambuFillOptions() {
  setEntityOptionsList(
    document.getElementById("bambuSensorOptions"),
    listEntitiesByDomain("sensor").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
  );
  setEntityOptionsList(
    document.getElementById("bambuButtonOptions"),
    listEntitiesByDomain("button").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
  );
}

// ---- canvas preview --------------------------------------------------------------------------------------------

const BAMBU_PREVIEW_STATES = {
  running: ["layout.preview.bambu_printing", "Printing"],
  pause: ["layout.preview.bambu_paused", "Paused"],
  idle: ["layout.preview.bambu_idle", "Idle"],
  finish: ["layout.preview.bambu_finished", "Finished"],
  failed: ["layout.preview.bambu_failed", "Failed"],
  offline: ["layout.preview.bambu_offline", "Offline"],
};

function bambuPreviewNumber(entityId) {
  const raw = entityId ? editor.states.get(entityId) : undefined;
  const parsed = Number(String(raw ?? "").replace(",", "."));
  return raw !== undefined && String(raw).trim() !== "" && Number.isFinite(parsed) ? parsed : null;
}

// The same simple frame as the firmware's illustration (1000 x 800 units): posts, gantry, head, bed and base.
const BAMBU_PREVIEW_SVG = `<svg class="bambu-preview-art" viewBox="0 0 1000 800" preserveAspectRatio="xMidYMid meet" aria-hidden="true">
  <rect class="b-soft" x="50" y="690" width="900" height="90" rx="30"/>
  <rect class="b-frame" x="80" y="170" width="64" height="530" rx="14"/>
  <rect class="b-frame" x="856" y="170" width="64" height="530" rx="14"/>
  <rect class="b-frame" x="80" y="190" width="840" height="58" rx="14"/>
  <rect class="b-bed" x="200" y="600" width="600" height="46" rx="12"/>
  <rect class="b-head" x="410" y="248" width="180" height="150" rx="22"/>
  <rect class="b-nozzle" x="478" y="398" width="44" height="58" rx="8"/>
  <circle class="b-spool" cx="770" cy="95" r="75"/>
</svg>`;

function bambuPreviewMarkup(widget) {
  const status = String(editor.states.get(widget.entity_id) ?? "");
  const [statusKey, statusFallback] = BAMBU_PREVIEW_STATES[status] || (status === ""
    ? ["layout.preview.bambu_no_data", "No data"] : ["unknown", "unavailable"].includes(status)
      ? ["layout.preview.bambu_unavailable", "Unavailable"] : ["layout.preview.bambu_unknown", "Unknown"]);
  const nozzle = bambuPreviewNumber(widget.nozzle_temp_entity_id);
  const bed = bambuPreviewNumber(widget.bed_temp_entity_id);
  const progress = bambuPreviewNumber(widget.progress_entity_id);
  const percent = progress === null ? 46 : clamp(Math.round(progress), 0, 100);
  const remaining = bambuPreviewNumber(widget.remaining_time_entity_id);
  const temp = (value, fallback) => `${value === null ? fallback : Math.round(value)} °C`;
  const known = ["running", "pause"].includes(status);
  const usable = (id) => previewControlAvailable(id, true);
  const canPause = known && usable(status === "pause" ? widget.resume_entity_id : widget.pause_entity_id);
  // A paused job offers Resume in the first button (primary_resume in w_bambu_printer.c).
  return `
    <div class="bambu-preview">
      <div class="bambu-preview-head">
        ${widgetNamePreview(widget, "span", 'class="bambu-preview-name"')}
        <span class="bambu-preview-status">${escapeHtml(t(statusKey, {}, statusFallback))}</span>
      </div>
      ${widget.stage_entity_id ? `<small class="bambu-preview-step">${escapeHtml(t("layout.preview.bambu_step"))}</small>` : ""}
      <div class="bambu-preview-stage">
        ${BAMBU_PREVIEW_SVG}
      </div>
      <div class="bambu-preview-temperatures">
        <span>${escapeHtml(t("layout.preview.bambu_nozzle", {}, "Nozzle"))} ${escapeHtml(temp(nozzle, 220))}</span>
        <span>${escapeHtml(t("layout.preview.bambu_bed", {}, "Bed"))} ${escapeHtml(temp(bed, 65))}</span>
      </div>
      <div class="bambu-preview-readings"><b>${percent}%</b><span>${remaining === null ? 22 : Math.round(remaining)} ${escapeHtml(t("layout.preview.bambu_min", {}, "min"))}</span></div>
      <div class="bambu-preview-bar"><i style="width:${percent}%"></i></div>
      <div class="bambu-preview-buttons">
        <span class="${previewControlClass(status === "running", canPause)}">${escapeHtml(status === "pause" ? t("layout.preview.bambu_resume", {}, "Resume") : t("layout.preview.bambu_pause", {}, "Pause"))}</span>
        <span>${escapeHtml(t("layout.preview.bambu_status", {}, "Status"))}</span>
        <span class="${previewControlClass(false, known && usable(widget.stop_entity_id), true)}">${escapeHtml(t("layout.preview.bambu_stop", {}, "Abort"))}</span>
      </div>
    </div>
    <div class="resize-handle"></div>
  `;
}
