/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Flow colors are shared by the Inspector and preview; firmware defaults match energy_timeline_colors.h.
const ENERGY_TIMELINE_COLORS = Object.freeze({
  grid: "#039bef", solar: "#ff9800", export: "#e45e65",
  battery_out: "#26a69a", battery_in: "#e84393", forecast: "#ffd54f",
});
let energyColorWidgetId = null;

function energyTimelineColorLabel(key) {
  const translationKey = "energy.colors." + key;
  return t(translationKey);
}

function energyTimelineHex(value) {
  const hex = String(value ?? "").trim().replace(/^(#|0x)/i, "");
  return /^[0-9a-f]{6}$/i.test(hex) ? "#" + hex.toLowerCase() : null;
}

function normalizeEnergyTimelineColors(widget) {
  const colors = {};
  if (widget.type === "energy_timeline" && widget.energy_colors && typeof widget.energy_colors === "object") {
    for (const key of Object.keys(ENERGY_TIMELINE_COLORS)) {
      const hex = energyTimelineHex(widget.energy_colors[key]);
      if (hex) colors[key] = hex;
    }
  }
  if (Object.keys(colors).length) widget.energy_colors = colors;
  else delete widget.energy_colors;
}

function energyTimelinePalette(widget) {
  const palette = { ...ENERGY_TIMELINE_COLORS, solar_dest: "#8bc34a" };
  for (const key of Object.keys(ENERGY_TIMELINE_COLORS)) {
    const hex = energyTimelineHex(widget.energy_colors?.[key]);
    if (hex) palette[key] = hex;
  }
  if (energyTimelineHex(widget.energy_colors?.solar)) palette.solar_dest = palette.solar;
  return palette;
}

function editEnergyTimelineColor(key, value) {
  const widget = selectedWidget(), hex = energyTimelineHex(value);
  if (!widget || widget.type !== "energy_timeline" || !hex || !(key in ENERGY_TIMELINE_COLORS)) return false;
  widget.energy_colors = { ...widget.energy_colors, [key]: hex };
  renderAll(); return true;
}

function resetEnergyTimelineColors() {
  const widget = selectedWidget();
  if (!widget || widget.type !== "energy_timeline") return;
  delete widget.energy_colors; renderAll();
}

function openEnergyTimelineColor(key) {
  const widget = selectedWidget();
  if (!widget || widget.type !== "energy_timeline") return;
  const dialog = document.createElement("dialog"); dialog.className = "inspector-help-dialog energy-color-dialog";
  dialog.setAttribute("aria-labelledby", "energyColorTitle");
  const header = document.createElement("div"); header.className = "section-head";
  const title = document.createElement("h2"); title.id = "energyColorTitle"; title.textContent = energyTimelineColorLabel(key);
  const close = document.createElement("button"); close.type = "button"; close.className = "btn small";
  close.textContent = t("entity_picker.close"); close.onclick = () => dialog.close(); header.append(title, close);
  const row = document.createElement("div"); row.className = "energy-color-edit";
  const color = document.createElement("input"); color.type = "color"; color.value = energyTimelinePalette(widget)[key];
  color.setAttribute("aria-label", title.textContent);
  const label = document.createElement("label"); label.textContent = "HEX";
  const hex = document.createElement("input"); hex.type = "text"; hex.value = color.value; hex.maxLength = 7;
  hex.spellcheck = false; hex.autofocus = true; hex.setAttribute("aria-label", "HEX");
  color.oninput = () => { hex.value = color.value; hex.setCustomValidity(""); hex.setAttribute("aria-invalid", "false"); editEnergyTimelineColor(key, color.value); };
  hex.oninput = () => {
    const valid = editEnergyTimelineColor(key, hex.value);
    hex.setCustomValidity(valid ? "" : t("energy.colors.invalid"));
    hex.setAttribute("aria-invalid", String(!valid));
    if (valid) color.value = energyTimelineHex(hex.value);
  };
  hex.onblur = () => { if (energyTimelineHex(hex.value)) hex.value = color.value; else hex.reportValidity(); };
  label.append(hex); row.append(color, label); dialog.append(header, row);
  dialog.addEventListener("keydown", event => event.stopPropagation());
  dialog.addEventListener("close", () => dialog.remove());
  document.body.append(dialog); dialog.showModal();
}

function renderEnergyTimelineColors(widget) {
  const group = document.getElementById("energyTimelineColorOptions");
  if (!group) return;
  const enabled = widget?.type === "energy_timeline";
  group.classList.toggle("hidden", !enabled);
  const details = group.querySelector("details");
  if (energyColorWidgetId !== widget?.id) { details.open = false; energyColorWidgetId = widget?.id; }
  details.querySelector("summary").textContent = t("energy.colors.heading");
  if (!group.dataset.bound) {
    for (const button of group.querySelectorAll("[data-energy-color]")) {
      button.onclick = () => openEnergyTimelineColor(button.dataset.energyColor);
    }
    group.querySelector(".energy-colors-reset").onclick = resetEnergyTimelineColors;
    details.querySelector("summary").addEventListener("click", () => {
      if (inspectorLayout) inspectorLayout.stylePreference = true;
    });
    group.dataset.bound = "true";
  }
  const palette = enabled ? energyTimelinePalette(widget) : ENERGY_TIMELINE_COLORS;
  for (const button of group.querySelectorAll("[data-energy-color]")) {
    const key = button.dataset.energyColor, text = energyTimelineColorLabel(key);
    button.parentElement.querySelector("span").textContent = text;
    button.title = text; button.setAttribute("aria-label", text + " " + palette[key]);
    button.style.setProperty("--energy-color", palette[key]); button.textContent = palette[key].toUpperCase();
  }
  group.querySelector(".energy-colors-reset").textContent = t("settings.reset.button");
}
