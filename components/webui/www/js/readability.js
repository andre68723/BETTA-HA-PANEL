/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Panel4 targets mirror ui_typography.h; canvas geometry is in physical panel pixels.
function panel4Typography() { return CANVAS_WIDTH === 720 && CANVAS_HEIGHT === 600; }
function readabilityTextWidth(text, pixels) {
  const context = document.createElement("canvas").getContext("2d");
  context.font = `500 ${pixels}px "BETTA Poppins"`;
  return Math.ceil(context.measureText(text).width);
}
function forecastPreviewCount(widget, maximum = 6) {
  const width = Math.max(1, Number(widget.rect?.w || 0) - 28);
  if (Number.isInteger(widget.forecast_days)) {
    if (widget.forecast_days > 0) return clamp(widget.forecast_days, 1, maximum);
    const valueSize = textRoleSize("value", panel4Typography() ? 28 : 20);
    const required = readabilityTextWidth("-12°", valueSize) + readabilityTextWidth("-18°", valueSize) + 26;
    return clamp(Math.floor((width + 6) / required), 1, maximum);
  }
  return clamp(Math.floor((width + 6) / 60), 3, maximum);
}
function renderReadabilityInspector(widget) {
  const fields = document.getElementById("forecastReadabilityOptions");
  const days = document.getElementById("fForecastDays");
  const aligned = document.getElementById("fAlignForecast");
  if (!fields || !days || !aligned) return;
  const supported = widget && ["weather_3day", "solar_forecast"].includes(widget.type);
  fields.classList.toggle("hidden", !supported);
  days.value = supported && Number.isInteger(widget.forecast_days) ? String(widget.forecast_days) : "";
  aligned.checked = supported && widget.align_forecast === true;
  const solar = widget?.type === "solar_forecast";
  if (days.options) for (const option of days.options) option.disabled = solar && option.value === "6";
  days.disabled = solar && !aligned.checked;
  aligned.onchange = () => { days.disabled = solar && !aligned.checked; };
  aligned.closest("label").classList.toggle("hidden", widget?.type !== "solar_forecast");
  const hint = document.getElementById("fReadabilityHint");
  if (hint) hint.textContent = t("layout.inspector.readability_hint");
  if (widget?.type === "gauge" && panel4Typography()) {
    const hint = document.getElementById("fGaugeHint");
    // Use the actual state/unit and a worst-case range, not a universal 120 px recommendation.
    const shown = previewEntityValue(widget.entity_id, "100 %");
    const unit = editor.entities.find((entity) => entity?.id === widget.entity_id)?.unit || "%";
    const min = Number.isFinite(widget.gauge_min) ? widget.gauge_min : 0;
    const max = Number.isFinite(widget.gauge_max) ? widget.gauge_max : 100;
    const candidates = [shown, `${min.toFixed(2).replace(".", ",")} ${unit}`, `${max.toFixed(2).replace(".", ",")} ${unit}`];
    const width = Math.max(...candidates.map((text) => readabilityTextWidth(String(text).replace(/<[^>]*>/g, ""), textRoleSize("key", 38)))) + 32;
    if (hint) hint.textContent = t("layout.inspector.gauge_readability", { width }, `30 cm: about ${width} px for this reading at 38 px. Narrow tiles use 28/24/20 px or split units; compare a wider or horizontal gauge.`);
  }
}
function saveReadabilityFields(widget) {
  const days = document.getElementById("fForecastDays");
  const aligned = document.getElementById("fAlignForecast");
  if (["weather_3day", "solar_forecast"].includes(widget.type) && days?.value !== "" && days) widget.forecast_days = clamp(Number(days.value), 0, 6);
  else delete widget.forecast_days;
  if (widget.type === "solar_forecast" && aligned?.checked) widget.align_forecast = true;
  else delete widget.align_forecast;
}
