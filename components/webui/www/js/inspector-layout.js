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
  const section = (name, labelKey = `layout.inspector.section_${name}`) => {
    const node = make("details", "inspector-sec"); node.dataset.section = name; node.dataset.label = labelKey; node.open = true;
    const body = make("div", "inspector-sec-body"), summary = make("summary", "inspector-sec-title");
    // The section's hints open from an info button behind its title instead of a "Hints" row in the card.
    const help = createInspectorInfoButton(); help.classList.add("inspector-sec-help"); help.hidden = true;
    help.onclick = event => {
      event.preventDefault(); event.stopPropagation();
      openInspectorHelp(summary.firstChild.textContent, null, { sections: inspectorSectionHelpRows(node).map(row => ({
        title: row.dataset.genericHint === "true" ? "" : row.querySelector(".inspector-help-caption").textContent,
        source: row.querySelector(".inspector-help-content") })) });
    };
    summary.append(make("span", "inspector-sec-label"), help);
    node.append(summary, body);
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
  const counterHelp = createInspectorHelpRow(el.energyTimelineHint, t("layout.inspector.counter_help"));
  el.energyTimelineFields.append(counterHelp);
  const etlStyle = make("div", "slider-options hidden");
  etlStyle.append(label(el.fEtlStyle));
  const entityRows = [];
  // Every entity field is a row with the role and the entity's name; a click opens the entity search. Widget entities
  // are mostly set when the widget is added, so the name is what matters and the ID is in the tooltip.
  const entityRow = (input, open, shorten = false) => {
    const field = label(input), row = input.closest(".entity-input-row") || input;
    const button = make("button", "inspector-entity-row"); button.type = "button";
    button.append(make("span", "inspector-entity-name"), make("span", "inspector-entity-chevron"));
    button.lastChild.setAttribute("aria-hidden", "true");
    button.onclick = () => { if (!input.readOnly) open(); };
    // The row shows the role before any parenthesis; "(optional)" and similar notes move to its tooltip.
    const caption = field.querySelector("span"), role = make("span", "inspector-entity-role");
    caption.hidden = true; field.prepend(role);
    field.classList.add("inspector-entity-label"); row.hidden = true; row.classList.add("hidden"); row.after(button);
    field.addEventListener("click", event => {
      if (event.target.closest("button, a, .field-hint, .meta")) return;
      event.preventDefault(); button.click();
    });
    input.addEventListener("change", () => refreshInspectorEntityRows());
    entityRows.push({ input, button, field, caption, role, shorten });
  };
  for (const input of document.querySelectorAll("#inspectorSection .entity-input-row :is(input[type=text], select)")) {
    const search = input.closest(".entity-input-row").querySelector("[data-inspector-entity-target]");
    if (search) entityRow(input, () => search.click(), true);
  }
  for (const input of document.querySelectorAll("#energyPageOptions input[type=text]")) entityRow(input, () => openPageSensorPicker(input));
  for (const [input, kind] of [[el.settingsTopbarWeatherEntity, "weather"], [el.settingsSaverWeatherEntity, "weather"],
    [el.settingsTopbarStockEntity, "stock"]]) if (input) entityRow(input, () => openSettingsTopbarEntityPicker(kind, input));
  // The page inspector has the same form: a header with the kind of page and its title edited in place, then Entities
  // and Display (Energy dashboard), Rooms (Room overview) and Navigation, the page's counterpart of Placement.
  const pageRoot = byId("pageInspectorSection"), pageForm = pageRoot.querySelector(".form");
  const pageHead = make("div", "inspector-head"), titleRow = label(el.pageTitleInput);
  el.pageTitleInput.classList.add("inspector-name-input");
  el.pageTitleInput.addEventListener("change", applyPageName);
  el.pageTitleInput.addEventListener("keydown", event => { if (event.key === "Enter") { event.preventDefault(); el.pageTitleInput.blur(); } });
  pageHead.append(el.pageTitleInput, byId("pageNavWarning")); titleRow.hidden = true;
  pageRoot.querySelector(".section-body").prepend(pageHead);
  // Its text is the blank page's overview in the heading's info popup.
  byId("pageInspectorHint").remove();
  const PAGE_SECTIONS = ["page_entities", "page_display", "page_rooms", "page_navigation"];
  for (const [name, key] of [["page_entities", "layout.inspector.section_entities"], ["page_display", "layout.inspector.section_display"],
    ["page_rooms", "layout.inspector.section_rooms"], ["page_navigation", "layout.inspector.section_navigation"]]) section(name, key);
  pageForm.prepend(...PAGE_SECTIONS.map(name => sections[name]));
  move("page_navigation", label(byId("pageIconSelect")), byId("pageIconHint"));
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
  follow(energyOptions, [label(el.energySource), byId("energyManualOptions"), label(byId("energySolarPower")), pageBattery, meters, pageHelp], "page_entities");
  follow(energyOptions, [label(byId("energyDetails"))], "page_display");
  const houseOptions = byId("housePageOptions");
  follow(houseOptions, [byId("houseInfo"), byId("houseImportBtn"), byId("houseStatus"), byId("houseRoomList")], "page_rooms");
  // Entities: what the tile reads or controls, including where its data comes from.
  move("entities", byId("entitySourceHint"), byId("entitySourceWarning"), label(el.fEntity), label(el.fSecondaryEntity), label(el.fTertiaryEntity), byId("parqetEntities"));
  follow(byId("vacuumOptions"), [label(el.fVacuumBattery)], "entities");
  follow(byId("roomEntitiesFields"), [label(el.fRoomEntities), byId("roomEntityList"), byId("roomEntityAdd"), byId("fRoomEntitiesHint")], "entities");
  move("entities", byId("carExtraFields"), byId("evccFields"), byId("bambuFields"), byId("energyTimelineFields"), byId("stockEntities"), byId("sensorListEntities"));
  follow(byId("solarForecastFields"), [byId("fSolarForecastTodayLabel"), byId("fSolarForecastTomorrowLabel"),
    byId("fSolarForecastDay3Label"), byId("fSolarForecastDay4Label"), byId("fSolarForecastDay5Label"), byId("fSolarForecastDay6Label"), byId("fSolarForecastDay7Label")].map(label), "entities");
  follow(byId("sliderOptions"), [label(el.fSliderEntityDomain)], "entities");
  follow(byId("graphOptions"), [label(el.fGraphHaHistory), byId("fGraphHaHistoryHint")], "entities");
  follow(byId("priceOptions"), [label(el.fPriceAverage)], "entities");
  move("entities", byId("optionalEntityControls"));
  // Display: what the tile shows.
  move("display", label(el.fShowName), byId("binaryOptions"));
  follow(byId("roomEntitiesFields"), [label(el.fRoomExtraMode), byId("fRoomExtraEntityWrap"), byId("fRoomExtraHint"), byId("fRoomExtraWarning")], "display");
  follow(byId("solarForecastFields"), [label(el.fSolarForecastBarMax)], "display");
  follow(byId("buttonOptions"), [label(el.fButtonAccentColor)], "display");
  move("display", byId("sliderOptions"), byId("graphOptions"), byId("solarForecastStyleOptions"), byId("gaugeOptions"),
    byId("sensorOptions"), byId("cameraOptions"));
  follow(byId("priceOptions"), [label(el.fPriceCheapTimes), label(el.fPriceLine)], "display");
  move("display", byId("evccHeaderOptions"));
  move("display", byId("stockOptions"), byId("parqetOptions"), byId("forecastReadabilityOptions"), byId("weatherForecastOptions"), byId("weatherNowOptions"),
    byId("heatingOptions"), etlStyle, byId("energyTimelineColorOptions"));
  // Controls: what tap and long press do.
  move("controls", byId("historyOptions"), byId("lockOptions"), byId("alarmOptions"), byId("buttonOptions"));
  follow(byId("priceOptions"), [label(el.fPriceSelector)], "controls");
  follow(byId("parqetOptions"), [label(el.fParqetSelector)], "controls");
  follow(byId("stockOptions"), [el.fStockSelectorRow], "controls");
  follow(byId("roomEntitiesFields"), [label(el.fRoomControls), byId("fRoomControlsHint")], "controls");
  follow(byId("vacuumOptions"), [label(el.fVacuumDefaultMode), byId("fVacuumDefaultModeHint")], "controls");
  // The old groups are emptied; Placement is a section like the others, collapsed, at the end.
  const placement = section("placement", "layout.inspector.placement");
  bodies.placement.append(...position.querySelector(".inspector-group-body").children);
  for (const group of form.querySelectorAll(":scope > .inspector-group")) group.hidden = true;
  form.append(placement);
  inspectorLayout = { root, head, sections, pageSections: PAGE_SECTIONS, pageRoot, follows, position: placement, placementChoice: null, counterHelp, etlStyle, entityRows,
    pageBattery, batteryManual, meters, pageHelp, page: null, widget: null, frame: null };
  // Placement starts folded (the canvas moves and sizes a widget by mouse); a click on its heading opens it until the next widget.
  placement.querySelector(":scope > summary").addEventListener("click", event => {
    if (event.target.closest("button")) return;
    event.preventDefault(); inspectorLayout.placementChoice = !placement.open; placement.open = inspectorLayout.placementChoice;
  });
  root.closest("aside").addEventListener("toggle", scheduleInspectorReflow, true);
  window.addEventListener("resize", scheduleInspectorReflow);
  refreshInspectorLayout();
}

