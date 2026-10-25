/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The Energy page's schematic preview on the canvas (flows, home ring, sensor nodes); moved out of canvas.js.
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */

function energyPreviewSensorCount(count) {
  return t(count === 1 ? "layout.energy.sensor_count_one" : "layout.energy.sensor_count_many", { count });
}

function energyPreviewEntityLabel(entityId) {
  return escapeHtml(entityId || t("layout.energy.no_sensor"));
}

function energyPreviewNodeMarkup(id, label, value, style = "") {
  const styleAttr = style ? ` style="${escapeHtml(style)}"` : "";
  return `<div class="energy-preview-node ${id}"${styleAttr}><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong></div>`;
}

function energyPreviewLabelMarkup(id, title, detail) {
  return `<div class="energy-preview-label ${id}"><strong>${escapeHtml(title)}</strong><small>${energyPreviewEntityLabel(detail)}</small></div>`;
}

function energyPreviewNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function energyPreviewPositive(value) {
  const n = energyPreviewNumber(value);
  return n === null ? 0 : Math.max(n, 0);
}

function energyPreviewFormatValue(value, unit = "") {
  const n = energyPreviewNumber(value);
  const suffix = String(unit || "").trim();
  if (n === null) return suffix ? `-- ${suffix}` : "--";
  const abs = Math.abs(n);
  const decimals = abs === 0 || abs >= 100 ? 0 : 1;
  const text = n.toFixed(decimals);
  return suffix ? `${text} ${suffix}` : text;
}

function energyPreviewFormatKwh(value) {
  return energyPreviewFormatValue(value, "kWh");
}

function energyPreviewComputeFlows(snapshot) {
  let fromGrid = energyPreviewPositive(snapshot?.from_grid_kwh);
  let toGrid = energyPreviewPositive(snapshot?.to_grid_kwh);
  let solar = energyPreviewPositive(snapshot?.solar_kwh);
  let toBattery = energyPreviewPositive(snapshot?.to_battery_kwh);
  let fromBattery = energyPreviewPositive(snapshot?.from_battery_kwh);
  const out = {
    usedSolar: 0,
    usedGrid: 0,
    usedBattery: 0,
    usedTotal: fromGrid + solar + fromBattery - toGrid - toBattery,
    gridToBattery: 0,
    batteryToGrid: 0,
    solarToBattery: 0,
    solarToGrid: 0,
  };
  let remaining = Math.max(out.usedTotal, 0);

  const gridToBattery = Math.max(0, Math.min(toBattery, fromGrid - remaining));
  out.gridToBattery += gridToBattery;
  toBattery -= gridToBattery;
  fromGrid -= gridToBattery;

  out.solarToBattery = Math.min(solar, toBattery);
  toBattery -= out.solarToBattery;
  solar -= out.solarToBattery;

  out.solarToGrid = Math.min(solar, toGrid);
  toGrid -= out.solarToGrid;
  solar -= out.solarToGrid;

  out.batteryToGrid = Math.min(fromBattery, toGrid);
  fromBattery -= out.batteryToGrid;
  toGrid -= out.batteryToGrid;

  const secondGridToBattery = Math.min(fromGrid, toBattery);
  out.gridToBattery += secondGridToBattery;
  fromGrid -= secondGridToBattery;
  toBattery -= secondGridToBattery;

  out.usedSolar = Math.min(remaining, solar);
  remaining -= out.usedSolar;
  out.usedBattery = Math.min(fromBattery, remaining);
  remaining -= out.usedBattery;
  out.usedGrid = Math.min(remaining, fromGrid);
  return out;
}

