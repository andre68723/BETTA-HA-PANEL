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

// A long entity ID wraps after "." or "_" rather than inside a word: the field shows zero-width spaces there, which
// never reach the bound input or the clipboard.
const ENTITY_SOFT_BREAK = "\u200B";
function entitySoftBreaks(value) {
  return String(value || "").replace(/([._])(?=.)/g, `$1${ENTITY_SOFT_BREAK}`);
}

function entityWithoutSoftBreaks(value) {
  return String(value || "").replaceAll(ENTITY_SOFT_BREAK, "");
}

function bindWrappingEntityField(input, field, resize) {
  input.hidden = true;
  // While typing the field already holds the input's value; only a change from elsewhere rewrites it.
  const mirror = () => {
    if (entityWithoutSoftBreaks(field.value) !== input.value) field.value = entitySoftBreaks(input.value);
    resize(field);
  };
  for (const type of ["input", "change"]) input.addEventListener(type, mirror);
  input.focus = (...args) => { mirror(); field.focus(...args); };
  input.select = () => field.select();
  const forward = type => {
    input.value = entityWithoutSoftBreaks(field.value).replace(/[\r\n]/g, "");
    input.dispatchEvent(new Event(type, { bubbles: true }));
  };
  field.addEventListener("input", () => { forward("input"); resize(field); });
  for (const type of ["focus", "change", "blur"]) field.addEventListener(type, () => forward(type));
  field.addEventListener("blur", () => { field.value = entitySoftBreaks(input.value); });
  for (const type of ["copy", "cut"]) field.addEventListener(type, event => {
    const text = field.value.slice(field.selectionStart, field.selectionEnd);
    if (!event.clipboardData || !text.includes(ENTITY_SOFT_BREAK)) return;
    event.preventDefault();
    event.clipboardData.setData("text/plain", entityWithoutSoftBreaks(text));
    if (type === "cut") field.setRangeText("", field.selectionStart, field.selectionEnd, "start"), forward("input");
  });
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

// The widget inspector reads top to bottom like the widget guides: a header (type, name, page and entity, device class),
// then Entities (what the tile reads or controls), Display (what it shows), Controls (what tap and long press do) and a
// collapsed Placement. The fields keep their elements, handlers and visibility logic; this layer only moves them.
const INSPECTOR_SECTIONS = ["entities", "display", "controls"];

function initInspectorLayout() {
  const root = document.getElementById("inspectorSection");
  if (!document.body || !root || inspectorLayout || !compactInspectorEnabled()) return;
  document.body.classList.add("compact-inspector");
  const make = (tag, cls) => { const node = document.createElement(tag); node.className = cls; return node; };
  const byId = id => document.getElementById(id), label = node => node.closest("label");
  const form = root.querySelector(".form"), position = el.fX.closest("details");
  // Header: the name is edited in place under the type (the section heading), above the device-class chips. The page
  // is named above the canvas and the entity in its field, so the header repeats neither.
  const head = make("div", "inspector-head");
  const titleLabel = label(el.fTitle);
  el.fTitle.classList.add("inspector-name-input");
  head.append(el.fTitle, el.fEntityClass);
  titleLabel.remove();
  root.querySelector(".section-body").prepend(head);
  const sections = {}, bodies = {};
  // Every section has the same form: its heading above a card with the fields, and the heading folds the card.
  const section = name => {
    const node = make("details", "inspector-sec"); node.dataset.section = name; node.open = true;
    const body = make("div", "inspector-sec-body"); node.append(make("summary", "inspector-sec-title"), body);
    sections[name] = node; bodies[name] = body;
    return node;
  };
  for (const name of INSPECTOR_SECTIONS) section(name);
  // In the document before anything moves, so getElementById still finds the moved wrappers.
  form.prepend(...INSPECTOR_SECTIONS.map(name => sections[name]));
  // A field that leaves a per-type wrapper goes into a copy of it whose visibility follows the original.
  const follows = [];
  const follow = (wrapper, nodes, section) => {
    const copy = make("div", wrapper.className); copy.classList.add("inspector-follow");
    copy.append(...nodes.filter(Boolean)); bodies[section].append(copy); follows.push({ wrapper, copy });
    return copy;
  };
  const move = (section, ...nodes) => bodies[section].append(...nodes.filter(Boolean));
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
  etlStyle.append(label(el.fEtlStyle));
  const editors = [], entityRows = [];
  for (const input of document.querySelectorAll("#inspectorSection .entity-input-row input[type=text], #energyPageOptions input[type=text]")) {
    let row = input.closest(".entity-input-row");
    const search = row?.querySelector("[data-inspector-entity-target]");
    if (search && input.closest("#inspectorSection")) {
      // A widget entity is mostly set when the widget is added: the row names it, and a click opens the picker.
      const field = label(input), button = make("button", "inspector-entity-row"); button.type = "button";
      button.append(make("span", "inspector-entity-name"), make("span", "inspector-entity-chevron"));
      button.lastChild.setAttribute("aria-hidden", "true");
      button.onclick = () => search.click();
      // The row shows the role before any parenthesis; "(optional)" and similar notes move to its tooltip.
      const caption = field.querySelector("span"), role = make("span", "inspector-entity-role");
      caption.hidden = true; field.prepend(role);
      field.classList.add("inspector-entity-label"); row.hidden = true; row.after(button);
      field.addEventListener("click", event => {
        if (event.target.closest("button, a, .field-hint, .meta")) return;
        event.preventDefault(); button.click();
      });
      input.addEventListener("change", () => refreshInspectorEntityRows());
      entityRows.push({ input, button, field, caption, role });
      continue;
    }
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
  const pageForm = byId("pageInspectorSection").querySelector(".form");
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
  // Entities: what the tile reads or controls, including where its data comes from.
  move("entities", label(el.fEntity), label(el.fSecondaryEntity), label(el.fTertiaryEntity), byId("parqetEntities"));
  follow(byId("vacuumOptions"), [label(el.fVacuumBattery)], "entities");
  follow(byId("roomEntitiesFields"), [label(el.fRoomEntities), byId("fRoomEntitiesHint")], "entities");
  move("entities", byId("carExtraFields"), byId("evccFields"), byId("bambuFields"), byId("energyTimelineFields"));
  follow(byId("solarForecastFields"), [byId("fSolarForecastTodayLabel"), byId("fSolarForecastTomorrowLabel"),
    byId("fSolarForecastDay3Label"), byId("fSolarForecastDay4Label"), byId("fSolarForecastDay5Label")].map(label), "entities");
  follow(byId("sliderOptions"), [label(el.fSliderEntityDomain)], "entities");
  follow(byId("graphOptions"), [label(el.fGraphHaHistory), byId("fGraphHaHistoryHint")], "entities");
  follow(byId("priceOptions"), [label(el.fPriceAverage)], "entities");
  // Display: what the tile shows.
  move("display", label(el.fShowName), byId("binaryOptions"));
  follow(byId("roomEntitiesFields"), [label(el.fRoomExtraMode), byId("fRoomExtraEntityWrap"), byId("fRoomExtraHint")], "display");
  follow(byId("solarForecastFields"), [label(el.fSolarForecastBarMax)], "display");
  follow(byId("buttonOptions"), [label(el.fButtonAccentColor)], "display");
  move("display", byId("sliderOptions"), byId("graphOptions"), byId("solarForecastStyleOptions"), byId("gaugeOptions"),
    byId("sensorOptions"), byId("cameraOptions"));
  follow(byId("priceOptions"), [label(el.fPriceCheapTimes), label(el.fPriceLine)], "display");
  move("display", byId("parqetOptions"), byId("forecastReadabilityOptions"), byId("weatherForecastOptions"),
    byId("heatingOptions"), etlStyle, byId("energyTimelineColorOptions"));
  // Controls: what tap and long press do.
  move("controls", byId("historyOptions"), byId("lockOptions"), byId("alarmOptions"), byId("buttonOptions"));
  follow(byId("priceOptions"), [label(el.fPriceDaySwitch)], "controls");
  follow(byId("parqetOptions"), [label(el.fParqetSelector)], "controls");
  follow(byId("roomEntitiesFields"), [label(el.fRoomControls), byId("fRoomControlsHint")], "controls");
  follow(byId("vacuumOptions"), [label(el.fVacuumDefaultMode), byId("fVacuumDefaultModeHint")], "controls");
  // The old groups are emptied; Placement is a section like the others, collapsed, at the end.
  const placement = section("placement");
  bodies.placement.append(...position.querySelector(".inspector-group-body").children);
  for (const group of form.querySelectorAll(":scope > .inspector-group")) group.hidden = true;
  form.append(placement);
  inspectorLayout = { root, head, sections, follows, position: placement, placementChoice: null, battery, forecast, counterHelp, etlStyle, editors, entityRows,
    navigation, pageBattery, batteryManual, meters, pageHelp, page: null, widget: null, frame: null };
  // Placement opens while the inspector fits without scrolling; a click on its heading decides until the next widget.
  placement.querySelector(":scope > summary").addEventListener("click", event => {
    event.preventDefault(); inspectorLayout.placementChoice = !placement.open; placement.open = inspectorLayout.placementChoice;
  });
  root.closest("aside").addEventListener("toggle", event => {
    // Measuring Placement opens and closes it briefly; that toggle needs no further measurement.
    if (event.target !== placement) scheduleInspectorReflow();
  }, true);
  window.addEventListener("resize", scheduleInspectorReflow);
  refreshInspectorLayout();
}

// Kept for callers of the former Properties / Position tabs: Placement is now a collapsed section.
function selectInspectorTab(name) {
  if (!inspectorLayout) return;
  inspectorLayout.placementChoice = name === "position";
  inspectorLayout.position.open = inspectorLayout.placementChoice;
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
    if (ui.position.hidden) return;
    if (ui.placementChoice !== null) { ui.position.open = ui.placementChoice; return; }
    ui.position.open = true;
    ui.position.open = aside.scrollHeight <= Math.min(available, parseFloat(getComputedStyle(aside).maxHeight) || available) + 1;
  });
}

