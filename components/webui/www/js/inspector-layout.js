/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Inspector presentation only. Original fields and their handlers own validation and saved JSON.
let inspectorLayout = null;

function compactInspectorEnabled() {
  return document.body?.dataset.localUi !== "spike" && !document.querySelector('link[href="/__local__/spike.css"]');
}

function inspectorSensorCount(count) {
  return t(count === 1 ? "layout.inspector.sensor_count_one" : "layout.inspector.sensor_count", { count });
}

function refreshInspectorSearchButtons(root) {
  for (const button of root.querySelectorAll(".entity-picker-btn, [data-spike-sensor-picker]")) {
    const caption = t("entity_picker.search"); button.title = caption; button.setAttribute("aria-label", caption);
    if (!button.querySelector("svg")) button.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="10" cy="10" r="6"/><path d="m15 15 6 6"/></svg>';
  }
}

function bindWrappingEntityField(input, field, resize) {
  input.hidden = true;
  const mirror = () => { if (field.value !== input.value) field.value = input.value; resize(field); };
  for (const type of ["input", "change"]) input.addEventListener(type, mirror);
  input.focus = (...args) => { mirror(); field.focus(...args); };
  input.select = () => field.select();
  const forward = type => {
    input.value = field.value.replace(/[\r\n]/g, "");
    input.dispatchEvent(new Event(type, { bubbles: true }));
  };
  field.addEventListener("input", () => { forward("input"); resize(field); });
  for (const type of ["focus", "change", "blur"]) field.addEventListener(type, () => forward(type));
  field.addEventListener("keydown", event => {
    if (event.key === "Enter") { event.preventDefault(); field.blur(); }
  });
}

function resizeInspectorEntity(field) {
  if (!field.getClientRects().length) return;
  field.style.height = "auto";
  field.style.height = Math.max(30, field.scrollHeight + 2) + "px";
  if (document.activeElement !== field) { field.setSelectionRange(0, 0); field.scrollTop = 0; }
  field.scrollLeft = 0;
}