function energyPreviewHomeRingStyle(flows) {
  const parts = [
    { color: ENERGY_PREVIEW_COLORS.solar, value: energyPreviewPositive(flows?.usedSolar) },
    { color: ENERGY_PREVIEW_COLORS.battery, value: energyPreviewPositive(flows?.usedBattery) },
    { color: ENERGY_PREVIEW_COLORS.grid, value: energyPreviewPositive(flows?.usedGrid) },
  ].filter((part) => part.value > 0.001);
  const total = parts.reduce((sum, part) => sum + part.value, 0);
  if (total <= 0.001 || parts.length === 0) {
    return `--energy-home-ring: ${ENERGY_PREVIEW_COLORS.idle} 0deg 360deg;`;
  }

  let start = 0;
  const segments = parts.map((part, index) => {
    if (index === parts.length - 1) {
      return `${part.color} ${start}deg 360deg`;
    }
    const remainingSegments = parts.length - index - 1;
    const maxEnd = 360 - remainingSegments;
    let end = Math.round(start + (part.value / total) * 360);
    end = Math.max(start + 1, Math.min(maxEnd, end));
    const segment = `${part.color} ${start}deg ${end}deg`;
    start = end;
    return segment;
  });
  return `--energy-home-ring: ${segments.join(", ")};`;
}

function energyPreviewFlowMarkup(id, visible, path, dot) {
  if (!visible) return "";
  const dotMarkup = dot ? `<circle class="dot ${id}" cx="${dot[0]}" cy="${dot[1]}" r="5" />` : "";
  return `<path class="flow ${id}" d="${path}" />${dotMarkup}`;
}