function inspectorNodeShown(node, stop) {
  for (let at = node; at && at !== stop; at = at.parentElement) if (at.hidden || at.classList.contains("hidden")) return false;
  return true;
}

// The entity rows show the shortened Home Assistant name (the ID while the editor knows none), "not set" for an empty
// optional binding and a warning for an ID Home Assistant does not list.
function refreshInspectorEntityRows() {
  if (!inspectorLayout) return;
  const rows = inspectorLayout.entityRows.filter(row => inspectorNodeShown(row.field, inspectorLayout.root));
  const ids = rows.map(row => row.input.value.trim());
  const names = inspectorEntityRowNames(ids);
  rows.forEach((row, index) => {
    const id = ids[index];
    const known = !id || !editor.entities.length || editor.entities.some(item => item?.id === id) || editor.states?.has(id);
    row.button.querySelector(".inspector-entity-name").textContent = !id ? t("layout.inspector.entity_unset") : names[index] || id;
    row.button.querySelector(".inspector-entity-chevron").textContent = id ? "\u203A" : "+";
    row.button.classList.toggle("is-empty", !id);
    row.button.classList.toggle("is-missing", !known);
    row.button.title = !id ? "" : known ? id : `${id} \u00B7 ${t("layout.inspector.entity_missing")}`;
    row.button.disabled = row.input.disabled;
    const caption = row.caption.textContent.trim() || t("layout.inspector.entity");
    row.role.textContent = caption.replace(/\s*\(.*$/, "");
    row.role.title = row.role.textContent === caption ? "" : caption;
    row.button.setAttribute("aria-label", `${caption}: ${row.button.querySelector(".inspector-entity-name").textContent}`);
  });
}

// A section is shown while one of its fields is: a field counts unless it or a container up to the section is hidden.
function inspectorSectionHasField(section) {
  const hidden = node => node.hidden || node.classList.contains("hidden");
  const visible = node => {
    for (let at = node; at && at !== section; at = at.parentElement) if (hidden(at)) return false;
    return true;
  };
  return Array.from(section.querySelectorAll("label, details, button, select")).some(visible);
}

function refreshInspectorLayout() {
  if (!inspectorLayout) return;
  const ui = inspectorLayout, widget = selectedWidget();
  if (ui.page !== editor.selectedPageId) {
    ui.page = editor.selectedPageId;
    ui.navigation.open = true;
    for (const group of [ui.pageBattery, ui.meters]) group.open = false;
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
  const etlBattery = [el.fEtlCharge, el.fEtlDischarge, el.fEtlSoc].filter(input => input.value.trim()).length;
  if (ui.widget !== widget?.id) {
    ui.widget = widget?.id;
    for (const group of ui.root.querySelectorAll("details:not(.inspector-group):not(.inspector-sec)")) group.open = false;
    // Optional subgroups start open when something is set in them.
    ui.battery.open = etlBattery > 0; ui.forecast.open = !!el.fEtlForecast.value.trim();
    ui.placementChoice = null;
    ui.root.closest("aside").scrollTop = 0;
  }
  const heading = document.getElementById("inspectorHeading");
  if (heading && widget) heading.textContent = widgetTypeLabel(widget.type);
  ui.head.hidden = !widget;
  for (const name of INSPECTOR_SECTIONS) {
    ui.sections[name].querySelector(":scope > summary").textContent = t(`layout.inspector.section_${name}`);
  }
  const position = ui.position.querySelector(":scope > summary");
  position.textContent = t("layout.inspector.placement");
  if (widget?.rect) {
    const values = document.createElement("b");
    values.textContent = `${widget.rect.x} \u00B7 ${widget.rect.y} \u00B7 ${widget.rect.w} \u00D7 ${widget.rect.h}`;
    position.append(values);
  }
  ui.battery.querySelector("summary").textContent = t("layout.inspector.battery") + " \u00B7 "
    + inspectorSensorCount(etlBattery);
  ui.forecast.querySelector("summary").textContent = t("layout.inspector.forecast") + " \u00B7 "
    + t(el.fEtlForecast.value.trim() ? "layout.inspector.configured" : "layout.inspector.optional");
  setInspectorHelpCaption(ui.counterHelp, t("layout.inspector.counter_help"));
  ui.etlStyle.classList.toggle("hidden", widget?.type !== "energy_timeline");
  if (widget?.type === "gauge") {
    const extras = ["radial", "horizontal"].includes(el.fGaugeStyle.value);
    // Presentation only: preserve configured extras when temporarily choosing another shape.
    el.fSecondaryEntityWrap.classList.toggle("hidden", !extras);
    el.fTertiaryEntityWrap.classList.toggle("hidden", !extras);
  }
  for (const { input, field } of ui.editors) {
    if (document.activeElement !== field) field.value = entitySoftBreaks(input.value);
    field.disabled = input.disabled;
    const caption = input.closest("label").querySelector("span")?.textContent || t("layout.inspector.entity");
    field.setAttribute("aria-label", caption);
  }
  for (const { wrapper, copy } of ui.follows) copy.classList.toggle("hidden", wrapper.classList.contains("hidden"));
  refreshInspectorEntityRows();
  for (const name of INSPECTOR_SECTIONS) ui.sections[name].hidden = !widget || !inspectorSectionHasField(ui.sections[name]);
  ui.position.hidden = !widget;
  refreshInspectorSearchButtons(ui.root.closest("aside"));
  scheduleInspectorReflow();
}