function initInspectorLayout() {
  const root = document.getElementById("inspectorSection");
  if (!document.body || !root || inspectorLayout || !compactInspectorEnabled()) return;
  document.body.classList.add("compact-inspector");
  const make = (tag, cls) => { const node = document.createElement(tag); node.className = cls; return node; };
  const form = root.querySelector(".form"), position = el.fX.closest("details");
  const properties = make("div", "inspector-properties"), geometry = make("div", "inspector-position");
  const tabs = make("div", "inspector-tabs"); tabs.setAttribute("role", "tablist");
  root.querySelector(".section-body").prepend(tabs);
  for (const group of form.querySelectorAll(":scope > .inspector-group")) (group === position ? geometry : properties).append(group);
  form.append(properties, geometry); position.open = true;
  const data = document.getElementById("inspectorGroupDataLabel").closest("details");
  data.open = true; data.classList.add("inspector-data");
  const tabButtons = [];
  for (const [name, panel] of [["properties", properties], ["position", geometry]]) {
    panel.id = "inspectorPanel_" + name;
    panel.setAttribute("role", "tabpanel");
    const tab = make("button", "btn small"); tab.type = "button"; tab.id = "inspectorTab_" + name;
    tab.setAttribute("role", "tab"); tab.setAttribute("aria-controls", panel.id);
    panel.setAttribute("aria-labelledby", tab.id);
    tab.onclick = () => selectInspectorTab(name);
    tabs.append(tab); tabButtons.push(tab);
  }
  tabs.addEventListener("keydown", event => {
    if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
    event.preventDefault();
    const next = event.key === "Home" ? "properties" : event.key === "End" ? "position"
      : inspectorLayout.tab === "properties" ? "position" : "properties";
    selectInspectorTab(next); tabButtons[next === "position" ? 1 : 0].focus();
  });
  const name = make("div", "inspector-name");
  el.fTitle.closest("label").before(name); name.append(el.fTitle.closest("label"), el.fShowName.closest("label"));
  const group = (id, inputs, container = el.energyTimelineFields) => {
    const node = make("details", "inspector-extra"); node.id = id;
    node.append(make("summary", ""));
    const fields = make("div", "inspector-extra-fields"); fields.append(...inputs.map(input => input.closest("label")));
    node.append(fields); container.append(node); return node;
  };
  const battery = group("inspectorEtlBattery", [el.fEtlCharge, el.fEtlDischarge, el.fEtlSoc]);
  const forecast = group("inspectorEtlForecast", [el.fEtlForecast]);
  const counterHelp = createInspectorHelpRow(el.energyTimelineHint, t("layout.inspector.counter_help"));
  el.energyTimelineFields.append(counterHelp);
  const etlStyle = make("div", "slider-options hidden");
  etlStyle.append(el.fEtlStyle.closest("label"));
  document.getElementById("inspectorGroupStyle").querySelector(".inspector-group-body").prepend(etlStyle);
  const editors = [];
  for (const input of document.querySelectorAll("#inspectorSection .entity-input-row input[type=text], #energyPageOptions input[type=text]")) {
    let row = input.closest(".entity-input-row");
    if (!row) {
      row = make("div", "entity-input-row"); input.before(row); row.append(input);
      const search = make("button", "btn small entity-picker-btn"); search.type = "button";
      search.dataset.inspectorPageSearch = "true";
      search.onclick = () => openLightEntityPicker("page_sensor", {
        inspectorTarget: { input, domain: "sensor", titleKey: "entity_picker.title_sensor", titleFallback: "Choose Sensor" },
        configOverride: entityPickerConfigForDomain("sensor", "entity_picker.title_sensor", "Choose Sensor"),
      });
      row.append(search);
    }
    const field = make("textarea", "inspector-entity-editor");
    field.id = input.id + "Editor"; field.rows = 1; field.wrap = "soft"; field.spellcheck = false;
    field.placeholder = input.placeholder; if (input.maxLength > 0) field.maxLength = input.maxLength;
    input.before(field); bindWrappingEntityField(input, field, resizeInspectorEntity); editors.push({ input, field });
  }
  const style = document.getElementById("inspectorGroupStyle");
  const byId = id => document.getElementById(id), pageForm = byId("pageInspectorSection").querySelector(".form");
  const navigation = group("inspectorPageNavigation", [byId("pageIconSelect"), el.pageTitleInput], pageForm);
  navigation.querySelector(".inspector-extra-fields").append(byId("pageIconHint")); pageForm.prepend(navigation);
  const energyOptions = byId("energyPageOptions");
  const pageBattery = group("inspectorPageBattery", [byId("energyBatterySoc")], energyOptions);
  const batteryManual = make("div", "inspector-extra-fields");
  batteryManual.append(...["energyBatteryPower", "energyBatteryCharge", "energyBatteryDischarge"].map(id => byId(id).closest("label")));
  pageBattery.querySelector(".inspector-extra-fields").prepend(batteryManual);
  const meters = group("inspectorPageMeters", [byId("energyGas"), byId("energyWater")], energyOptions);
  const helpBody = make("div", "inspector-extra-fields");
  for (const id of ["energyPageHint", "energySourceHint", "energyDetailsHint", "energyGasWaterHint", "energySolarPowerHint", "energyBatterySocHint"]) helpBody.append(byId(id));
  const pageHelp = createInspectorHelpRow(helpBody, t("layout.inspector.energy_help")); energyOptions.append(pageHelp);
  // Changes apply through the existing handlers; this button repeats the same operation.
  byId("applyEnergyPageBtn").hidden = true;
  inspectorLayout = { root, tabs, properties, geometry, tabButtons, battery, forecast, counterHelp, etlStyle, editors,
    navigation, pageBattery, batteryManual, meters, pageHelp, page: null,
    style, stylePreference: null, tab: "properties", widget: null, frame: null };
  style.querySelector("summary").addEventListener("click", event => {
    event.preventDefault(); inspectorLayout.stylePreference = !style.open; style.open = inspectorLayout.stylePreference;
  });
  root.closest("aside").addEventListener("toggle", event => {
    // Measuring an auto-collapsed Style section toggles it briefly; do not schedule another measurement.
    if (event.target !== style) scheduleInspectorReflow();
  }, true);
  window.addEventListener("resize", scheduleInspectorReflow);
  refreshInspectorLayout();
}

function selectInspectorTab(name) {
  if (!inspectorLayout) return;
  const ui = inspectorLayout; ui.tab = name;
  for (const [index, panel] of [ui.properties, ui.geometry].entries()) {
    const active = (index === 0 ? "properties" : "position") === name;
    panel.hidden = !active; ui.tabButtons[index].setAttribute("aria-selected", String(active));
    ui.tabButtons[index].tabIndex = active ? 0 : -1;
  }
  scheduleInspectorReflow();
}