function renderEnergyCanvasPreview(page) {
  const energy = page.energy || {};
  const source = energy.source === ENERGY_SOURCE_MANUAL ? ENERGY_SOURCE_MANUAL : ENERGY_SOURCE_HA;
  const isManual = source === ENERGY_SOURCE_MANUAL;
  const configured = ENERGY_ENTITY_KEYS.filter((key) => energy[key]).length;
  const autoLabel = t("layout.energy.preview_auto");
  const headerBadge = isManual ? energyPreviewSensorCount(configured) : t("layout.energy.preview_source_ha");
  const snapshot = !isManual && editor.energySnapshot?.available === true ? editor.energySnapshot : null;
  const snapshotFlows = snapshot ? energyPreviewComputeFlows(snapshot) : null;
  const solarEntity = isManual ? energy.solar_power_entity_id : autoLabel;
  const gridEntity = isManual
    ? (energy.grid_power_entity_id || energy.grid_import_power_entity_id || energy.grid_export_power_entity_id)
    : autoLabel;
  const homeEntity = isManual ? energy.home_power_entity_id : autoLabel;
  const batteryEntity = isManual
    ? (energy.battery_power_entity_id || energy.battery_charge_power_entity_id ||
      energy.battery_discharge_power_entity_id || energy.battery_soc_entity_id)
    : autoLabel;
  const nodes = isManual ? {
    lowCarbon: false,
    solar: !!solarEntity,
    gas: !!energy.gas_entity_id,
    grid: !!gridEntity,
    home: !!homeEntity || configured > 0,
    battery: !!batteryEntity,
    water: !!energy.water_entity_id,
  } : {
    lowCarbon: false,
    solar: snapshot?.has_solar === true,
    gas: snapshot?.has_gas === true,
    grid: snapshot?.has_grid === true,
    home: true,
    battery: snapshot?.has_battery === true,
    water: snapshot?.has_water === true,
  };
  if (isManual && configured === 0) {
    nodes.home = true;
  }
  const homeValue = snapshot ? energyPreviewFormatKwh(Math.max(snapshotFlows?.usedTotal || 0, 0)) : (isManual ? "-- W" : "-- kWh");
  const solarValue = snapshot ? energyPreviewFormatKwh(snapshot.solar_kwh) : (isManual ? "-- W" : "-- kWh");
  const gridImport = energyPreviewPositive(snapshot?.from_grid_kwh);
  const gridExport = energyPreviewPositive(snapshot?.to_grid_kwh);
  const gridValue = snapshot
    ? `${gridExport > gridImport && gridExport > 0.01 ? "out" : "in"} ${energyPreviewFormatKwh(Math.max(gridImport, gridExport))}`
    : (isManual ? "-- W" : "-- kWh");
  const batteryCharge = energyPreviewPositive(snapshot?.to_battery_kwh);
  const batteryDischarge = energyPreviewPositive(snapshot?.from_battery_kwh);
  const batteryValue = snapshot
    ? `${batteryCharge > batteryDischarge && batteryCharge > 0.01 ? "chg" : "out"} ${energyPreviewFormatKwh(Math.max(batteryCharge, batteryDischarge))}`
    : (isManual ? "--%" : "-- kWh");
  const gasValue = snapshot ? energyPreviewFormatValue(snapshot.gas_value, snapshot.gas_unit) : "--";
  const waterValue = snapshot ? energyPreviewFormatValue(snapshot.water_value, snapshot.water_unit) : "--";
  const homeRingStyle = energyPreviewHomeRingStyle(snapshotFlows);
  const flows = [
    energyPreviewFlowMarkup("low-carbon", nodes.lowCarbon && nodes.grid, "M96 190 V282", [96, 254]),
    energyPreviewFlowMarkup("solar-return", nodes.solar && nodes.grid, "M312 190 V286 A40 40 0 0 1 272 326 H150", [250, 326]),
    energyPreviewFlowMarkup("solar", nodes.solar && nodes.home, "M312 190 V286 A40 40 0 0 0 352 326 H522", [402, 326]),
    energyPreviewFlowMarkup("battery-in", nodes.solar && nodes.battery, "M312 190 V452", [312, 312]),
    energyPreviewFlowMarkup("grid", nodes.grid && nodes.home, "M150 346 H522", [244, 346]),
    energyPreviewFlowMarkup("battery-out", nodes.battery && nodes.home, "M336 452 V386 A40 40 0 0 1 376 346 H522", [394, 346]),
    energyPreviewFlowMarkup("return", nodes.grid && nodes.battery, "M150 368 H272 A40 40 0 0 1 312 408 V452", [216, 368]),
    energyPreviewFlowMarkup("gas", nodes.gas && nodes.home, "M528 190 V282", [528, 254]),
    energyPreviewFlowMarkup("water", nodes.water && nodes.home, "M528 452 V402", [528, 426]),
  ].join("");
  const nodeMarkup = [
    nodes.lowCarbon ? energyPreviewNodeMarkup("low-carbon", "LC", "-- kWh") : "",
    nodes.solar ? energyPreviewNodeMarkup("solar", "PV", solarValue) : "",
    nodes.gas ? energyPreviewNodeMarkup("gas", "GAS", gasValue) : "",
    nodes.grid ? energyPreviewNodeMarkup("grid", "GRID", gridValue) : "",
    nodes.home ? energyPreviewNodeMarkup("home", "HOME", homeValue, homeRingStyle) : "",
    nodes.battery ? energyPreviewNodeMarkup("battery", "BAT", batteryValue) : "",
    nodes.water ? energyPreviewNodeMarkup("water", "H2O", waterValue) : "",
  ].join("");
  const labelMarkup = [
    nodes.lowCarbon ? energyPreviewLabelMarkup("low-carbon", t("layout.energy.low_carbon"), autoLabel) : "",
    nodes.solar ? energyPreviewLabelMarkup("solar", t("layout.energy.solar"), solarEntity) : "",
    nodes.gas ? energyPreviewLabelMarkup("gas", t("layout.energy.gas"), autoLabel) : "",
    nodes.grid ? energyPreviewLabelMarkup("grid", t("layout.energy.grid"), gridEntity) : "",
    nodes.home ? energyPreviewLabelMarkup("home", t("layout.energy.home"), homeEntity) : "",
    nodes.battery ? energyPreviewLabelMarkup("battery", t("layout.energy.battery"), batteryEntity) : "",
    nodes.water ? energyPreviewLabelMarkup("water", t("layout.energy.water"), autoLabel) : "",
  ].join("");
  const node = document.createElement("div");
  node.className = "energy-page-preview";
  const previewWidth = CANVAS_WIDTH;
  const previewHeight = CANVAS_HEIGHT;
  node.style.setProperty("--energy-preview-width", `${previewWidth}px`);
  node.style.setProperty("--energy-preview-height", `${previewHeight}px`);
  const viewBox = `0 0 ${previewWidth} ${previewHeight}`;
  node.innerHTML = `
    <div class="energy-preview-card">
      <div class="energy-preview-heading">
        <strong>${escapeHtml(t("layout.energy.preview_title"))}</strong>
        <span>${escapeHtml(headerBadge)}</span>
      </div>
      <svg class="energy-preview-flow" viewBox="${viewBox}" aria-hidden="true">
        ${flows}
      </svg>
      ${nodeMarkup}
      ${labelMarkup}
    </div>
  `;
  el.canvas.appendChild(node);
}