function scheduleInspectorReflow() {
  if (!inspectorLayout || inspectorLayout.frame !== null) return;
  inspectorLayout.frame = requestAnimationFrame(() => {
    const ui = inspectorLayout; ui.frame = null;
    const aside = ui.root.closest("aside");
    const available = Math.max(180, window.innerHeight - Math.max(18, aside.getBoundingClientRect().top) - 18);
    aside.style.setProperty("--inspector-available", available + "px");
    if (!ui.position.hidden) ui.position.open = ui.placementChoice === true;
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
  // Only the widget's visible rows share a shortened name, and only its entities are known to the editor's list.
  const widgetRows = inspectorLayout.entityRows.filter(row => row.shorten && inspectorNodeShown(row.field, row.field.closest(".inspector-sec")));
  const shortNames = inspectorEntityRowNames(widgetRows.map(row => row.input.value.trim()));
  for (const row of inspectorLayout.entityRows) {
    const id = row.input.value.trim(), index = widgetRows.indexOf(row);
    const name = (index >= 0 ? shortNames[index] : inspectorEntityRowNames([id])[0]).replace(/\s+/g, " ").trim();
    const known = !id || editor.entities.some(item => item?.id === id) || editor.states?.has(id) || !entityMissingFromSearch(id);
    row.button.querySelector(".inspector-entity-name").textContent = !id ? t("layout.inspector.entity_unset") : name || id;
    row.button.querySelector(".inspector-entity-chevron").textContent = row.input.readOnly ? "" : id ? "\u203A" : "+";
    row.button.classList.toggle("is-empty", !id);
    row.button.classList.toggle("is-missing", !known);
    row.button.title = !id ? "" : known ? id : `${id} \u00B7 ${t("layout.inspector.entity_missing")}`;
    row.button.disabled = row.input.disabled || row.input.readOnly;
    const caption = row.caption.textContent.trim() || t("layout.inspector.entity");
    row.role.textContent = caption.replace(/\s*\(.*$/, "");
    row.role.title = row.role.textContent === caption ? "" : caption;
    row.button.setAttribute("aria-label", `${caption}: ${row.button.querySelector(".inspector-entity-name").textContent}`);
  }
}

// A section is shown while one of its fields is: a field counts unless it or a container up to the section is hidden.
function inspectorSectionHasField(section) {
  const hidden = node => node.hidden || node.classList.contains("hidden");
  const visible = node => {
    for (let at = node; at && at !== section; at = at.parentElement) if (hidden(at)) return false;
    return true;
  };
  return Array.from(section.querySelectorAll(".inspector-sec-body :is(label, details, button, select)"))
    .some(node => !node.closest(".inspector-help-row") && visible(node));
}

function inspectorSectionHelpRows(section) {
  return Array.from(section.querySelectorAll(".inspector-sec-body .inspector-help-row"))
    .filter(row => inspectorNodeShown(row, section));
}

function refreshInspectorSectionHelp() {
  if (!inspectorLayout) return;
  for (const node of Object.values(inspectorLayout.sections)) {
    const help = node.querySelector(":scope > summary > .inspector-sec-help"), hidden = !inspectorSectionHelpRows(node).length;
    if (help.hidden !== hidden) help.hidden = hidden;
    help.title = t("inspector.help"); help.setAttribute("aria-label", `${t("inspector.help")}: ${node.querySelector(".inspector-sec-label").textContent}`);
  }
}

function refreshInspectorLayout() {
  if (!inspectorLayout) return;
  const ui = inspectorLayout, widget = selectedWidget();
  if (ui.page !== editor.selectedPageId) {
    ui.page = editor.selectedPageId;
    for (const group of [ui.pageBattery, ui.meters]) group.open = false;
    ui.root.closest("aside").scrollTop = 0;
  }
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
    ui.widget = widget?.id;
    for (const group of ui.root.querySelectorAll("details:not(.inspector-group):not(.inspector-sec)")) group.open = false;
    ui.placementChoice = null;
    ui.root.closest("aside").scrollTop = 0;
  }
  const heading = document.getElementById("inspectorHeading");
  if (heading && widget) heading.textContent = widgetTypeLabel(widget.type);
  // Alarm, Lock and Vacuum open their history with a long press, since a tap operates them.
  const historyLabel = document.getElementById("fHistoryPopupLabel");
  if (historyLabel) historyLabel.textContent = t(["alarm_tile", "lock_tile", "vacuum_tile"].includes(widget?.type)
    ? "layout.inspector.history_long_press" : "layout.inspector.history_popup");
  ui.head.hidden = !widget;
  for (const node of Object.values(ui.sections)) node.querySelector(":scope > summary > .inspector-sec-label").textContent = t(node.dataset.label);
  const position = ui.position.querySelector(":scope > summary");
  position.querySelector("b")?.remove();
  if (widget?.rect) {
    const values = document.createElement("b");
    values.textContent = `${widget.rect.x} \u00B7 ${widget.rect.y} \u00B7 ${widget.rect.w} \u00D7 ${widget.rect.h}`;
    position.append(values);
  }
  setInspectorHelpCaption(ui.counterHelp, t("layout.inspector.counter_help"));
  ui.etlStyle.classList.toggle("hidden", widget?.type !== "energy_timeline");
  if (widget?.type === "gauge") {
    const extras = ["radial", "horizontal"].includes(el.fGaugeStyle.value);
    // Presentation only: preserve configured extras when temporarily choosing another shape.
    el.fSecondaryEntityWrap.classList.toggle("hidden", !extras);
    el.fTertiaryEntityWrap.classList.toggle("hidden", !extras);
  }
  // A car whose extras are bound on a tile too small for them (w_car_widget.c: 200 x 330 px): say so beside the bindings,
  // not only in the section's info popup; the extras were missed on the panel otherwise.
  let carNote = ui.sections.entities.querySelector("#carExtrasSizeNote");
  if (!carNote) {
    carNote = document.createElement("div");
    carNote.id = "carExtrasSizeNote";
    carNote.className = "meta inspector-size-note";
    ui.sections.entities.querySelector(".inspector-sec-body").append(carNote);
  }
  const carExtras = widget?.type === "car_widget" && [widget.precond_entity_id, widget.lock_entity_id, widget.interior_temp_entity_id].some(Boolean);
  const carShort = carExtras && (Number(widget.rect?.w) < 200 || Number(widget.rect?.h) < 330);
  carNote.hidden = !carShort;
  carNote.textContent = carShort ? t("layout.inspector.car_extras_size", { w: widget.rect.w, h: widget.rect.h }) : "";
  for (const { wrapper, copy } of ui.follows) copy.classList.toggle("hidden", wrapper.classList.contains("hidden"));
  // A long caption beside a short field leaves neither enough room: it goes above, and the field takes the full width.
  for (const root of [ui.root, ui.pageRoot]) {
    for (const label of root.querySelectorAll(".inspector-sec label:not(.row):has(> select:only-of-type, > input[type=number], > input[type=color])")) {
      label.classList.toggle("inspector-label-stacked", (label.querySelector(":scope > span")?.textContent.trim().length || 0) > 22);
    }
  }
  refreshOptionalEntities();
  refreshInspectorEntityRows();
  for (const name of INSPECTOR_SECTIONS) ui.sections[name].hidden = !widget || !inspectorSectionHasField(ui.sections[name]);
  for (const name of ui.pageSections) ui.sections[name].hidden = !selectedPage() || !inspectorSectionHasField(ui.sections[name]);
  const pageHeading = document.getElementById("pageInspectorHeading"), page = selectedPage();
  if (pageHeading && page) pageHeading.textContent = t(isHousePage(page) ? "layout.pages.menu_house" : isEnergyPage(page) ? "layout.pages.menu_energy" : "layout.pages.type_normal");
  refreshInspectorSectionHelp();
  ui.position.hidden = !widget;
  refreshInspectorSearchButtons(ui.root.closest("aside"));
  scheduleInspectorReflow();
}