function scheduleInspectorReflow() {
  if (!inspectorLayout || inspectorLayout.frame !== null) return;
  inspectorLayout.frame = requestAnimationFrame(() => {
    const ui = inspectorLayout; ui.frame = null;
    for (const { field } of ui.editors) resizeInspectorEntity(field);
    const aside = ui.root.closest("aside");
    const available = Math.max(180, window.innerHeight - Math.max(18, aside.getBoundingClientRect().top) - 18);
    aside.style.setProperty("--inspector-available", available + "px");
    if (ui.tab !== "properties" || !ui.style.getClientRects().length) return;
    if (ui.stylePreference !== null) { ui.style.open = ui.stylePreference; return; }
    ui.style.open = true;
    ui.style.open = aside.scrollHeight <= Math.min(available, parseFloat(getComputedStyle(aside).maxHeight) || available) + 1;
  });
}

function refreshInspectorLayout() {
  if (!inspectorLayout) return;
  const ui = inspectorLayout, widget = selectedWidget();
  if (ui.page !== editor.selectedPageId) {
    ui.page = editor.selectedPageId;
    for (const group of [ui.navigation, ui.pageBattery, ui.meters]) group.open = false;
    ui.root.closest("aside").scrollTop = 0;
  }
  ui.navigation.querySelector("summary").textContent = t("layout.inspector.navigation");
  setInspectorHelpCaption(ui.pageHelp, t("layout.inspector.energy_help"));
  const manual = el.energySource.value === ENERGY_SOURCE_MANUAL;
  ui.batteryManual.hidden = !manual; ui.meters.hidden = !manual;
  for (const [group, caption, inputs] of [
    [ui.pageBattery, "layout.inspector.battery", [el.energyBatteryPower, el.energyBatteryCharge, el.energyBatteryDischarge, el.energyBatterySoc]],
    [ui.meters, "layout.inspector.meters", [el.energyGas, el.energyWater]],
  ]) {
    const count = inputs.filter(input => input.value.trim() && (manual || input === el.energyBatterySoc)).length;
    group.querySelector("summary").textContent = t(caption) + " · " + (count ? inspectorSensorCount(count) : t("layout.inspector.optional"));
  }
  if (ui.widget !== widget?.id) {
    ui.widget = widget?.id; ui.stylePreference = null;
    for (const group of ui.root.querySelectorAll("details:not(.inspector-group)")) group.open = false;
    ui.root.closest("aside").scrollTop = 0; selectInspectorTab("properties");
  }
  ui.tabButtons[0].textContent = t("layout.inspector.properties");
  ui.tabButtons[1].textContent = t("layout.inspector.group_position");
  ui.tabs.setAttribute("aria-label", t("layout.inspector.heading"));
  ui.battery.querySelector("summary").textContent = t("layout.inspector.battery") + " · "
    + inspectorSensorCount([el.fEtlCharge, el.fEtlDischarge, el.fEtlSoc].filter(input => input.value.trim()).length);
  ui.forecast.querySelector("summary").textContent = t("layout.inspector.forecast") + " · "
    + t(el.fEtlForecast.value.trim() ? "layout.inspector.configured" : "layout.inspector.optional");
  setInspectorHelpCaption(ui.counterHelp, t("layout.inspector.counter_help"));
  ui.etlStyle.classList.toggle("hidden", widget?.type !== "energy_timeline");
  updateStyleGroupVisibility();
  if (widget?.type === "gauge") {
    const extras = ["radial", "horizontal"].includes(el.fGaugeStyle.value);
    // Presentation only: preserve configured extras when temporarily choosing another shape.
    el.fSecondaryEntityWrap.classList.toggle("hidden", !extras);
    el.fTertiaryEntityWrap.classList.toggle("hidden", !extras);
  }
  for (const { input, field } of ui.editors) {
    if (document.activeElement !== field) field.value = input.value;
    field.disabled = input.disabled;
    const caption = input.closest("label").querySelector("span")?.textContent || t("layout.inspector.entity");
    field.setAttribute("aria-label", caption);
  }
  refreshInspectorSearchButtons(ui.root.closest("aside"));
  scheduleInspectorReflow();
}
