/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Panel4 targets mirror ui_typography.h; canvas geometry is in physical panel pixels.
function panel4Typography() { return CANVAS_WIDTH === 720 && CANVAS_HEIGHT === 600; }
// Panel4 and Tab5 lay their widgets out from the text roles; the 10-inch boards (1280 x 800) keep fixed rows and smaller roles
// (UI_TEXT_ADAPTIVE in text_scale.h, reported as /api/version "text.adaptive"; the screen size stands in for older firmware).
function previewTextAdaptive() {
  if (typeof editor.appText?.adaptive === "boolean") return editor.appText.adaptive;
  const w = Number(editor.appScreenW) || 0;
  const h = Number(editor.appScreenH) || 0;
  return !(Math.max(w, h) === 1280 && Math.min(w, h) === 800);
}
function readabilityTextWidth(text, pixels) {
  const context = document.createElement("canvas").getContext("2d");
  context.font = `500 ${pixels}px "BETTA Poppins"`;
  return Math.ceil(context.measureText(text).width);
}
function forecastPreviewCount(widget, maximum = 7) {
  const width = Math.max(1, Number(widget.rect?.w || 0) - 28);
  if (Number.isInteger(widget.forecast_days)) {
    if (widget.forecast_days > 0) return clamp(widget.forecast_days, 1, maximum);
    const valueSize = textRoleSize("value", panel4Typography() ? 28 : 20);
    const required = readabilityTextWidth("-12°", valueSize) + readabilityTextWidth("-18°", valueSize) + 26;
    return clamp(Math.floor((width + 6) / required), 1, maximum);
  }
  return clamp(Math.floor((width + 6) / 60), 3, Math.min(maximum, 6)); // layouts saved before "Visible days" keep six columns
}
// Solar columns follow the weather's day grid only with vertical bars; with horizontal bars the options have no effect and
// stay hidden (their saved values are kept, the panel ignores them).
function readabilityToggle(widget) {
  const fields = document.getElementById("forecastReadabilityOptions");
  if (!fields) return;
  const horizontal = widget?.type === "solar_forecast"
    && normalizeSolarForecastBarOrientation(el.fSolarForecastBarOrientation?.value) !== "vertical";
  fields.classList.toggle("hidden", !widget || !["weather_3day", "solar_forecast"].includes(widget.type) || horizontal);
}
function renderReadabilityInspector(widget) {
  const fields = document.getElementById("forecastReadabilityOptions");
  const days = document.getElementById("fForecastDays");
  const aligned = document.getElementById("fAlignForecast");
  if (!fields || !days || !aligned) return;
  const supported = widget && ["weather_3day", "solar_forecast"].includes(widget.type);
  readabilityToggle(widget);
  days.value = supported && Number.isInteger(widget.forecast_days) ? String(widget.forecast_days) : "";
  aligned.checked = supported && widget.align_forecast === true;
  const solar = widget?.type === "solar_forecast";
  if (days.options) for (const option of days.options) option.disabled = solar && option.value === "6";
  days.disabled = solar && !aligned.checked;
  aligned.onchange = () => { days.disabled = solar && !aligned.checked; };
  aligned.closest("label").classList.toggle("hidden", !solar);
  const orientation = el.fSolarForecastBarOrientation;
  if (orientation) orientation.onchange = () => readabilityToggle(selectedWidget());
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
  if (["weather_3day", "solar_forecast"].includes(widget.type) && days?.value !== "" && days) widget.forecast_days = clamp(Number(days.value), 0, 7);
  else delete widget.forecast_days;
  if (widget.type === "solar_forecast" && aligned?.checked) widget.align_forecast = true;
  else delete widget.align_forecast;
}
