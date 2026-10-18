/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Pages list, canvas and previews, inspector, page and widget operations
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
function renderEntityOptions() {
  const inspectorType = inspectorWidgetType();
  const sliderDomain = inspectorSliderEntityDomain();
  const buttonMode = inspectorButtonMode();
  const secondaryConfig = secondaryEntityConfigForWidgetType(inspectorType);
  const tertiaryConfig = tertiaryEntityConfigForWidgetType(inspectorType);
  const inspectorOptions = listEntitiesForWidgetType(inspectorType, sliderDomain, buttonMode);
  const primaryOptions = inspectorOptions.length > 0 ? inspectorOptions : editor.entities;
  setEntityOptionsList(el.entityOptions, primaryOptions.slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS));

  setEntityOptionsList(
    el.sensorEntityOptions,
    (secondaryConfig.domain || inspectorType === "solar_forecast")
      ? applyEntityClassRule(listEntitiesByDomain(inspectorType === "solar_forecast" ? "sensor" : secondaryConfig.domain), inspectorType, "secondary").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS)
      : [],
  );
  setEntityOptionsList(
    document.getElementById("tertiaryEntityOptions"),
    tertiaryConfig.enabled ? applyEntityClassRule(listEntitiesByDomain(tertiaryConfig.domain), inspectorType, "tertiary").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS) : [],
  );
  setEntityOptionsList(
    document.getElementById("carPrecondOptions"),
    ["switch", "input_boolean", "climate", "button", "input_button", "select"]
      .flatMap((domain) => listEntitiesByDomain(domain)).slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
  );
  setEntityOptionsList(
    document.getElementById("carLockOptions"),
    ["lock", "binary_sensor"].flatMap((domain) => listEntitiesByDomain(domain)).slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
  );
  bambuFillOptions(); evccFillOptions();
  if (el.energyEntityOptions) {
    setEntityOptionsList(el.energyEntityOptions, listEntitiesByDomain("sensor").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS));
  }
  if (el.houseSensorOptions) {
    setEntityOptionsList(el.houseSensorOptions, listEntitiesByDomain("sensor").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS));
  }
  if (el.settingsWeatherEntityOptions) {
    setEntityOptionsList(
      el.settingsWeatherEntityOptions,
      listEntitiesByDomain("weather").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
    );
  }
  if (el.settingsStockEntityOptions) {
    setEntityOptionsList(
      el.settingsStockEntityOptions,
      listEntitiesByDomain("sensor").slice(0, ENTITY_AUTOCOMPLETE_MAX_ITEMS),
    );
  }

  if (el.fSecondaryEntityLabel) {
    const primaryLabelKey = inspectorType === "solar_forecast"
      ? "layout.inspector.solar_remaining_entity"
      : inspectorType === "energy_timeline"
        ? "layout.inspector.entity_energy_timeline"
        : inspectorType === "parqet_portfolio"
          ? "layout.inspector.parqet_total_value"
          : "layout.inspector.entity";
    setTextById("fEntityLabel", primaryLabelKey);
    el.fSecondaryEntityLabel.textContent = t(secondaryConfig.labelKey, {}, secondaryConfig.labelFallback);
  }
  if (el.weatherReportHint) {
    el.weatherReportHint.classList.toggle("hidden", inspectorType !== "weather_3day");
    el.weatherReportHint.textContent = t("layout.inspector.weather_report_hint");
  }
  if (el.sonosFavoritesHint) {
    el.sonosFavoritesHint.classList.toggle("hidden", inspectorType !== "sonos_favorites");
    el.sonosFavoritesHint.textContent = t("layout.inspector.sonos_favorites_hint");
  }
  if (el.fTertiaryEntityLabel) {
    el.fTertiaryEntityLabel.textContent = t(tertiaryConfig.labelKey, {}, tertiaryConfig.labelFallback);
  }

  const secondaryEnabled = secondaryConfig.enabled;
  if (el.fSecondaryEntityWrap) {
    el.fSecondaryEntityWrap.classList.toggle("hidden", !secondaryEnabled);
  }
  el.fSecondaryEntity.disabled = !secondaryEnabled;
  if (!secondaryEnabled) {
    el.fSecondaryEntity.value = "";
  }

  const tertiaryEnabled = tertiaryConfig.enabled;
  if (el.fTertiaryEntityWrap) {
    el.fTertiaryEntityWrap.classList.toggle("hidden", !tertiaryEnabled);
  }
  if (el.fTertiaryEntity) {
    el.fTertiaryEntity.disabled = !tertiaryEnabled;
    if (!tertiaryEnabled) {
      el.fTertiaryEntity.value = "";
    }
  }

  const primaryEnabled = inspectorType !== "empty_tile";
  if (el.fEntityWrap) {
    el.fEntityWrap.classList.toggle("hidden", !primaryEnabled || inspectorType === "wallbox_evcc");
  }
  if (el.fEntity) {
    el.fEntity.disabled = !primaryEnabled;
    if (!primaryEnabled) {
      el.fEntity.value = "";
    }
  }

  document.querySelectorAll("[data-inspector-entity-target]").forEach((button) => {
    const target = inspectorEntityPickerTarget(button.dataset.inspectorEntityTarget || "");
    button.disabled = !target;
  });
}

// Drag and drop reordering of the pages list: the order is the order of the navigation bar.
let draggedPageId = null;

function attachPageDrag(li, page) {
  li.draggable = true;
  li.classList.add("draggable");
  li.addEventListener("dragstart", (event) => {
    draggedPageId = page.id;
    event.dataTransfer.effectAllowed = "move";
    try { event.dataTransfer.setData("text/plain", page.id); } catch (_) { /* ignore */ }
    li.classList.add("dragging");
  });
  li.addEventListener("dragend", () => {
    draggedPageId = null;
    document.querySelectorAll("#pagesList .drop-before, #pagesList .drop-after, #pagesList .dragging")
      .forEach((node) => node.classList.remove("drop-before", "drop-after", "dragging"));
  });
  li.addEventListener("dragover", (event) => {
    if (!draggedPageId || draggedPageId === page.id) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "move";
    const rect = li.getBoundingClientRect();
    const after = event.clientY > rect.top + rect.height / 2;
    li.classList.toggle("drop-after", after);
    li.classList.toggle("drop-before", !after);
  });
  li.addEventListener("dragleave", () => li.classList.remove("drop-before", "drop-after"));
  li.addEventListener("drop", (event) => {
    event.preventDefault();
    const after = li.classList.contains("drop-after");
    li.classList.remove("drop-before", "drop-after");
    const pages = editor.layout.pages;
    const from = pages.findIndex((p) => p.id === draggedPageId);
    if (from < 0 || draggedPageId === page.id) return;
    layoutHistoryBoundary();
    const [moved] = pages.splice(from, 1);
    let to = pages.findIndex((p) => p.id === page.id);
    if (after) to += 1;
    pages.splice(to, 0, moved);
    draggedPageId = null;
    renderAll();
  });
}

function renderPages() {
  el.pagesList.innerHTML = "";
  for (const page of editor.layout.pages) {
    const li = document.createElement("li");
    li.className = `list-item ${page.id === editor.selectedPageId ? "active selected" : ""}`;
    attachPageDrag(li, page);

    const label = document.createElement("span");
    const badge = pageIconEmoji(page);
    label.textContent = `${badge ? badge + " " : ""}${page.title || page.id}`;
    label.title = `[${page.id}] ${isEnergyPage(page) ? "energy" : (isHousePage(page) ? "house" : "page")}`;
    label.className = "list-item-label";
    li.appendChild(label);
    li.onclick = () => {
      editor.selectedPageId = page.id;
      editor.selectedWidgetId = null;
      renderAll();
    };

    const actions = document.createElement("span");
    actions.className = "row-actions";

    const renameBtn = document.createElement("button");
    renameBtn.type = "button";
    renameBtn.className = "row-icon-btn";
    renameBtn.title = t("layout.pages.rename");
    renameBtn.textContent = "✎";
    renameBtn.onclick = (ev) => {
      ev.stopPropagation();
      startInlinePageRename(li, label, page);
    };
    actions.appendChild(renameBtn);

    if (!isFixedPage(page)) {
      const dupBtn = document.createElement("button");
      dupBtn.type = "button";
      dupBtn.className = "row-icon-btn";
      dupBtn.title = t("layout.pages.duplicate");
      dupBtn.textContent = "⧉";
      dupBtn.setAttribute("aria-label", t("layout.pages.duplicate"));
      dupBtn.onclick = (ev) => {
        ev.stopPropagation();
        duplicatePage(page);
      };
      actions.appendChild(dupBtn);
    }

    const delBtn = document.createElement("button");
    delBtn.type = "button";
    delBtn.className = "row-icon-btn row-delete-btn";
    delBtn.title = t("layout.pages.delete") || "Delete";
    delBtn.textContent = "✕";
    delBtn.setAttribute("aria-label", t("layout.pages.delete") || "Delete");
    delBtn.onclick = (ev) => {
      ev.stopPropagation();
      editor.selectedPageId = page.id;
      deletePage();
    };
    actions.appendChild(delBtn);

    li.appendChild(actions);
    el.pagesList.appendChild(li);
  }
  renderPagesMini();
}

function startInlinePageRename(li, labelSpan, page) {
  if (!li || !page) return;
  const input = document.createElement("input");
  input.type = "text";
  input.className = "row-rename-input";
  input.value = page.title || page.id;
  input.maxLength = 63;

  const commit = (save) => {
    if (save) {
      const next = input.value.trim();
      page.title = next || page.id;
      if (isEnergyPage(page)) {
        applyEnergyPageConfig({ render: false });
      }
    }
    renderAll();
  };

  input.onkeydown = (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      commit(true);
    } else if (e.key === "Escape") {
      e.preventDefault();
      commit(false);
    }
  };
  input.onblur = () => commit(true);

  li.replaceChild(input, labelSpan);
  input.focus();
  input.select();
}

function renderPagesMini() {
  if (!el.pagesMiniList) return;
  el.pagesMiniList.innerHTML = "";
  for (const page of editor.layout.pages) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `mini-page-btn ${page.id === editor.selectedPageId ? "active" : ""}`;
    const badge = pageIconEmoji(page);
    button.textContent = `${badge ? badge + " " : ""}${page.title || page.id}`;
    button.title = `${page.title || page.id} [${page.id}]`;
    button.onclick = () => {
      editor.selectedPageId = page.id;
      editor.selectedWidgetId = null;
      renderAll();
    };
    el.pagesMiniList.appendChild(button);
  }
}

// Maps a widget's `type` value to the i18n key of its "+ <Name>" Add-menu label (the only place
// each type already has a translated display name), used to build the inspector's live heading.
const WIDGET_TYPE_LABEL_KEYS = {
  sensor: "layout.widgets.add_sensor",
  button: "layout.widgets.add_button",
  slider: "layout.widgets.add_slider",
  gauge: "layout.widgets.add_gauge",
  graph: "layout.widgets.add_graph",
  empty_tile: "layout.widgets.add_empty_tile",
  light_tile: "layout.widgets.add_light_tile",
  heating_tile: "layout.widgets.add_heating_tile",
  weather_tile: "layout.widgets.add_weather_tile",
  weather_3day: "layout.widgets.add_weather_3day",
  solar_forecast: "layout.widgets.add_solar_forecast",
  car_widget: "layout.widgets.add_car",
  bambu_printer: "layout.widgets.add_bambu_printer",
  wallbox_evcc: "evcc.wallbox",
  todo_list: "layout.widgets.add_todo",
  media_player: "layout.widgets.add_media_player",
  sonos_favorites: "layout.widgets.add_sonos_favorites",
  vacuum_tile: "layout.widgets.add_vacuum",
  cover_tile: "layout.widgets.add_cover",
  climate_tile: "layout.widgets.add_climate",
  room_tile: "layout.widgets.add_room",
  scene_tile: "layout.widgets.add_scene",
  select_tile: "layout.widgets.add_select",
  energy_timeline: "layout.widgets.add_energy_timeline",
  price_forecast: "layout.widgets.add_price_forecast",
  parqet_portfolio: "layout.widgets.add_parqet_portfolio",
  datetime_tile: "layout.widgets.add_datetime",
  lock_tile: "layout.widgets.add_lock",
  alarm_tile: "layout.widgets.add_alarm",
  binary_tile: "layout.widgets.add_binary",
  contact_tile: "layout.widgets.add_contact",
  camera_tile: "layout.widgets.add_camera",
  fan_tile: "layout.widgets.add_fan",
};

function widgetTypeLabel(type) {
  const key = WIDGET_TYPE_LABEL_KEYS[type];
  // Every entry above is "+ <Name>"; strip the Add-menu's own "+ " prefix for reuse in a heading.
  return key ? t(key).replace(/^\+\s*/, "") : type || "";
}

// The right column shows the page settings while no widget is selected, and the widget inspector otherwise.
function renderPageIconOptions() {
  const select = document.getElementById("pageIconSelect");
  if (!select) return;
  const current = select.value;
  select.replaceChildren();
  const add = (value, text) => {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = text;
    select.appendChild(option);
  };
  add("", t("page_icon.auto"));
  add("none", t("page_icon.none"));
  for (const item of PAGE_ICONS) add(item.key, `${item.emoji} ${t(`page_icon.${item.key}`)}`);
  select.value = current;
}

function updateInspectorPanels() {
  const widget = selectedWidget();
  const noWidget = !widget;
  const pageSection = document.getElementById("pageInspectorSection");
  const widgetSection = document.getElementById("inspectorSection");
  if (pageSection) pageSection.classList.toggle("hidden", !noWidget);
  if (widgetSection) widgetSection.classList.toggle("hidden", noWidget);
  const heading = document.getElementById("pageInspectorHeading");
  const page = selectedPage();
  const iconSelect = document.getElementById("pageIconSelect");
  if (iconSelect && page) iconSelect.value = typeof page.icon === "string" ? page.icon : "";
  const navWarning = document.getElementById("pageNavWarning");
  if (navWarning) {
    const index = page ? editor.layout.pages.indexOf(page) : -1;
    const issue = index >= 0 ? navIconIssue(editor.layout, index, fullNavPages()) : "";
    navWarning.hidden = issue === "";
    navWarning.textContent = issue ? t(`layout.pages.nav_icon_${issue}`, { max: fullNavPages() }) : "";
  }
  if (heading) heading.textContent = t("layout.pages.inspector_heading", { title: page ? (page.title || page.id) : "" });
  const hint = document.getElementById("pageInspectorHint");
  if (hint) hint.hidden = !!page && isFixedPage(page); /* energy and house pages have no tiles to click */
  // Kept in sync with the current selection (not just set once) and left visible while scrolling
  // through a long field list below it, so a tile that looks like several others is still
  // identifiable without scrolling back up to the canvas.
  const widgetHeading = document.getElementById("inspectorHeading");
  if (widgetHeading) {
    widgetHeading.textContent = widget
      ? t("layout.widgets.inspector_heading", { type: widgetTypeLabel(widget.type), title: widget.title || widget.id })
      : t("layout.inspector.heading");
  }
}

function deselectWidget() {
  if (!editor.selectedWidgetId) return;
  editor.selectedWidgetId = null;
  renderWidgets();
  renderInspector();
  renderCanvas();
}

function renderPageEditor() {
  updateInspectorPanels();
  const page = selectedPage();
  if (!page) {
    el.pageTitleInput.value = "";
    el.pageTitleInput.disabled = true;
    el.applyPageBtn.disabled = true;
    if (el.energyPageOptions) {
      el.energyPageOptions.classList.add("hidden");
    }
    if (el.housePageOptions) {
      el.housePageOptions.classList.add("hidden");
    }
    return;
  }
  el.pageTitleInput.disabled = false;
  el.applyPageBtn.disabled = false;
  el.pageTitleInput.value = page.title || page.id;

  const energyPage = isEnergyPage(page);
  if (el.energyPageOptions) {
    el.energyPageOptions.classList.toggle("hidden", !energyPage);
  }
  if (el.housePageOptions) {
    el.housePageOptions.classList.toggle("hidden", !isHousePage(page));
  }
  if (isHousePage(page)) {
    normalizeHouseConfig(page);
    renderHouseOptions(page);
  }
  if (energyPage) {
    normalizeEnergyConfig(page);
    if (el.energySource) {
      el.energySource.value = page.energy.source || ENERGY_SOURCE_HA;
    }
    if (el.energyDetails) {
      el.energyDetails.value = page.energy.details === ENERGY_DETAILS_SIDE ? ENERGY_DETAILS_SIDE : "popup";
    }
    updateEnergySourceUi(page.energy.source);
    const inputs = getEnergyInputs();
    for (const key of ENERGY_ENTITY_KEYS) {
      if (inputs[key]) {
        inputs[key].value = page.energy[key] || "";
      }
    }
  }
}

function updateEnergySourceUi(source) {
  const checkedSource = ENERGY_SOURCES.has(source) ? source : ENERGY_SOURCE_HA;
  const isManual = checkedSource === ENERGY_SOURCE_MANUAL;
  if (el.energyManualOptions) {
    el.energyManualOptions.classList.toggle("hidden", !isManual);
  }
  if (el.energySourceHint) {
    el.energySourceHint.textContent = t(isManual ? "layout.energy.source_hint_manual" : "layout.energy.source_hint_ha");
  }
}

// Tracks whose widgets the search box's text currently applies to, so switching pages clears a
// filter that would otherwise silently hide every widget on the newly selected page too.
let widgetFilterPageId = null;

function renderWidgets() {
  const page = selectedPage();
  el.widgetsList.innerHTML = "";
  if (!page) return;

  if (page.id !== widgetFilterPageId) {
    widgetFilterPageId = page.id;
    editor.widgetFilter = "";
    if (el.widgetSearchInput) el.widgetSearchInput.value = "";
  }

  const energyPage = isFixedPage(page);
  const addButtons = [
    el.addSensorBtn,
    el.addButtonBtn,
    el.addSliderBtn,
    el.addGraphBtn,
    el.addEmptyTileBtn,
    el.addLightTileBtn,
    el.addHeatingTileBtn,
    el.addWeatherTileBtn,
    el.addWeather3DayBtn,
    el.addSolarForecastBtn,
    el.addPriceForecastBtn,
    el.addCarWidgetBtn,
    el.addBambuPrinterBtn, el.addEvccWallboxBtn,
    el.addTodoListBtn,
    el.addMediaPlayerBtn,
    el.addSonosFavoritesBtn,
  ];
  for (const button of addButtons) {
    if (button) button.disabled = energyPage;
  }
  if (el.addTileBtn) {
    el.addTileBtn.disabled = energyPage;
  }
  if (el.duplicateWidgetBtn) {
    el.duplicateWidgetBtn.disabled = energyPage || !editor.selectedWidgetId;
  }
  if (el.deleteWidgetBtn) {
    el.deleteWidgetBtn.disabled = energyPage || !editor.selectedWidgetId;
  }

  if (el.widgetSearchInput) {
    el.widgetSearchInput.classList.toggle("hidden", energyPage);
  }

  if (energyPage) {
    const li = document.createElement("li");
    li.className = "list-item muted";
    li.textContent = t(isHousePage(page) ? "house.no_widgets" : "layout.energy.no_widgets");
    el.widgetsList.appendChild(li);
    return;
  }

  const query = editor.widgetFilter.trim().toLowerCase();
  const widgets = query ? page.widgets.filter((widget) => widgetMatchesFilter(widget, query)) : page.widgets;
  if (query && widgets.length === 0) {
    const li = document.createElement("li");
    li.className = "list-item muted";
    li.textContent = t("layout.widgets.search_no_matches", { query: editor.widgetFilter.trim() });
    el.widgetsList.appendChild(li);
    return;
  }

  for (const widget of widgets) {
    const li = document.createElement("li");
    li.className = `list-item ${widget.id === editor.selectedWidgetId ? "active selected" : ""}`;

    const label = document.createElement("span");
    label.className = "list-item-label";
    label.textContent = `${widgetDisplayLabel(widget)}`;
    label.title = `[${widget.id}] ${widget.type}`;
    li.appendChild(label);
    li.onclick = () => {
      editor.selectedWidgetId = widget.id;
      renderAll();
    };

    const actions = document.createElement("span");
    actions.className = "row-actions";
    const actionsBtn = document.createElement("button");
    actionsBtn.type = "button";
    actionsBtn.className = "row-icon-btn widget-actions-toggle";
    actionsBtn.dataset.widgetId = widget.id;
    actionsBtn.title = t("layout.transfer.actions");
    actionsBtn.textContent = "⋯";
    actionsBtn.setAttribute("aria-label", t("layout.transfer.actions"));
    actionsBtn.setAttribute("aria-haspopup", "menu");
    actionsBtn.setAttribute("aria-expanded", "false");
    actionsBtn.onclick = (event) => {
      event.stopPropagation();
      openWidgetActions(page.id, widget.id, actionsBtn);
    };
    li.oncontextmenu = (event) => {
      event.preventDefault();
      openWidgetActions(page.id, widget.id, actionsBtn, { x: event.clientX, y: event.clientY });
    };
    actions.appendChild(actionsBtn);
    const delBtn = document.createElement("button");
    delBtn.type = "button";
    delBtn.className = "row-icon-btn row-delete-btn";
    delBtn.title = t("layout.widgets.delete") || "Delete";
    delBtn.textContent = "✕";
    delBtn.setAttribute("aria-label", t("layout.widgets.delete") || "Delete");
    delBtn.onclick = (ev) => {
      ev.stopPropagation();
      editor.selectedWidgetId = widget.id;
      if (typeof el.deleteWidgetBtn?.click === "function") {
        el.deleteWidgetBtn.click();
      }
    };
    actions.appendChild(delBtn);
    li.appendChild(actions);

    el.widgetsList.appendChild(li);
  }
}

function widgetDisplayLabel(widget) {
  const title = (widget.title || "").trim();
  if (title) return `${title} · ${widget.type}`;
  return `${widget.type} [${widget.id}]`;
}

// Widget-list search (item 6 of the WebUI polish pass): a page can hold up to 32 widgets, hard to
// scan by eye once several share a type (e.g. three plain sensors). Matches the same fields a
// maintainer would look at in the list itself or the inspector -- title, type (both the raw value
// and its translated Add-menu name so "Gauge" and "gauge" both find a gauge tile), and the primary
// entity id -- rather than a fuzzy search a 30-line vanilla-JS editor has no good reason to grow.
function widgetMatchesFilter(widget, query) {
  const haystack = [
    widget.title,
    widget.type,
    widgetTypeLabel(widget.type),
    widget.id,
    widget.entity_id,
  ]
    .filter(Boolean)
    .join(" ")
    .toLowerCase();
  return haystack.includes(query);
}

function geometryStyle(node, rect) {
  node.style.left = `${rect.x}px`;
  node.style.top = `${rect.y}px`;
  node.style.width = `${rect.w}px`;
  node.style.height = `${rect.h}px`;
}

function selectWidgetLive(widgetId, selectedBox) {
  editor.selectedWidgetId = widgetId;
  document.querySelectorAll(".widget-box.selected").forEach((node) => node.classList.remove("selected"));
  if (selectedBox) {
    selectedBox.classList.add("selected");
  }
  renderWidgets();
  renderInspector();
}

function attachDragAndResize(box, widget) {
  box.addEventListener("contextmenu", (event) => {
    event.preventDefault();
    openWidgetActions(editor.selectedPageId, widget.id, box, { x: event.clientX, y: event.clientY });
  });
  const startMove = (mode, downEvent, captureTarget) => {
    downEvent.preventDefault();
    downEvent.stopPropagation();
    const startX = downEvent.clientX;
    const startY = downEvent.clientY;
    const startRect = { ...widget.rect };
    const pointerId = downEvent.pointerId;
    let moved = false;
    let finished = false;

    // Capture all subsequent pointer events on this element so they don't get
    // hijacked by native drag, hover over iframes/images, or focus changes.
    try { captureTarget.setPointerCapture(pointerId); } catch (_) { /* ignore */ }

    // Suppress full canvas re-renders triggered by HA state pushes / other
    // async events while the user is interacting with the tile.
    layoutHistoryBoundary();
    editor.canvasInteractionActive = true;
    editor.canvasRenderPending = false;

    box.classList.add(mode === "drag" ? "dragging" : "resizing");

    const onMove = (moveEvent) => {
      if (moveEvent.pointerId !== pointerId) return;
      const scale = editor.canvasScale || 1;
      const dx = (moveEvent.clientX - startX) / scale;
      const dy = (moveEvent.clientY - startY) / scale;
      const limits = widgetSizeLimits(widgetSizeType(widget));
      const maxW = Math.min(limits.maxW, CANVAS_WIDTH);
      const maxH = Math.min(limits.maxH, CANVAS_HEIGHT);
      const minW = Math.min(limits.minW, maxW);
      const minH = Math.min(limits.minH, maxH);
      let nextX = widget.rect.x;
      let nextY = widget.rect.y;
      let nextW = widget.rect.w;
      let nextH = widget.rect.h;

      if (mode === "drag") {
        nextX = clamp(snap(startRect.x + dx), 0, CANVAS_WIDTH - startRect.w);
        nextY = clamp(snap(startRect.y + dy), 0, CANVAS_HEIGHT - startRect.h);
      } else {
        nextW = clamp(snap(startRect.w + dx), minW, Math.min(maxW, CANVAS_WIDTH - startRect.x));
        nextH = clamp(snap(startRect.h + dy), minH, Math.min(maxH, CANVAS_HEIGHT - startRect.y));
      }

      if (nextX !== widget.rect.x || nextY !== widget.rect.y || nextW !== widget.rect.w || nextH !== widget.rect.h) {
        moved = true;
        widget.rect.x = nextX;
        widget.rect.y = nextY;
        widget.rect.w = nextW;
        widget.rect.h = nextH;
        if (nextW !== startRect.w) autoGaugeName(widget);
        geometryStyle(box, widget.rect);
        renderInspector();
      }
    };

    const cleanup = () => {
      if (finished) return;
      finished = true;
      captureTarget.removeEventListener("pointermove", onMove);
      captureTarget.removeEventListener("pointerup", onUp);
      captureTarget.removeEventListener("pointercancel", onUp);
      captureTarget.removeEventListener("lostpointercapture", onUp);
      try { captureTarget.releasePointerCapture(pointerId); } catch (_) { /* ignore */ }
      box.classList.remove("dragging");
      box.classList.remove("resizing");
      editor.canvasInteractionActive = false;
      const hadPendingRender = editor.canvasRenderPending;
      editor.canvasRenderPending = false;
      if (moved) {
        renderWidgets();
        renderInspector();
        markDirty(); // a drag/resize never goes through renderAll(), which would otherwise catch this
      }
      if (hadPendingRender) {
        // Flush any canvas updates that async events requested while we were
        // dragging (e.g. HA state pushes). The geometry is already live via
        // geometryStyle(), so this only matters for content changes.
        renderCanvas();
      }
    };

    const onUp = (upEvent) => {
      if (upEvent && upEvent.pointerId !== undefined && upEvent.pointerId !== pointerId) return;
      cleanup();
    };

    captureTarget.addEventListener("pointermove", onMove);
    captureTarget.addEventListener("pointerup", onUp);
    captureTarget.addEventListener("pointercancel", onUp);
    captureTarget.addEventListener("lostpointercapture", onUp);
  };

  box.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    if (event.target.classList.contains("resize-handle")) return;
    selectWidgetLive(widget.id, box);
    startMove("drag", event, box);
  });

  const resizeHandle = box.querySelector(".resize-handle");
  resizeHandle.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    event.stopPropagation();
    selectWidgetLive(widget.id, box);
    startMove("resize", event, resizeHandle);
  });
}

function escapeHtml(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

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

function renderCanvas() {
  if (editor.activePane === "settings") {
    setActiveSettingsSection(editor.activeSettingsSection);
    return;
  }
  updateCanvasScale();
  el.canvas.classList.toggle("panel4-readable", panel4Typography());
  // While the user is actively dragging or resizing a tile we must not rebuild
  // the canvas DOM — doing so would destroy the element that owns the pointer
  // capture and cause the drag to "let go" mid-motion. Remember that a render
  // was requested and replay it once the interaction ends.
  if (editor.canvasInteractionActive) {
    editor.canvasRenderPending = true;
    return;
  }
  const page = selectedPage();
  el.canvas.innerHTML = "";
  if (!page) {
    el.canvasTitle.textContent = t("layout.canvas.title");
    return;
  }

  el.canvasTitle.textContent = `${t("layout.canvas.title")}: ${page.title || page.id}`;

  if (isEnergyPage(page)) {
    renderEnergyCanvasPreview(page);
    return;
  }
  if (isHousePage(page)) {
    renderHouseCanvasPreview(page);
    return;
  }
  if (!page.widgets.length) renderEmptyPageHint(page);

  for (const widget of page.widgets) {
    const box = document.createElement("div");
    const isEmptyTile = widget.type === "empty_tile";
    const isMediaPlayerButton = widget.type === "button" && String(widget.entity_id || "").startsWith("media_player.");
    const previewTitle = (isMediaPlayerButton && !String(widget.title || "").trim()) ? "" : (widget.title || widget.id);
    box.className = `widget-box ${isEmptyTile ? "empty-tile" : ""} ${widget.id === editor.selectedWidgetId ? "selected" : ""}`;
    if ((Number(widget.rect && widget.rect.w) || 0) >= 260) box.classList.add("matte-wide");
    box.classList.toggle("name-hidden", !widgetShowsName(widget));
    box.dataset.widgetId = widget.id;
    box.style.zIndex = isEmptyTile ? "1" : "10";
    const previewState = isEmptyTile ? "design" : (editor.states.get(widget.entity_id) || "unavailable");
    box.classList.toggle("matte-on", ["on", "open", "playing", "home"].includes(String(previewState).toLowerCase()));
    let extraHint = "";
    if (widget.type === "weather_3day") {
      /* Mirrors the firmware layout in w_weather_tile.c:
       *   ROWS_TOP=150, BOTTOM_PAD=12, ROW_HEIGHT=44, ROW_GAP=4
       *   visible rows = clamp(floor((h-162+4)/48), 2, 6)
       *   forecast days = visible rows - 1 (the "Now" row).
       * Keep these constants in sync when the tile layout changes. */
      const h = Number(widget.rect && widget.rect.h) || 0;
      const rowsTop = 150;
      const rowHeight = 44;
      const rowGap = 4;
      const avail = h - rowsTop - 12;
      let rows = 2;
      if (avail > rowHeight) {
        rows = Math.floor((avail + rowGap) / (rowHeight + rowGap));
      }
      if (rows < 2) rows = 2;
      if (rows > 6) rows = 6;
      const days = rows - 1;
      extraHint = `<div class="w-hint">forecast days ${days}/5</div>`;
    }
    if (widget.type === "solar_forecast") {
      box.innerHTML = solarPreviewMarkup(widget);
    } else if (widget.type === "car_widget") {
      box.innerHTML = carPreviewMarkup(widget);
    } else if (widget.type === "wallbox_evcc") {
      box.innerHTML = evccPreviewMarkup(widget);
    } else if (widget.type === "bambu_printer") {
      box.innerHTML = bambuPreviewMarkup(widget);
    } else if (widget.type === "sonos_favorites") {
      box.innerHTML = sonosFavoritesPreviewMarkup(widget);
    } else if (tilePreviewMarkup(widget) !== null) {
      box.innerHTML = tilePreviewMarkup(widget);
    } else {
      box.innerHTML = `
        <div class="w-type">${escapeHtml(widget.type)}</div>
        <div class="w-title">${escapeHtml(previewTitle)}</div>
        <div class="w-state">${escapeHtml(previewState)}</div>
        ${extraHint}
        <div class="resize-handle"></div>
      `;
    }
    geometryStyle(box, widget.rect);
    attachDragAndResize(box, widget);
    el.canvas.appendChild(box);
  }
}

// Widgets can be moved to any other regular (non-energy) page.
function populateMovePageOptions() {
  const select = el.fMovePage;
  if (!select) return;
  const current = selectedPage();
  const widget = selectedWidget();
  select.innerHTML = "";
  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.textContent = t("layout.inspector.move_placeholder");
  select.appendChild(placeholder);
  const pages = editor.layout?.pages || [];
  for (const page of pages) {
    if (page === current || isFixedPage(page)) continue;
    const option = document.createElement("option");
    option.value = page.id;
    option.textContent = page.title || page.id;
    select.appendChild(option);
  }
  select.disabled = !widget || !current || isFixedPage(current) || select.options.length < 2;
  select.value = "";
}

function moveSelectedWidgetToPage(targetPageId) {
  const source = selectedPage();
  const widget = selectedWidget();
  const target = (editor.layout?.pages || []).find((p) => p.id === targetPageId);
  if (!source || !widget || !target || target === source) return;
  if (isFixedPage(source) || isFixedPage(target)) return;
  source.widgets = source.widgets.filter((w) => w !== widget);
  const used = new Set(target.widgets.map((w) => w.id));
  let id = widget.id;
  let n = 2;
  while (used.has(id)) {
    id = `${widget.id}_${n++}`;
  }
  widget.id = id;
  widget.rect = clampRectToCanvas(widget.rect, widgetSizeType(widget));
  target.widgets.push(widget);
  editor.selectedPageId = target.id;
  editor.selectedWidgetId = widget.id;
  renderAll();
  setStatus(t("layout.widgets.moved", { page: target.title || target.id }));
}

// alarm_tile: show the layout's alarm_actions (no key: every action is offered)
function alarmLoadInspector(widget) {
  if (el.alarmOptions) el.alarmOptions.classList.toggle("hidden", widget.type !== "alarm_tile");
  for (const [key, action] of ALARM_ACTION_FIELDS) {
    if (el[key]) el[key].checked = !Array.isArray(widget.alarm_actions) || widget.alarm_actions.includes(action);
  }
}

function renderInspector() {
  renderEnergyTimelineColors(selectedWidget());
  updateInspectorPanels();
  const widget = selectedWidget();
  if (!widget) {
    renderReadabilityInspector(null);
    el.fTitle.value = "";
    el.fType.value = "sensor";
    el.fEntity.value = "";
    el.fSecondaryEntity.value = "";
    if (el.fTertiaryEntity) {
      el.fTertiaryEntity.value = "";
    }
    el.fX.value = "";
    el.fY.value = "";
    el.fW.value = "";
    el.fH.value = "";
    populateMovePageOptions();
    if (el.buttonOptions) {
      el.buttonOptions.classList.add("hidden");
    }
    if (el.sliderOptions) {
      el.sliderOptions.classList.add("hidden");
    }
    if (el.graphOptions) {
      el.graphOptions.classList.add("hidden");
    }
    if (el.fSliderDirection) {
      el.fSliderDirection.value = DEFAULT_SLIDER_DIRECTION;
    }
    if (el.fSliderEntityDomain) {
      el.fSliderEntityDomain.value = DEFAULT_SLIDER_ENTITY_DOMAIN;
    }
    if (el.fButtonAccentColor) {
      el.fButtonAccentColor.value = DEFAULT_BUTTON_ACCENT_COLOR;
    }
    if (el.fButtonMode) {
      el.fButtonMode.value = DEFAULT_BUTTON_MODE;
    }
    if (el.fSliderAccentColor) {
      el.fSliderAccentColor.value = DEFAULT_SLIDER_ACCENT_COLOR;
    }
    if (el.fGraphLineColor) {
      el.fGraphLineColor.value = DEFAULT_GRAPH_LINE_COLOR;
    }
    if (el.fGraphTimeWindowMin) {
      el.fGraphTimeWindowMin.value = String(DEFAULT_GRAPH_TIME_WINDOW_MIN);
    }
    if (el.fGraphPointCount) {
      el.fGraphPointCount.value = "";
    }
    if (el.fGraphDisplayMode) {
      el.fGraphDisplayMode.value = DEFAULT_GRAPH_DISPLAY_MODE;
    }
    if (el.fGraphBarBucketMin) {
      el.fGraphBarBucketMin.value = String(DEFAULT_GRAPH_BAR_BUCKET_MIN);
    }
    if (el.fGraphBarBucketMinWrap) {
      el.fGraphBarBucketMinWrap.classList.add("hidden");
    }
    if (el.heatingOptions) {
      el.heatingOptions.classList.add("hidden");
    }
    if (el.fHeatingStyleVariant) {
      el.fHeatingStyleVariant.value = "default";
    }
    if (el.fHeatingArcOpening) {
      el.fHeatingArcOpening.value = "left";
    }
    if (el.fHeatingArcOpeningWrap) {
      el.fHeatingArcOpeningWrap.classList.add("hidden");
    }
    if (el.solarForecastFields) {
      el.solarForecastFields.classList.add("hidden");
    }
    if (el.carExtraFields) {
      el.carExtraFields.classList.add("hidden");
    }
    if (el.cameraOptions) el.cameraOptions.classList.add("hidden");
    if (el.priceOptions) el.priceOptions.classList.add("hidden");
    if (el.parqetOptions) el.parqetOptions.classList.add("hidden");
    if (el.parqetEntities) el.parqetEntities.classList.add("hidden");
    if (el.fParqetDividends) el.fParqetDividends.value = "";
    if (el.fParqetReturn) el.fParqetReturn.value = "";
    if (el.fParqetSelector) el.fParqetSelector.checked = false;
    if (el.fParqetView) el.fParqetView.value = "performance";
    if (el.fParqetInterval) el.fParqetInterval.value = "";
    if (el.fParqetHideAmounts) el.fParqetHideAmounts.checked = false;
    if (el.fCameraLive) el.fCameraLive.checked = false;
    bambuResetInspector(); evccResetInspector();
    for (const input of [el.fCarPrecond, el.fCarLock, el.fCarTemp, el.fEtlStyle, el.fEtlCharge, el.fEtlDischarge, el.fEtlSoc, el.fEtlForecast]) {
      if (input) input.value = "";
    }
    if (el.energyTimelineFields) {
      el.energyTimelineFields.classList.add("hidden");
    }
    if (el.roomEntitiesFields) {
      el.roomEntitiesFields.classList.add("hidden");
    }
    if (el.fVacuumBattery) el.fVacuumBattery.value = "";
    if (el.vacuumOptions) {
      el.vacuumOptions.classList.add("hidden");
    }
    if (el.binaryOptions) {
      el.binaryOptions.classList.add("hidden");
    }
    if (el.solarForecastStyleOptions) {
      el.solarForecastStyleOptions.classList.add("hidden");
    }
    if (el.gaugeOptions) {
      el.gaugeOptions.classList.add("hidden");
    }
    if (el.fGaugeStyle) el.fGaugeStyle.value = "linear";
    if (el.fGaugeBarStyle) el.fGaugeBarStyle.value = "smooth";
    if (el.fGaugeSegmentCount) el.fGaugeSegmentCount.value = "";
    if (el.fGaugeMin) el.fGaugeMin.value = "0";
    if (el.fGaugeMax) el.fGaugeMax.value = "100";
    if (el.fGaugeColorMode) el.fGaugeColorMode.value = "";
    if (el.fGaugeTargetLow) el.fGaugeTargetLow.value = "";
    if (el.fGaugeTargetHigh) el.fGaugeTargetHigh.value = "";
    if (el.fGaugeColorLow) el.fGaugeColorLow.value = "#3b82f6";
    if (el.fGaugeColorHigh) el.fGaugeColorHigh.value = "#ef4444";
    if (el.fGaugeTargetColor) el.fGaugeTargetColor.value = "#4fcb7e";
    updateGaugeColorModeVisibility();
    if (el.weatherForecastOptions) {
      el.weatherForecastOptions.classList.add("hidden");
    }
    if (el.fWeatherShowPrecip) {
      el.fWeatherShowPrecip.checked = true;
    }
    updateStyleGroupVisibility();
    if (el.fSolarForecastBarOrientation) {
      el.fSolarForecastBarOrientation.value = DEFAULT_SOLAR_FORECAST_BAR_ORIENTATION;
    }
    for (const input of [
      el.fSolarForecastToday,
      el.fSolarForecastTomorrow,
      el.fSolarForecastDay3,
      el.fSolarForecastDay4,
      el.fSolarForecastDay5,
      el.fSolarForecastBarMax,
    ]) {
      if (input) input.value = "";
    }
    renderEntityOptions();
    refreshInspectorLayout();
    return;
  }
  el.fTitle.value = widget.title || "";
  el.fType.value = widget.type;
  el.fEntity.value = widget.entity_id || "";
  el.fSecondaryEntity.value = widget.secondary_entity_id || "";
  if (el.fTertiaryEntity) el.fTertiaryEntity.value = widget.tertiary_entity_id || "";
  if (el.fSolarForecastToday) el.fSolarForecastToday.value = widget.forecast_today_entity_id || "";
  if (el.fSolarForecastTomorrow) el.fSolarForecastTomorrow.value = widget.forecast_tomorrow_entity_id || "";
  if (el.fSolarForecastDay3) el.fSolarForecastDay3.value = widget.forecast_day_3_entity_id || "";
  if (el.fSolarForecastDay4) el.fSolarForecastDay4.value = widget.forecast_day_4_entity_id || "";
  if (el.fSolarForecastDay5) el.fSolarForecastDay5.value = widget.forecast_day_5_entity_id || "";
  bambuLoadInspector(widget); evccLoadInspector(widget);
  if (el.fCarPrecond) el.fCarPrecond.value = widget.precond_entity_id || "";
  if (el.fCarLock) el.fCarLock.value = widget.lock_entity_id || "";
  if (el.fCarTemp) el.fCarTemp.value = widget.interior_temp_entity_id || "";
  if (el.fEtlStyle) el.fEtlStyle.value = widget.style_variant === "nested" ? "nested" : "";
  if (el.fEtlCharge) el.fEtlCharge.value = widget.battery_charge_entity_id || "";
  if (el.fEtlDischarge) el.fEtlDischarge.value = widget.battery_discharge_entity_id || "";
  if (el.fEtlSoc) el.fEtlSoc.value = widget.battery_soc_entity_id || "";
  if (el.fEtlForecast) el.fEtlForecast.value = widget.forecast_today_entity_id || "";
  if (el.fBinaryDisplayMode) el.fBinaryDisplayMode.value = widget.display_mode || "";
  if (el.fVacuumBattery) el.fVacuumBattery.value = widget.type === "vacuum_tile" ? String(widget.battery_entity_id || "") : "";
  if (widget.type === "vacuum_tile") {
    fillVacuumModeSelect([], widget.default_mode || "");
    loadVacuumModeOptions(widget.tertiary_entity_id);
  }
  if (el.fRoomEntities) el.fRoomEntities.value = Array.isArray(widget.room_entities) ? widget.room_entities.join("\n") : "";
  if (widget.type === "room_tile") {
    refreshRoomExtraControls(widget.entity_id, widget.room_entities, widget.room_extra_mode, widget.room_extra_entity_id);
  }
  if (el.fRoomControls) el.fRoomControls.checked = widget.type === "room_tile" && widget.room_controls === true;
  if (el.fSolarForecastBarMax) el.fSolarForecastBarMax.value = widget.solar_forecast_bar_max_kwh > 0 ? String(widget.solar_forecast_bar_max_kwh) : "";
  if (el.fSolarForecastBarOrientation) {
    el.fSolarForecastBarOrientation.value = normalizeSolarForecastBarOrientation(widget.solar_forecast_bar_orientation);
  }
  if (el.fGaugeStyle) el.fGaugeStyle.value = GAUGE_STYLES.includes(widget.style_variant) ? widget.style_variant : "linear";
  if (el.fGaugeBarStyle) el.fGaugeBarStyle.value = widget.gauge_bar_style === "segmented" ? "segmented" : "smooth";
  if (el.fGaugeSegmentCount) {
    const segments = normalizeGaugeSegmentCount(widget.gauge_segment_count);
    el.fGaugeSegmentCount.value = segments ? String(segments) : "";
  }
  if (el.fGaugeMin) el.fGaugeMin.value = String(Number.isFinite(widget.gauge_min) ? widget.gauge_min : 0);
  if (el.fGaugeMax) el.fGaugeMax.value = String(Number.isFinite(widget.gauge_max) ? widget.gauge_max : 100);
  if (el.fGaugeColorMode) {
    el.fGaugeColorMode.value = ["gradient", "range"].includes(widget.gauge_color_mode) ? widget.gauge_color_mode : "";
  }
  if (el.fGaugeTargetLow) el.fGaugeTargetLow.value = Number.isFinite(widget.gauge_target_low) ? String(widget.gauge_target_low) : "";
  if (el.fGaugeTargetHigh) el.fGaugeTargetHigh.value = Number.isFinite(widget.gauge_target_high) ? String(widget.gauge_target_high) : "";
  if (el.fGaugeColorLow) el.fGaugeColorLow.value = normalizeHexColor(widget.gauge_color_low, "#3b82f6");
  if (el.fGaugeColorHigh) el.fGaugeColorHigh.value = normalizeHexColor(widget.gauge_color_high, "#ef4444");
  if (el.fGaugeTargetColor) el.fGaugeTargetColor.value = normalizeHexColor(widget.gauge_target_color, "#4fcb7e");
  updateGaugeColorModeVisibility();
  updateGaugeBarStyleVisibility();
  el.fX.value = widget.rect.x;
  el.fY.value = widget.rect.y;
  el.fW.value = widget.rect.w;
  el.fH.value = widget.rect.h;
  populateMovePageOptions();

  const isButton = widget.type === "button";
  const isSlider = widget.type === "slider";
  const isGraph = widget.type === "graph";
  const isHeating = widget.type === "heating_tile";
  const isSolarForecast = widget.type === "solar_forecast";
  const isGauge = widget.type === "gauge";
  const isRoomTile = widget.type === "room_tile";
  const isBinaryTile = widget.type === "binary_tile";
  if (el.buttonOptions) {
    el.buttonOptions.classList.toggle("hidden", !isButton);
  }
  if (el.sliderOptions) {
    el.sliderOptions.classList.toggle("hidden", !isSlider);
  }
  if (el.graphOptions) {
    el.graphOptions.classList.toggle("hidden", !isGraph);
  }
  if (el.heatingOptions) {
    el.heatingOptions.classList.toggle("hidden", !isHeating);
  }
  if (el.solarForecastFields) {
    el.solarForecastFields.classList.toggle("hidden", !isSolarForecast);
  }
  if (el.carExtraFields) {
    el.carExtraFields.classList.toggle("hidden", widget.type !== "car_widget");
  }
  if (el.bambuFields) {
    el.bambuFields.classList.toggle("hidden", widget.type !== "bambu_printer");
  }
  if (el.energyTimelineFields) {
    el.energyTimelineFields.classList.toggle("hidden", widget.type !== "energy_timeline");
    if (el.energyTimelineHint) el.energyTimelineHint.textContent = t("layout.inspector.energy_timeline_hint");
  }
  if (el.roomEntitiesFields) {
    el.roomEntitiesFields.classList.toggle("hidden", !isRoomTile);
  }
  if (el.vacuumOptions) {
    el.vacuumOptions.classList.toggle("hidden", widget.type !== "vacuum_tile");
  }
  if (el.binaryOptions) {
    el.binaryOptions.classList.toggle("hidden", !isBinaryTile);
  }
  if (el.solarForecastStyleOptions) {
    el.solarForecastStyleOptions.classList.toggle("hidden", !isSolarForecast);
  }
  if (el.sensorOptions) {
    el.sensorOptions.classList.toggle("hidden", widget.type !== "sensor");
  }
  if (el.cameraOptions) el.cameraOptions.classList.toggle("hidden", widget.type !== "camera_tile");
  if (el.fCameraLive) el.fCameraLive.checked = widget.type === "camera_tile" && widget.camera_live === true;
  if (el.lockOptions) el.lockOptions.classList.toggle("hidden", widget.type !== "lock_tile");
  if (el.fLockConfirm) el.fLockConfirm.checked = widget.type === "lock_tile" && widget.lock_confirm === true;
  if (el.parqetOptions) el.parqetOptions.classList.toggle("hidden", widget.type !== "parqet_portfolio");
  if (el.parqetEntities) el.parqetEntities.classList.toggle("hidden", widget.type !== "parqet_portfolio");
  if (el.fParqetDividends) el.fParqetDividends.value = widget.type === "parqet_portfolio" ? String(widget.dividends_entity_id || "") : "";
  if (el.fParqetReturn) el.fParqetReturn.value = widget.type === "parqet_portfolio" ? String(widget.return_entity_id || "") : "";
  if (el.fParqetHideAmounts) el.fParqetHideAmounts.checked = widget.type === "parqet_portfolio" && widget.parqet_hide_amounts === true;
  if (el.fParqetSelector) el.fParqetSelector.checked = widget.type === "parqet_portfolio" && widget.parqet_selector === true;
  if (el.fParqetView) el.fParqetView.value = widget.type === "parqet_portfolio" && widget.parqet_view === "holdings" ? "holdings" : "performance";
  if (el.fParqetInterval) el.fParqetInterval.value = widget.type === "parqet_portfolio" && ["1d", "1w", "1m", "ytd"].includes(widget.parqet_interval) ? widget.parqet_interval : "";
  if (el.priceOptions) el.priceOptions.classList.toggle("hidden", widget.type !== "price_forecast");
  if (el.fPriceDaySwitch) el.fPriceDaySwitch.checked = widget.type === "price_forecast" && widget.price_day_switch === true;
  if (el.fPriceCheapTimes) el.fPriceCheapTimes.checked = widget.type === "price_forecast" && widget.price_cheap_times === true;
  if (el.fPriceLine) el.fPriceLine.checked = widget.type === "price_forecast" && widget.price_line === true;
  if (el.fPriceAverage) el.fPriceAverage.value = widget.type === "price_forecast" && widget.price_average > 0 ? String(widget.price_average) : "";
  alarmLoadInspector(widget);
  if (el.historyOptions) el.historyOptions.classList.toggle("hidden",
    !["sensor", "binary_tile", "contact_tile", "lock_tile", "alarm_tile", "vacuum_tile"].includes(widget.type));
  if (el.fHistoryPopup) el.fHistoryPopup.checked = widget.history_popup === true;
  if (el.fShowName) {
    el.fShowName.checked = widgetShowsName(widget);
    el.fShowName.disabled = !widgetSupportsName(widget.type);
  }
  if (el.fSensorShowAge) {
    el.fSensorShowAge.checked = widget.type !== "sensor" || widget.sensor_show_age !== false;
  }
  updateSensorEntityInfo(widget);
  if (el.gaugeOptions) {
    el.gaugeOptions.classList.toggle("hidden", !isGauge);
  }
  updateGaugeBarStyleVisibility();
  if (el.weatherForecastOptions) {
    el.weatherForecastOptions.classList.toggle("hidden", widget.type !== "weather_3day");
  }
  if (el.fWeatherShowPrecip) {
    el.fWeatherShowPrecip.checked = widget.type !== "weather_3day" || widget.show_precipitation !== false;
  }
  renderReadabilityInspector(widget);
  updateStyleGroupVisibility();
  if (isButton) {
    const accent = normalizeHexColor(widget.button_accent_color, DEFAULT_BUTTON_ACCENT_COLOR);
    const buttonMode = normalizeButtonMode(widget.button_mode);
    widget.button_accent_color = accent;
    widget.button_mode = buttonMode;
    if (el.fButtonAccentColor) {
      el.fButtonAccentColor.value = accent;
    }
    if (el.fButtonMode) {
      el.fButtonMode.value = buttonMode;
    }
  } else {
    if (el.fButtonAccentColor) {
      el.fButtonAccentColor.value = DEFAULT_BUTTON_ACCENT_COLOR;
    }
    if (el.fButtonMode) {
      el.fButtonMode.value = DEFAULT_BUTTON_MODE;
    }
  }
  if (isSlider) {
    const sliderEntityDomain = normalizeSliderEntityDomain(widget.slider_entity_domain);
    const direction = normalizeSliderDirection(widget.slider_direction);
    const accent = normalizeHexColor(widget.slider_accent_color, DEFAULT_SLIDER_ACCENT_COLOR);
    widget.slider_entity_domain = sliderEntityDomain;
    widget.slider_direction = direction;
    widget.slider_accent_color = accent;
    if (el.fSliderEntityDomain) {
      el.fSliderEntityDomain.value = sliderEntityDomain;
    }
    if (el.fSliderDirection) {
      el.fSliderDirection.value = direction;
    }
    if (el.fSliderAccentColor) {
      el.fSliderAccentColor.value = accent;
    }
  } else {
    if (el.fSliderEntityDomain) {
      el.fSliderEntityDomain.value = DEFAULT_SLIDER_ENTITY_DOMAIN;
    }
    if (el.fSliderDirection) {
      el.fSliderDirection.value = DEFAULT_SLIDER_DIRECTION;
    }
    if (el.fSliderAccentColor) {
      el.fSliderAccentColor.value = DEFAULT_SLIDER_ACCENT_COLOR;
    }
  }
  if (isGraph) {
    const lineColor = normalizeHexColor(widget.graph_line_color, DEFAULT_GRAPH_LINE_COLOR);
    const timeWindowMin = normalizeGraphTimeWindowMin(widget.graph_time_window_min);
    const pointCount = normalizeGraphPointCount(widget.graph_point_count);
    const displayMode = normalizeGraphDisplayMode(widget.graph_display_mode);
    const barBucketMin = normalizeGraphBarBucketMin(widget.graph_bar_bucket_min);
    widget.graph_line_color = lineColor;
    widget.graph_time_window_min = timeWindowMin;
    widget.graph_display_mode = displayMode;
    widget.graph_bar_bucket_min = barBucketMin;
    if (pointCount > 0) {
      widget.graph_point_count = pointCount;
    } else {
      delete widget.graph_point_count;
    }
    if (el.fGraphLineColor) {
      el.fGraphLineColor.value = lineColor;
    }
    if (el.fGraphTimeWindowMin) {
      el.fGraphTimeWindowMin.value = String(widget.graph_time_window_min);
    }
    if (el.fGraphHaHistory) {
      el.fGraphHaHistory.checked = widget.graph_ha_history === true;
    }
    if (el.fGraphPointCount) {
      el.fGraphPointCount.value = pointCount > 0 ? String(pointCount) : "";
    }
    if (el.fGraphDisplayMode) {
      el.fGraphDisplayMode.value = displayMode;
    }
    if (el.fGraphBarBucketMin) {
      el.fGraphBarBucketMin.value = String(barBucketMin);
    }
    if (el.fGraphBarBucketMinWrap) {
      el.fGraphBarBucketMinWrap.classList.toggle("hidden", displayMode !== "bars");
    }
    if (el.fGraphPointCountWrap) {
      el.fGraphPointCountWrap.classList.toggle("hidden", displayMode !== "line");
    }
  } else {
    if (el.fGraphLineColor) {
      el.fGraphLineColor.value = DEFAULT_GRAPH_LINE_COLOR;
    }
    if (el.fGraphTimeWindowMin) {
      el.fGraphTimeWindowMin.value = String(DEFAULT_GRAPH_TIME_WINDOW_MIN);
    }
    if (el.fGraphPointCount) {
      el.fGraphPointCount.value = "";
    }
    if (el.fGraphDisplayMode) {
      el.fGraphDisplayMode.value = DEFAULT_GRAPH_DISPLAY_MODE;
    }
    if (el.fGraphBarBucketMin) {
      el.fGraphBarBucketMin.value = String(DEFAULT_GRAPH_BAR_BUCKET_MIN);
    }
    if (el.fGraphBarBucketMinWrap) {
      el.fGraphBarBucketMinWrap.classList.add("hidden");
    }
  }

  if (isHeating) {
    const styleVariant = widget.style_variant === "arc_semi" ? "arc_semi" : "default";
    const arcOpening = ["left", "right", "top", "bottom"].includes(widget.arc_opening) ? widget.arc_opening : "left";
    widget.style_variant = styleVariant;
    widget.arc_opening = arcOpening;
    if (el.fHeatingStyleVariant) {
      el.fHeatingStyleVariant.value = styleVariant;
    }
    if (el.fHeatingArcOpening) {
      el.fHeatingArcOpening.value = arcOpening;
    }
    if (el.fHeatingArcOpeningWrap) {
      el.fHeatingArcOpeningWrap.classList.toggle("hidden", styleVariant !== "arc_semi");
    }
  } else {
    if (el.fHeatingStyleVariant) {
      el.fHeatingStyleVariant.value = "default";
    }
    if (el.fHeatingArcOpening) {
      el.fHeatingArcOpening.value = "left";
    }
    if (el.fHeatingArcOpeningWrap) {
      el.fHeatingArcOpeningWrap.classList.add("hidden");
    }
  }

  renderEntityOptions();
  refreshInspectorLayout();
}

function renderAll() {
  normalizeLayoutWidgets(editor.layout);
  renderPages();
  renderPageEditor();
  renderWidgets();
  renderInspector();
  renderCanvas();
  updateCanvasScale();
  refreshInspectorLayout();
  // Every real edit goes through renderAll(); markDirty() compares the layout with the loaded/saved snapshot,
  // so a render that changed nothing (selecting a page or a widget) leaves the layout marked as saved.
  markDirty();
}

function addPage() {
  layoutHistoryBoundary();
  const pageId = uniqueId("page", editor.layout.pages);
  const pageNumber = editor.layout.pages.length + 1;
  editor.layout.pages.push({
    id: pageId,
    title: t("layout.pages.new_title", { number: pageNumber }),
    widgets: [],
  });
  editor.selectedPageId = pageId;
  editor.selectedWidgetId = null;
  renderAll();
}

// Copies a normal page (all its tiles) right behind the original. Energy pages
// have no tiles of their own, so they are not duplicated.
function duplicatePage(page) {
  layoutHistoryBoundary();
  if (!page || isFixedPage(page)) return;
  if (editor.layout.pages.length >= maxPages()) {
    setStatus(t("layout.status.max_pages", { max: maxPages() }), true);
    return;
  }
  const copy = JSON.parse(JSON.stringify(page));
  copy.id = uniqueId("page", editor.layout.pages);
  copy.title = `${page.title || page.id}${t("layout.pages.copy_suffix")}`.slice(0, 63);
  const index = editor.layout.pages.indexOf(page);
  // Widget ids are unique across the whole layout (the panel rejects duplicates on save): give
  // every copied tile a new id. The copy joins the layout first with no tiles, and the tiles are
  // added back one by one, so createWidgetIdForPage() also sees the ids already given out here.
  const widgets = Array.isArray(copy.widgets) ? copy.widgets : [];
  copy.widgets = [];
  editor.layout.pages.splice(index + 1, 0, copy);
  for (const widget of widgets) {
    widget.id = createWidgetIdForPage(copy, widget.type);
    copy.widgets.push(widget);
  }
  editor.selectedPageId = copy.id;
  editor.selectedWidgetId = null;
  renderAll();
  setStatus(t("layout.status.duplicated", { name: copy.title }));
}

const ROOM_EXTRA_NONE_KEYS = { humidity: "humidity", cover: "cover", media_player: "media" };

// The room entities typed into the inspector (one per line, at most 7 besides the main sensor).
function roomEntitiesFromInspector() {
  return (el.fRoomEntities?.value || "")
    .split(/[\n,;]+/)
    .map((v) => v.trim())
    .filter((v) => /^[a-z0-9_]+\.[A-Za-z0-9_]+$/.test(v))
    .slice(0, 7);
}

// Fills the room card's two extra-field controls from the entities the card already has: the choice is limited to them,
// so the field never adds a subscription; without a fitting entity the hint says how to get one.
function refreshRoomExtraControls(primary, extras, mode, entityId) {
  if (!el.fRoomExtraMode || !el.fRoomExtraEntity) return;
  const current = ROOM_EXTRA_MODES.includes(mode) ? mode : "default";
  el.fRoomExtraMode.value = current;
  const candidates = roomExtraCandidates(current, primary, extras);
  el.fRoomExtraEntity.innerHTML = "";
  for (const id of candidates) {
    const option = document.createElement("option");
    option.value = id;
    option.textContent = id;
    el.fRoomExtraEntity.appendChild(option);
  }
  if (candidates.includes(entityId)) el.fRoomExtraEntity.value = entityId;
  el.fRoomExtraEntity.disabled = candidates.length === 0;
  if (el.fRoomExtraEntityWrap) el.fRoomExtraEntityWrap.classList.toggle("hidden", current === "default");
  if (el.fRoomExtraHint) {
    el.fRoomExtraHint.textContent = current !== "default" && candidates.length === 0
      ? t(`layout.inspector.room_extra_none_${ROOM_EXTRA_NONE_KEYS[current]}`)
      : t("layout.inspector.room_extra_hint");
  }
}

function normalizeHouseConfig(page) {
  if (!page || !isHousePage(page)) return;
  if (!page.house || typeof page.house !== "object" || Array.isArray(page.house)) page.house = { rooms: [] };
  if (!Array.isArray(page.house.rooms)) page.house.rooms = [];
  page.house.rooms = page.house.rooms
    .filter((room) => room && typeof room.entity_id === "string" && room.entity_id)
    .slice(0, 32)
    .map((room) => ({
      name: String(room.name || room.entity_id),
      floor: String(room.floor || ""),
      entity_id: room.entity_id,
      room_entities: Array.isArray(room.room_entities) ? room.room_entities.slice(0, 7) : [],
      ...(ROOM_EXTRA_MODES.includes(room.room_extra_mode) && room.room_extra_mode !== "default" && room.room_extra_entity_id
        ? { room_extra_mode: room.room_extra_mode, room_extra_entity_id: String(room.room_extra_entity_id) } : {}),
    }));
  for (const room of page.house.rooms) normalizeRoomExtra(room, room.entity_id, room.room_entities);
  page.widgets = [];
}

function addHousePage() {
  layoutHistoryBoundary();
  if (editor.layout.pages.length >= maxPages()) {
    setStatus(t("layout.status.max_pages", { max: maxPages() }), true);
    return;
  }
  const pageId = uniqueId("house", editor.layout.pages);
  editor.layout.pages.push({
    id: pageId,
    type: HOUSE_PAGE_TYPE,
    title: t("layout.pages.house_title"),
    house: { rooms: [] },
    widgets: [],
  });
  editor.selectedPageId = pageId;
  editor.selectedWidgetId = null;
  renderAll();
  void importHouseRooms();
}

// A room discovery the panel still holds from an earlier import is reused for this long, so
// importing again (house page, then a room, or a second try) does not read every entity of every
// area from Home Assistant again (459 entities in 17 areas took about 24 s on a paneljc).
const ROOM_ITEMS_MAX_AGE_MS = 3 * 60 * 1000;

// Entities of the house's areas from the panel's Home Assistant discovery (pseudo domain "room"),
// all of them or, with search, those of the areas whose name contains it.
async function fetchAllRoomItems({ search = "", maxAgeMs = ROOM_ITEMS_MAX_AGE_MS } = {}) {
  const url = "/api/ha/light_entities?" + new URLSearchParams(search ? { domain: "room", search } : { domain: "room" });
  let refresh = false;
  let refreshed = false;
  for (let poll = 0; poll < 120; poll++) {
    const data = await apiGet(url + (refresh ? "&refresh=1" : ""));
    if (refresh) {
      refresh = false;
      refreshed = true;
    }
    if (data.pending !== true && data.status === "ready") {
      if (!refreshed && Number(data.age_ms) > maxAgeMs) {
        refresh = true;
        continue;
      }
      // The registry answer (areas, floors) can arrive a moment after the items.
      if (!Array.isArray(data.areas) || !data.areas.length) {
        const again = await apiGet(url);
        if (Array.isArray(again.areas)) data.areas = again.areas;
      }
      return {
        items: normalizeEntityPickerItems(data.items, "room"),
        areas: new Map((data.areas || []).map((a) => [a.name, a])),
        ageMs: refreshed ? 0 : Number(data.age_ms) || 0,
      };
    }
    if (data.status === "disconnected") throw new Error(t("entity_picker.disconnected"));
    await new Promise((resolve) => setTimeout(resolve, 900));
  }
  throw new Error(t("entity_picker.pending"));
}

async function importHouseRooms() {
  const page = selectedPage();
  if (!isHousePage(page)) return;
  const setHouseStatus = (text) => {
    if (el.houseStatus) el.houseStatus.textContent = text;
  };
  setHouseStatus(t("house.importing"));
  let items;
  let areas = new Map();
  let ageMs = 0;
  try {
    ({ items, areas, ageMs } = await fetchAllRoomItems());
  } catch (error) {
    setHouseStatus(String(error.message || error));
    return;
  }
  const byRoom = new Map();
  const floorOf = new Map();
  for (const item of items) {
    if (!item.room) continue;
    if (!byRoom.has(item.room)) byRoom.set(item.room, []);
    byRoom.get(item.room).push(item);
    if (item.floor) floorOf.set(item.room, item.floor);
  }
  const existing = new Map(page.house.rooms.map((room) => [room.name, room]));
  const rooms = [];
  const roomExtraCleared = [];
  for (const [name, entries] of byRoom) {
    const info = areas.get(name);
    const fill = roomCardFill(entries, name, info);
    const cat = roomCardCategories(entries, name);
    // Rooms without a temperature, window/door sensor or light are not worth a card.
    if (!cat.temperature.length && !cat.contact.length && !cat.light.length && !cat.climate.length) continue;
    const previous = existing.get(name);
    const imported = {
      name,
      level: info && Number.isFinite(info.floor_level) ? info.floor_level : 0,
      floor: previous ? previous.floor : ((info && info.floor) || floorOf.get(name) || ""),
      entity_id: fill.primary,
      room_entities: fill.extras,
    };
    // A chosen extra field survives the import while its entity is still part of the room; otherwise it is cleared.
    if (previous && previous.room_extra_mode) {
      imported.room_extra_mode = previous.room_extra_mode;
      imported.room_extra_entity_id = previous.room_extra_entity_id;
      if (normalizeRoomExtra(imported, imported.entity_id, imported.room_entities)) roomExtraCleared.push(name);
    }
    rooms.push(imported);
  }
  // Keep the order of rooms the user already arranged, new ones after them; floors stay together.
  const order = new Map(page.house.rooms.map((room, index) => [room.name, index]));
  // Like the house: the top floor first, the basement last (floor level from Home Assistant, descending).
  rooms.sort((a, b) => (order.has(a.name) ? order.get(a.name) : 999) - (order.has(b.name) ? order.get(b.name) : 999) ||
    b.level - a.level || a.floor.localeCompare(b.floor) || a.name.localeCompare(b.name));
  for (const room of rooms) delete room.level;
  page.house.rooms = rooms.slice(0, 32);
  const minutes = Math.round(ageMs / 60000);
  setHouseStatus(t("house.imported", { count: page.house.rooms.length }) +
    (minutes >= 1 ? " " + t("house.imported_cached", { minutes }) : "") +
    (roomExtraCleared.length ? ` ${t("layout.inspector.room_extra_cleared")} (${roomExtraCleared.join(", ")})` : ""));
  renderAll();
}

// An empty page offers the two ways to fill it right on the canvas (this replaced the Quick Setup
// wizard): a room page from Home Assistant, filling this very page, or a single tile.
function renderEmptyPageHint(page) {
  const hint = document.createElement("div");
  hint.className = "canvas-empty-hint";
  hint.addEventListener("pointerdown", (event) => event.stopPropagation());
  const text = document.createElement("p");
  text.textContent = t("layout.canvas.empty");
  const room = document.createElement("button");
  room.type = "button";
  room.className = "btn primary";
  room.textContent = t("layout.canvas.empty_room");
  room.onclick = () => void openRoomImportPicker({ targetPageId: page.id });
  const tile = document.createElement("button");
  tile.type = "button";
  tile.className = "btn";
  tile.textContent = t("layout.canvas.empty_tile");
  tile.onclick = () => openAddTileDialog();
  hint.append(text, room, tile);
  el.canvas.appendChild(hint);
}

function renderHouseOptions(page) {
  if (!el.houseRoomList) return;
  el.houseRoomList.innerHTML = "";
  page.house.rooms.forEach((room, index) => {
    const row = document.createElement("div");
    row.className = "house-room-row";
    row.style.cssText = "display:flex;gap:6px;margin:4px 0;align-items:center";
    const floor = document.createElement("input");
    floor.type = "text";
    floor.value = room.floor;
    floor.placeholder = t("house.floor");
    floor.style.width = "16%";
    floor.onchange = () => {
      room.floor = floor.value.trim();
      renderCanvas();
      markDirty();
    };
    const name = document.createElement("input");
    name.type = "text";
    name.value = room.name;
    name.onchange = () => {
      room.name = name.value.trim() || room.entity_id;
      renderCanvas();
      markDirty();
    };
    name.style.width = "auto";
    // The room's main sensor (temperature): editable, marked when Home Assistant does not know it.
    const sensor = document.createElement("input");
    sensor.type = "text";
    sensor.value = room.entity_id || "";
    sensor.setAttribute("list", "sensorEntityOptions");
    sensor.placeholder = t("house.sensor_placeholder");
    sensor.style.flex = "1 1 100%";
    sensor.style.minWidth = "0";
    const markMissing = () => {
      const missing = (editor.haDiagnostics?.names || []).includes(sensor.value.trim());
      sensor.style.borderColor = missing ? "#d9534f" : "";
      sensor.title = missing ? t("house.sensor_missing") : "";
    };
    markMissing();
    sensor.onchange = () => {
      room.entity_id = sensor.value.trim();
      markMissing();
      renderCanvas();
      markDirty();
    };
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "btn small";
    remove.textContent = "\u00d7";
    remove.onclick = () => {
      page.house.rooms.splice(index, 1);
      renderAll();
    };
    const move = (delta) => {
      const target = index + delta;
      if (target < 0 || target >= page.house.rooms.length) return;
      [page.house.rooms[index], page.house.rooms[target]] = [page.house.rooms[target], page.house.rooms[index]];
      renderAll();
    };
    const up = document.createElement("button");
    up.type = "button";
    up.className = "btn small";
    up.textContent = "\u2191";
    up.onclick = () => move(-1);
    const down = document.createElement("button");
    down.type = "button";
    down.className = "btn small";
    down.textContent = "\u2193";
    down.onclick = () => move(1);
    row.style.flexWrap = "wrap";
    name.style.flex = "1";
    // The extra field of this room's card: a mode and an entity the room already has.
    const extraMode = document.createElement("select");
    extraMode.title = t("layout.inspector.room_extra");
    for (const [value, key] of [["default", "room_extra_default"], ["humidity", "room_extra_humidity"],
      ["cover", "room_extra_cover"], ["media_player", "room_extra_media"]]) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = t(`layout.inspector.${key}`);
      extraMode.appendChild(option);
    }
    const extraEntity = document.createElement("select");
    const fillExtraEntity = () => {
      const mode = extraMode.value;
      const candidates = roomExtraCandidates(mode, room.entity_id, room.room_entities);
      extraEntity.innerHTML = "";
      for (const id of candidates) {
        const option = document.createElement("option");
        option.value = id;
        option.textContent = id;
        extraEntity.appendChild(option);
      }
      if (candidates.includes(room.room_extra_entity_id)) extraEntity.value = room.room_extra_entity_id;
      extraEntity.disabled = candidates.length === 0;
      extraEntity.classList.toggle("hidden", mode === "default");
      extraEntity.title = mode !== "default" && candidates.length === 0
        ? t(`layout.inspector.room_extra_none_${ROOM_EXTRA_NONE_KEYS[mode]}`) : "";
    };
    extraMode.value = ROOM_EXTRA_MODES.includes(room.room_extra_mode) ? room.room_extra_mode : "default";
    fillExtraEntity();
    const saveExtra = () => {
      applyRoomExtra(room, extraMode.value, extraEntity.value, room.entity_id, room.room_entities);
      renderCanvas();
      markDirty();
    };
    extraMode.onchange = () => { fillExtraEntity(); saveExtra(); };
    extraEntity.onchange = saveExtra;
    extraMode.style.flex = "1 1 40%";
    extraEntity.style.flex = "1 1 50%";
    row.append(floor, name, up, down, remove, sensor, extraMode, extraEntity);
    el.houseRoomList.appendChild(row);
  });
  if (el.houseStatus && !el.houseStatus.textContent) {
    el.houseStatus.textContent = t("house.count", { count: page.house.rooms.length });
  }
}

function addEnergyPage() {
  layoutHistoryBoundary();
  const pageId = uniqueId("energy", editor.layout.pages);
  editor.layout.pages.push({
    id: pageId,
    type: ENERGY_PAGE_TYPE,
    title: t("layout.pages.energy_title"),
    energy: defaultEnergyConfig(),
    widgets: [],
  });
  editor.selectedPageId = pageId;
  editor.selectedWidgetId = null;
  renderAll();
  void loadEnergyPreview();
}

// A short-lived "Undo" toast after deleting a page or widget: the confirm() dialog already asks
// once, but a wrong click among several similar-looking tiles is still easy, and re-creating a
// tile from scratch (entity, size, position, style) is real work compared to one more click.
let pendingUndo = null;
let undoTimeoutId = null;
const UNDO_TOAST_MS = 8000;

function hideUndoToast() {
  if (undoTimeoutId) {
    clearTimeout(undoTimeoutId);
    undoTimeoutId = null;
  }
  pendingUndo = null;
  if (el.undoToast) el.undoToast.classList.add("hidden");
}

function offerUndo(message, restore) {
  pendingUndo = restore;
  if (el.undoToastText) el.undoToastText.textContent = message;
  if (el.undoToast) el.undoToast.classList.remove("hidden");
  if (undoTimeoutId) clearTimeout(undoTimeoutId);
  undoTimeoutId = window.setTimeout(hideUndoToast, UNDO_TOAST_MS);
}

function deletePage() {
  layoutHistoryBoundary();
  if (!editor.layout.pages.length || !editor.selectedPageId) return;
  if (editor.layout.pages.length === 1) {
    setStatus(t("layout.status.at_least_one_page"), true);
    return;
  }
  const page = editor.layout.pages.find((p) => p.id === editor.selectedPageId);
  if (!page) return;
  const name = page.title || page.id;
  if (!window.confirm(t("layout.pages.confirm_delete", { name }))) return;
  editor.layout.pages = editor.layout.pages.filter((p) => p.id !== editor.selectedPageId);
  editor.selectedPageId = editor.layout.pages[0].id;
  editor.selectedWidgetId = null;
  renderAll();
  offerUndo(t("layout.pages.deleted_undo", { name }), () => layoutHistoryStep(-1));
}

function applyPageName() {
  const page = selectedPage();
  if (!page) return;
  const nextTitle = el.pageTitleInput.value.trim();
  page.title = nextTitle || page.id;
  if (isEnergyPage(page)) {
    applyEnergyPageConfig({ render: false });
  }
  renderAll();
}

function applyEnergyPageConfig(options = {}) {
  const page = selectedPage();
  if (!isEnergyPage(page)) return false;
  normalizeEnergyConfig(page);
  const selectedSource = (el.energySource?.value || page.energy.source || ENERGY_SOURCE_HA).trim();
  page.energy.source = ENERGY_SOURCES.has(selectedSource) ? selectedSource : ENERGY_SOURCE_HA;
  const inputs = getEnergyInputs();
  for (const key of ENERGY_ENTITY_KEYS) {
    page.energy[key] = (inputs[key]?.value || "").trim();
  }
  if ((el.energyDetails?.value || "") === ENERGY_DETAILS_SIDE) page.energy.details = ENERGY_DETAILS_SIDE;
  else delete page.energy.details;
  updateEnergySourceUi(page.energy.source);
  if (options.render !== false) {
    renderAll();
  }
  if (page.energy.source === ENERGY_SOURCE_HA) {
    void loadEnergyPreview();
  }
  return true;
}

// First free position (scanning row by row in grid steps) where a new tile of
// the given size overlaps no existing tile; falls back to the top left corner.
function findFreeSpot(page, w, h) {
  const rects = (page?.widgets || []).map((widget) => widget.rect).filter(Boolean);
  const overlaps = (x, y) => rects.some((r) => x < r.x + r.w && x + w > r.x && y < r.y + r.h && y + h > r.y);
  for (let y = GRID; y + h <= CANVAS_HEIGHT; y += GRID) {
    for (let x = GRID; x + w <= CANVAS_WIDTH; x += GRID) {
      if (!overlaps(x, y)) return { x, y };
    }
  }
  // No free space: cascade the new tile so it does not hide the previous ones completely.
  const step = (rects.length % 8) * GRID;
  return {
    x: Math.max(0, Math.min(20 + step, CANVAS_WIDTH - w)),
    y: Math.max(0, Math.min(20 + step, CANVAS_HEIGHT - h)),
  };
}

function addWidget(type, options = {}) {
  layoutHistoryBoundary();
  const page = selectedPage();
  if (!page) return;
  if (isFixedPage(page)) {
    setStatus(t("layout.status.energy_page_only"), true);
    return null;
  }
  const sliderDomain = DEFAULT_SLIDER_ENTITY_DOMAIN;
  const id = createWidgetIdForPage(page, type);
  const entityId = typeof options.entityId === "string"
    ? options.entityId
    : type === "solar_forecast"
      ? DEFAULT_SOLCAST_ENTITIES.remaining
      : pickDefaultEntityForWidgetType(type, sliderDomain);
  const secondaryEntityId = type === "heating_tile" ? pickDefaultSecondaryEntity("heating_tile", "sensor") : "";
  const defaultW = type === "weather_3day" ? 360
      : type === "solar_forecast" ? 460
      : type === "car_widget" ? 280
      : type === "wallbox_evcc" ? 360
      : type === "bambu_printer" ? 360
      : type === "energy_timeline" ? 420
      : type === "price_forecast" ? 460
      : type === "parqet_portfolio" ? 360
      : type === "todo_list" ? 360
      : type === "media_player" ? 360
      : type === "sonos_favorites" ? 360
      : type === "cover_tile" ? 360
      : (type === "scene_tile" || type === "binary_tile" || type === "select_tile" || type === "datetime_tile") ? 260
      : (type === "lock_tile" || type === "alarm_tile") ? 300
      : type === "room_tile" ? 400
      : type === "climate_tile" ? 320
      : type === "contact_tile" ? 240
      : type === "camera_tile" ? 360
      : type === "fan_tile" ? 360
      : type === "vacuum_tile" ? 360
      : (type === "light_tile" || type === "heating_tile" || type === "weather_tile" || type === "empty_tile") ? 300
      : 220;
  const defaultH = type === "weather_3day" ? 260
      : type === "solar_forecast" ? 250
      : type === "car_widget" ? 340
      : type === "wallbox_evcc" ? 300
      : type === "bambu_printer" ? 420
      : type === "energy_timeline" ? 260
      : type === "price_forecast" ? 250
      : type === "parqet_portfolio" ? 330
      : type === "todo_list" ? 360
      : type === "media_player" ? 280
      : type === "sonos_favorites" ? 300
      : type === "fan_tile" ? 90
      : type === "camera_tile" ? 240
      : type === "contact_tile" ? 80
      : (type === "select_tile" || type === "lock_tile" || type === "alarm_tile" || type === "datetime_tile") ? 100
      : (type === "scene_tile" || type === "binary_tile") ? 90
      : type === "room_tile" ? 200
      : type === "climate_tile" ? 260
      : type === "cover_tile" ? 90
      : type === "vacuum_tile" ? 300
      : (type === "light_tile" || type === "heating_tile" || type === "weather_tile" || type === "empty_tile") ? 260
      : 120;
  const sizeType = widgetSizeType({ type, display_mode: options.displayMode });
  const sized = clampRectToCanvas({ x: 20, y: 20, w: defaultW, h: defaultH }, sizeType);
  const spot = findFreeSpot(page, sized.w, sized.h);
  const rect = clampRectToCanvas({ x: spot.x, y: spot.y, w: sized.w, h: sized.h }, sizeType);

  const widget = {
    id,
    type,
    title:
      typeof options.title === "string" && options.title.trim() ? options.title.trim() : entityFriendlyName(entityId) || id,
    entity_id: entityId,
    secondary_entity_id: secondaryEntityId,
    rect,
  };
  if (typeof options.displayMode === "string" && options.displayMode) {
    widget.display_mode = options.displayMode;
  }
  autoGaugeName(widget);
  if (type === "slider") {
    widget.slider_entity_domain = DEFAULT_SLIDER_ENTITY_DOMAIN;
    widget.slider_direction = String(entityId || "").startsWith("cover.") ? "bottom_to_top" : DEFAULT_SLIDER_DIRECTION;
    widget.slider_accent_color = DEFAULT_SLIDER_ACCENT_COLOR;
  }
  if (type === "button") {
    widget.button_mode = DEFAULT_BUTTON_MODE;
    widget.button_accent_color = DEFAULT_BUTTON_ACCENT_COLOR;
  }
  if (type === "graph") {
    widget.graph_line_color = DEFAULT_GRAPH_LINE_COLOR;
    widget.graph_time_window_min = DEFAULT_GRAPH_TIME_WINDOW_MIN;
  }
  if (type === "solar_forecast") {
    widget.title = "Solar Forecast";
    widget.forecast_today_entity_id = DEFAULT_SOLCAST_ENTITIES.today;
    widget.forecast_tomorrow_entity_id = DEFAULT_SOLCAST_ENTITIES.tomorrow;
    widget.forecast_day_3_entity_id = DEFAULT_SOLCAST_ENTITIES.day3;
    widget.forecast_day_4_entity_id = DEFAULT_SOLCAST_ENTITIES.day4;
    widget.forecast_day_5_entity_id = DEFAULT_SOLCAST_ENTITIES.day5;
    widget.solar_forecast_bar_orientation = DEFAULT_SOLAR_FORECAST_BAR_ORIENTATION;
  }
  if (type === "car_widget") {
    widget.title = typeof options.title === "string" && options.title.trim() ? options.title.trim() : "Car";
    widget.secondary_entity_id = "";
    widget.tertiary_entity_id = "";
  }
  if (type === "bambu_printer") {
    widget.title = typeof options.title === "string" && options.title.trim() ? options.title.trim() : "Bambu printer";
  }
  if (type === "room_tile") {
    widget.secondary_entity_id = undefined;
    delete widget.secondary_entity_id;
    widget.room_entities = Array.isArray(options.roomEntities) ? options.roomEntities.slice(0, 7) : [];
  }
  if (type === "vacuum_tile") {
    const battery = suggestVacuumBatterySensor(entityId);
    if (battery) widget.battery_entity_id = battery;
  }
  if (type === "sonos_favorites") {
    widget.title = typeof options.title === "string" && options.title.trim() ? options.title.trim() : "Sonos Favorites";
    widget.secondary_entity_id = DEFAULT_SONOS_FAVORITES_SENSOR;
  }

  page.widgets.push(widget);
  editor.selectedWidgetId = id;
  renderAll();
  return widget;
}

// Where a copy of `rect` goes on `page`: right of the original, else below it, else the first free
// spot (findFreeSpot() cascades when the page is full).
function duplicateWidgetSpot(page, rect) {
  const rects = (page?.widgets || []).map((widget) => widget.rect).filter(Boolean);
  const free = (x, y) => x >= 0 && y >= 0 && x + rect.w <= CANVAS_WIDTH && y + rect.h <= CANVAS_HEIGHT &&
    !rects.some((r) => x < r.x + r.w && x + rect.w > r.x && y < r.y + r.h && y + rect.h > r.y);
  for (const [x, y] of [[rect.x + rect.w + GRID, rect.y], [rect.x, rect.y + rect.h + GRID]]) {
    if (free(x, y)) return { x, y };
  }
  return findFreeSpot(page, rect.w, rect.h);
}

// Copies a widget with all its settings onto the same page, next to the original, and selects the copy.
function duplicateWidget(widgetId = editor.selectedWidgetId) {
  layoutHistoryBoundary();
  const page = selectedPage();
  const widget = page?.widgets?.find((w) => w.id === widgetId);
  if (!page || !widget || isFixedPage(page)) return null;
  if (page.widgets.length >= MAX_WIDGETS_PER_PAGE) {
    setStatus(t("layout.status.max_widgets_page", { max: MAX_WIDGETS_PER_PAGE }), true);
    return null;
  }
  const copy = JSON.parse(JSON.stringify(widget));
  copy.id = createWidgetIdForPage(page, copy.type);
  const rect = copy.rect || { x: 0, y: 0, w: 120, h: 120 };
  copy.rect = { ...rect, ...duplicateWidgetSpot(page, rect) };
  page.widgets.splice(page.widgets.indexOf(widget) + 1, 0, copy);
  editor.selectedWidgetId = copy.id;
  renderAll();
  setStatus(t("layout.status.widget_duplicated", { name: widgetDisplayLabel(copy) }));
  return copy;
}

function deleteWidget() {
  layoutHistoryBoundary();
  const page = selectedPage();
  if (!page || !editor.selectedWidgetId) return;
  const widget = page.widgets.find((w) => w.id === editor.selectedWidgetId);
  if (!widget) return;
  const name = widgetDisplayLabel(widget);
  if (!window.confirm(t("layout.widgets.confirm_delete", { name }))) return;
  page.widgets = page.widgets.filter((w) => w.id !== editor.selectedWidgetId);
  editor.selectedWidgetId = null;
  renderAll();
  offerUndo(t("layout.widgets.deleted_undo", { name }), () => layoutHistoryStep(-1));
}

function renderInspectorChange(refreshInspector) {
  if (refreshInspector) {
    renderAll(); // marks dirty on its own
    return;
  }
  renderWidgets();
  renderCanvas();
  markDirty(); // autoApplyInspector()'s live-as-you-type path -- most inspector edits go through here
  refreshInspectorLayout();
}

function applyInspector(options = {}) {
  const widget = selectedWidget();
  if (!widget) return false;

  const refreshInspector = options.refreshInspector !== false;
  const softEntityValidation = options.softEntityValidation === true;

  const widgetType = widget.type;
  const sliderDomain = widgetType === "slider"
    ? normalizeSliderEntityDomain(el.fSliderEntityDomain?.value)
    : DEFAULT_SLIDER_ENTITY_DOMAIN;
  const buttonMode = widgetType === "button"
    ? normalizeButtonMode(el.fButtonMode?.value)
    : DEFAULT_BUTTON_MODE;
  const secondaryConfig = secondaryEntityConfigForWidgetType(widgetType);
  const tertiaryConfig = tertiaryEntityConfigForWidgetType(widgetType);
  const nextEntityId = el.fEntity.value.trim() || pickDefaultEntityForWidgetType(widgetType, sliderDomain, buttonMode);

  const primaryEntityValid = entityMatchesWidgetType({ id: nextEntityId }, widgetType, sliderDomain, buttonMode);
  if (!primaryEntityValid && !softEntityValidation) {
    const allowedDomains = allowedEntityDomainsForWidgetType(widgetType, sliderDomain, buttonMode);
    const allowedHint = allowedDomains.length ? allowedDomains.join(", ") : t("layout.status.expected_domain");
    setStatus(t("layout.status.entity_domain_required", { domains: allowedHint }), true);
    return false;
  }

  widget.title = el.fTitle.value.trim();
  const previousEntityId = String(widget.entity_id || "");
  if (primaryEntityValid) {
    widget.entity_id = nextEntityId;
  }
  if (widgetType === "solar_forecast") {
    // Store what the user typed; an emptied field stays empty (slot disabled).
    widget.forecast_today_entity_id = el.fSolarForecastToday?.value.trim() ?? "";
    widget.forecast_tomorrow_entity_id = el.fSolarForecastTomorrow?.value.trim() ?? "";
    widget.forecast_day_3_entity_id = el.fSolarForecastDay3?.value.trim() ?? "";
    widget.forecast_day_4_entity_id = el.fSolarForecastDay4?.value.trim() ?? "";
    widget.forecast_day_5_entity_id = el.fSolarForecastDay5?.value.trim() ?? "";
    widget.solar_forecast_bar_orientation = normalizeSolarForecastBarOrientation(el.fSolarForecastBarOrientation?.value);
    const barMax = Number(el.fSolarForecastBarMax?.value);
    if (Number.isFinite(barMax) && barMax > 0) {
      widget.solar_forecast_bar_max_kwh = barMax;
    } else {
      delete widget.solar_forecast_bar_max_kwh;
    }
    delete widget.secondary_entity_id;
  } else {
    delete widget.forecast_today_entity_id;
    delete widget.forecast_tomorrow_entity_id;
    delete widget.forecast_day_3_entity_id;
    delete widget.forecast_day_4_entity_id;
    delete widget.forecast_day_5_entity_id;
    delete widget.solar_forecast_bar_max_kwh;
    delete widget.solar_forecast_bar_orientation;
  }
  for (const [key, input] of [
    ["precond_entity_id", el.fCarPrecond],
    ["lock_entity_id", el.fCarLock],
    ["interior_temp_entity_id", el.fCarTemp],
  ]) {
    const value = widgetType === "car_widget" ? (input?.value.trim() ?? "") : "";
    if (value) widget[key] = value;
    else delete widget[key];
  }
  bambuSaveInspector(widget, widgetType);
  if (!evccSaveInspector(widget, widgetType)) return false;
  // Energy timeline: the two battery counters go together (the firmware ignores half a battery).
  for (const [key, input] of [
    ["battery_charge_entity_id", el.fEtlCharge],
    ["battery_discharge_entity_id", el.fEtlDischarge],
    ["battery_soc_entity_id", el.fEtlSoc],
    ["forecast_today_entity_id", el.fEtlForecast],
  ]) {
    const value = widgetType === "energy_timeline" ? (input?.value.trim() ?? "") : "";
    if (value) widget[key] = value;
    else if (key !== "forecast_today_entity_id" || widgetType !== "solar_forecast") delete widget[key];
  }
  if (widgetType !== "energy_timeline") {
    delete widget.battery_charge_entity_id;
    delete widget.battery_discharge_entity_id;
    delete widget.battery_soc_entity_id;
  }
  if (widgetType === "vacuum_tile") {
    const defaultMode = (el.fVacuumDefaultMode?.value || "").trim();
    if (defaultMode) widget.default_mode = defaultMode;
    else delete widget.default_mode;
    const battery = (el.fVacuumBattery?.value || "").trim();
    if (battery) widget.battery_entity_id = battery;
    else delete widget.battery_entity_id;
  } else {
    delete widget.default_mode;
    delete widget.battery_entity_id;
  }
  if (widgetType === "binary_tile") {
    const mode = el.fBinaryDisplayMode?.value || "";
    if (mode) widget.display_mode = mode;
    else delete widget.display_mode;
  } else {
    delete widget.display_mode;
  }
  if (widgetType === "room_tile") {
    // One entity id per line (or comma separated), at most 7 besides the main sensor.
    const roomEntities = roomEntitiesFromInspector();
    widget.room_entities = roomEntities;
    // The extra field: an entity of the card that fits the mode, else it is dropped (and the controls say why).
    const extraMode = el.fRoomExtraMode?.value || "default";
    const extraEntity = el.fRoomExtraEntity?.value || "";
    applyRoomExtra(widget, extraMode, extraEntity, widget.entity_id, roomEntities);
    refreshRoomExtraControls(widget.entity_id, roomEntities, extraMode, widget.room_extra_entity_id || extraEntity);
    if (el.fRoomControls?.checked) widget.room_controls = true;
    else delete widget.room_controls; // default: off
  } else {
    delete widget.room_controls;
    delete widget.room_entities;
    delete widget.room_extra_mode;
    delete widget.room_extra_entity_id;
  }
  if (widgetType === "weather_3day" && el.fWeatherShowPrecip && !el.fWeatherShowPrecip.checked) {
    widget.show_precipitation = false;
  } else {
    delete widget.show_precipitation; // default: shown
  }
  saveReadabilityFields(widget);
  if (widgetSupportsName(widgetType) && el.fShowName && el.fShowName.checked !== widgetNameDefault(widgetType)) {
    widget.show_name = el.fShowName.checked; // only a deviation from the type's default is stored
  } else {
    delete widget.show_name;
  }
  delete widget.sensor_show_label;
  if (widgetType === "camera_tile" && el.fCameraLive?.checked) widget.camera_live = true;
  else delete widget.camera_live; // off by default
  if (widgetType === "lock_tile" && el.fLockConfirm?.checked) {
    widget.lock_confirm = true;
  } else {
    delete widget.lock_confirm; // default: no confirmation
  }
  // parqet_portfolio: the dividends sensor and the percentages-only option (gain is the secondary entity, XIRR the tertiary one)
  const dividends = (el.fParqetDividends?.value || "").trim();
  if (widgetType === "parqet_portfolio" && dividends) widget.dividends_entity_id = dividends;
  else delete widget.dividends_entity_id;
  const portfolioReturn = (el.fParqetReturn?.value || "").trim();
  if (widgetType === "parqet_portfolio" && portfolioReturn) widget.return_entity_id = portfolioReturn;
  else delete widget.return_entity_id;
  if (widgetType === "parqet_portfolio" && el.fParqetHideAmounts?.checked) widget.parqet_hide_amounts = true;
  else delete widget.parqet_hide_amounts;
  if (widgetType === "parqet_portfolio" && el.fParqetSelector?.checked) widget.parqet_selector = true;
  else delete widget.parqet_selector;
  if (widgetType === "parqet_portfolio" && el.fParqetView?.value === "holdings") widget.parqet_view = "holdings"; // performance is the default
  else delete widget.parqet_view;
  const parqetInterval = el.fParqetInterval?.value || "";
  if (widgetType === "parqet_portfolio" && ["1d", "1w", "1m", "ytd"].includes(parqetInterval)) widget.parqet_interval = parqetInterval; // max (the sensors) is the default
  else delete widget.parqet_interval;
  // price_forecast: the price timeline card's options, all off by default; the average sensor is the secondary entity
  if (widgetType === "price_forecast" && el.fPriceDaySwitch?.checked) widget.price_day_switch = true;
  else delete widget.price_day_switch;
  if (widgetType === "price_forecast" && el.fPriceCheapTimes?.checked) widget.price_cheap_times = true;
  else delete widget.price_cheap_times;
  if (widgetType === "price_forecast" && el.fPriceLine?.checked) widget.price_line = true;
  else delete widget.price_line;
  const priceAverage = Number(String(el.fPriceAverage?.value ?? "").replace(",", "."));
  if (widgetType === "price_forecast" && Number.isFinite(priceAverage) && priceAverage > 0 && priceAverage <= 5) widget.price_average = priceAverage;
  else delete widget.price_average;
  const alarmChosen = ALARM_ACTION_FIELDS.filter(([key]) => el[key]?.checked).map(([, action]) => action);
  if (widgetType === "alarm_tile" && alarmChosen.length > 0 && alarmChosen.length < ALARM_ACTION_FIELDS.length) {
    widget.alarm_actions = alarmChosen; // only a deviation from "all actions" is stored
  } else {
    delete widget.alarm_actions;
  }
  if (["sensor", "binary_tile", "contact_tile", "lock_tile", "alarm_tile", "vacuum_tile"].includes(widgetType) && el.fHistoryPopup?.checked) {
    widget.history_popup = true;
  } else {
    delete widget.history_popup;
  }
  if (widgetType === "sensor" && el.fSensorShowAge && !el.fSensorShowAge.checked) {
    widget.sensor_show_age = false;
  } else {
    delete widget.sensor_show_age; // default: shown
  }
  if (widgetType !== "sensor") {
    delete widget.sensor_format;
    delete widget.sensor_unit;
  } else {
    // A choice the device class does not offer (hidden, or the list of another class still shown) keeps the saved
    // value for when such a sensor is chosen again; the first choice is the default and is not saved.
    const format = sensorFormatChoices(widget, nextEntityId);
    const formatValue = el.fSensorFormat?.value || "";
    if (format && format.values.includes(formatValue)) {
      if (formatValue === format.values[0]) delete widget.sensor_format;
      else widget.sensor_format = formatValue;
    }
    const unit = sensorUnitChoices(widget, nextEntityId);
    const unitValue = el.fSensorUnit?.value ?? "";
    if (unit && unit.values.includes(unitValue)) {
      if (!unitValue) delete widget.sensor_unit;
      else widget.sensor_unit = unitValue;
    }
  }
  if (secondaryConfig.enabled) {
    const typedSecondaryEntityId = el.fSecondaryEntity.value.trim();
    const defaultSecondaryEntityId = widgetType === "sonos_favorites"
      ? DEFAULT_SONOS_FAVORITES_SENSOR
      : pickDefaultSecondaryEntity(widgetType, secondaryConfig.domain);
    const secondaryEntityId = secondaryConfig.optional
      ? typedSecondaryEntityId
      : (typedSecondaryEntityId || defaultSecondaryEntityId);
    const effectiveSecondaryEntityId = widgetType === "sonos_favorites"
      ? (secondaryEntityId || DEFAULT_SONOS_FAVORITES_SENSOR)
      : secondaryEntityId;
    if (effectiveSecondaryEntityId.length > 0 && !effectiveSecondaryEntityId.startsWith(`${secondaryConfig.domain}.`)) {
      if (!softEntityValidation) {
        setStatus(t(secondaryConfig.invalidStatusKey), true);
        return false;
      }
    } else {
      widget.secondary_entity_id = effectiveSecondaryEntityId;
    }
  } else if (widgetType === "bambu_printer" || widgetType === "wallbox_evcc") {
    delete widget.secondary_entity_id; // its bindings are named fields (js/bambu-printer.js), not the generic slots
  } else {
    widget.secondary_entity_id = "";
  }
  if (tertiaryConfig.enabled) {
    const typedTertiaryEntityId = el.fTertiaryEntity?.value.trim() || "";
    const tertiaryEntityId = tertiaryConfig.optional
      ? typedTertiaryEntityId
      : (typedTertiaryEntityId || pickDefaultEntityForWidgetType(tertiaryConfig.domain));
    if (tertiaryEntityId.length > 0 && !tertiaryEntityId.startsWith(`${tertiaryConfig.domain}.`)) {
      if (!softEntityValidation) {
        setStatus(t(tertiaryConfig.invalidStatusKey), true);
        return false;
      }
    } else {
      widget.tertiary_entity_id = tertiaryEntityId;
    }
  } else {
    delete widget.tertiary_entity_id;
  }
  if (widgetType === "button") {
    widget.button_mode = buttonMode;
    widget.button_accent_color = normalizeHexColor(el.fButtonAccentColor?.value, DEFAULT_BUTTON_ACCENT_COLOR);
  } else {
    delete widget.button_mode;
    delete widget.button_accent_color;
  }
  if (widgetType === "slider") {
    widget.slider_entity_domain = sliderDomain;
    let direction = normalizeSliderDirection(el.fSliderDirection?.value);
    // A blind or shutter is dragged up and down: choose the vertical direction when a cover is picked.
    if (direction === "auto" && String(widget.entity_id || "").startsWith("cover.") && !previousEntityId.startsWith("cover.")) {
      direction = "bottom_to_top";
      if (el.fSliderDirection) el.fSliderDirection.value = direction;
    }
    widget.slider_direction = direction;
    widget.slider_accent_color = normalizeHexColor(el.fSliderAccentColor?.value, DEFAULT_SLIDER_ACCENT_COLOR);
  } else {
    delete widget.slider_entity_domain;
    delete widget.slider_direction;
    delete widget.slider_accent_color;
  }
  if (widgetType === "graph") {
    widget.graph_line_color = normalizeHexColor(el.fGraphLineColor?.value, DEFAULT_GRAPH_LINE_COLOR);
    widget.graph_time_window_min = normalizeGraphTimeWindowMin(el.fGraphTimeWindowMin?.value);
    widget.graph_display_mode = normalizeGraphDisplayMode(el.fGraphDisplayMode?.value);
    widget.graph_bar_bucket_min = normalizeGraphBarBucketMin(el.fGraphBarBucketMin?.value);
    if (el.fGraphHaHistory?.checked) {
      widget.graph_ha_history = true;
    } else {
      delete widget.graph_ha_history; // default: the panel collects its own history
    }
    const graphPointCount = normalizeGraphPointCount(el.fGraphPointCount?.value);
    if (graphPointCount > 0) {
      widget.graph_point_count = graphPointCount;
    } else {
      delete widget.graph_point_count;
    }
  } else {
    delete widget.graph_line_color;
    delete widget.graph_time_window_min;
    delete widget.graph_point_count;
    delete widget.graph_display_mode;
    delete widget.graph_bar_bucket_min;
    delete widget.graph_ha_history;
  }
  if (widgetType === "heating_tile") {
    const variant = el.fHeatingStyleVariant?.value === "arc_semi" ? "arc_semi" : "default";
    widget.style_variant = variant;
    if (variant === "arc_semi") {
      const opening = el.fHeatingArcOpening?.value;
      widget.arc_opening = ["left", "right", "top", "bottom"].includes(opening) ? opening : "left";
    } else {
      delete widget.arc_opening;
    }
  } else if (widgetType === "gauge") {
    widget.style_variant = GAUGE_STYLES.includes(el.fGaugeStyle?.value) ? el.fGaugeStyle.value : "linear";
    delete widget.arc_opening;
  } else if (widgetType === "energy_timeline") {
    if (el.fEtlStyle?.value === "nested") widget.style_variant = "nested";
    else delete widget.style_variant;
    delete widget.arc_opening;
  } else {
    delete widget.style_variant;
    delete widget.arc_opening;
  }
  if (widgetType === "gauge") {
    const min = Number(el.fGaugeMin?.value);
    const max = Number(el.fGaugeMax?.value);
    widget.gauge_min = Number.isFinite(min) ? min : 0;
    widget.gauge_max = Number.isFinite(max) && max > widget.gauge_min ? max : widget.gauge_min + 100;
    widget.gauge_bar_style = el.fGaugeBarStyle?.value === "segmented" ? "segmented" : "smooth";
    const segments = widget.gauge_bar_style === "segmented" ? normalizeGaugeSegmentCount(el.fGaugeSegmentCount?.value) : 0;
    if (segments) {
      widget.gauge_segment_count = segments;
    } else {
      delete widget.gauge_segment_count;
    }
    const colorMode = ["gradient", "range"].includes(el.fGaugeColorMode?.value) ? el.fGaugeColorMode.value : "";
    widget.gauge_color_mode = colorMode;
    if (colorMode === "gradient" || colorMode === "range") {
      widget.gauge_color_low = normalizeHexColor(el.fGaugeColorLow?.value, "#3b82f6");
      widget.gauge_color_high = normalizeHexColor(el.fGaugeColorHigh?.value, "#ef4444");
    } else {
      delete widget.gauge_color_low;
      delete widget.gauge_color_high;
    }
    if (colorMode === "range") {
      const targetLow = Number(el.fGaugeTargetLow?.value);
      const targetHigh = Number(el.fGaugeTargetHigh?.value);
      widget.gauge_target_low = Number.isFinite(targetLow) ? targetLow : widget.gauge_min;
      widget.gauge_target_high =
        Number.isFinite(targetHigh) && targetHigh > widget.gauge_target_low ? targetHigh : widget.gauge_target_low + 1;
      widget.gauge_target_color = normalizeHexColor(el.fGaugeTargetColor?.value, "#4fcb7e");
    } else {
      delete widget.gauge_target_low;
      delete widget.gauge_target_high;
      delete widget.gauge_target_color;
    }
  } else {
    delete widget.gauge_min;
    delete widget.gauge_max;
    delete widget.gauge_bar_style;
    delete widget.gauge_segment_count;
    delete widget.gauge_color_mode;
    delete widget.gauge_color_low;
    delete widget.gauge_color_high;
    delete widget.gauge_target_low;
    delete widget.gauge_target_high;
    delete widget.gauge_target_color;
  }
  const previousWidth = widget.rect.w;
  widget.rect = clampRectToCanvas(
    {
      x: Number(el.fX.value || 0),
      y: Number(el.fY.value || 0),
      w: Number(el.fW.value || widget.rect.w),
      h: Number(el.fH.value || widget.rect.h),
    },
    widgetSizeType(widget)
  );
  if (widget.rect.w !== previousWidth) autoGaugeName(widget);
  editor.selectedWidgetId = widget.id;
  renderInspectorChange(refreshInspector);
  return true;
}

function autoApplyInspector(options = {}) {
  const result = applyInspector({
    refreshInspector: false,
    ...options,
  });
  updateSensorEntityInfo();
  return result;
}

// A sensor or binary sensor names its device class (and unit) below the main entity: the tiles choose icons, state
// texts, filters and the Sensor tile's time format from it. The Sensor tile offers the time format for a timestamp
// sensor (device_class timestamp, or a format saved before the entity list was loaded).
function sensorEntityInfo(entityId) {
  if (!entityId) return {};
  return editor.entities.find((entity) => entity?.id === entityId) || editor.stateMeta?.get(entityId) || {};
}

function entityHasDeviceClass(entityId) {
  return /^(sensor|binary_sensor)\./.test(entityId || "");
}

// A binary sensor's class can mean something else than a sensor's of the same name (power: on/off, not watts).
function deviceClassLabel(deviceClass, entityId = "") {
  if (!deviceClass) return "";
  const fallback = t(`device_class.${deviceClass}`, {}, deviceClass);
  return String(entityId).startsWith("binary_sensor.") ? t(`device_class.binary.${deviceClass}`, {}, fallback) : fallback;
}

// "Device class: Temperature \u00B7 Unit: \u00B0C"; "none" only for an entity Home Assistant listed, so a list that is not
// loaded yet shows nothing rather than a wrong "none".
// The device class and unit of a sensor or binary sensor as chips below the entity field; "no device class" only
// for an entity the panel lists, nothing while its list or state is not loaded.
function entityClassChips(entityId) {
  if (!entityHasDeviceClass(entityId)) return [];
  const listed = editor.entities.find((entity) => entity?.id === entityId);
  const info = listed || editor.stateMeta?.get(entityId);
  if (!info) return [];
  const label = deviceClassLabel(info.device_class || "", entityId);
  if (!label && !listed) return [];
  const chips = [label ? { text: label, title: t("inspector.device_class") }
    : { text: t("inspector.device_class_none"), title: t("inspector.device_class"), none: true }];
  if (info.unit) chips.push({ text: info.unit, title: t("inspector.unit") });
  return chips;
}

// The Sensor tile's sensor_format and sensor_unit choices by device class (ha_sensor_value.h, w_sensor.c); the first
// value is the default. Without a known class a saved value names its class, so it stays editable before the entity
// list has loaded.
const SENSOR_FORMAT_CLASSES = {
  timestamp: { label: "sensor_time_format", values: ["datetime", "date", "time", "relative"] },
  duration: { label: "sensor_format", values: ["text", "clock", "raw"] },
  date: { label: "sensor_format", values: ["date", "relative"] },
};
const SENSOR_UNIT_CLASSES = {
  temperature: { label: "sensor_unit", values: ["", "degree", "hide"] }, // the default reads as the unit itself
  power: { label: "sensor_unit", values: ["", "auto", "hide"], defaultText: "ha" },
  monetary: { label: "sensor_currency", values: ["", "code", "hide"], defaultText: "symbol" },
  plain: { label: "sensor_unit", values: ["", "hide"], defaultText: "show" },
};
const SENSOR_UNIT_CLASS_OF = {
  temperature: "temperature", temperature_delta: "temperature", power: "power", apparent_power: "power",
  reactive_power: "power", energy: "power", energy_storage: "power", monetary: "monetary",
};
const SENSOR_NO_UNIT_CHOICE = ["timestamp", "date", "duration", "enum"];

function sensorFormatChoices(widget, entityId) {
  if ((el.fType?.value || widget?.type) !== "sensor" || !entityId.startsWith("sensor.")) return null;
  const dc = sensorEntityInfo(entityId).device_class || "";
  if (SENSOR_FORMAT_CLASSES[dc]) return SENSOR_FORMAT_CLASSES[dc];
  const saved = widget?.sensor_format;
  if (dc || !saved) return null;
  return ["text", "clock", "raw"].includes(saved) ? SENSOR_FORMAT_CLASSES.duration : SENSOR_FORMAT_CLASSES.timestamp;
}

function sensorUnitChoices(widget, entityId) {
  if ((el.fType?.value || widget?.type) !== "sensor" || !entityId.startsWith("sensor.")) return null;
  const info = sensorEntityInfo(entityId);
  const dc = info.device_class || "";
  if (SENSOR_UNIT_CLASS_OF[dc]) return SENSOR_UNIT_CLASSES[SENSOR_UNIT_CLASS_OF[dc]];
  if (info.unit && !SENSOR_NO_UNIT_CHOICE.includes(dc)) return SENSOR_UNIT_CLASSES.plain;
  const saved = widget?.sensor_unit;
  if (dc || !saved) return null;
  return SENSOR_UNIT_CLASSES[{ degree: "temperature", auto: "power", code: "monetary" }[saved] || "plain"];
}

function sensorChoiceText(choices, kind, value, unit) {
  if (kind !== "unit") return t(`layout.option.sensor_format.${value}`);
  if (value) return t(`layout.option.sensor_unit.${value}`);
  return choices.defaultText || !unit ? t(`layout.option.sensor_unit.${choices.defaultText || "show"}`) : unit;
}

// Fills a select with the class's choices (only when they change, so an open list is not rebuilt) and selects the
// saved value, or the default when the class does not offer it.
function fillSensorChoice(select, wrap, labelEl, choices, kind, saved, unit) {
  wrap?.classList.toggle("hidden", !choices);
  if (!choices || !select) return;
  if (labelEl) labelEl.textContent = t(`layout.inspector.${choices.label}`);
  const texts = choices.values.map((value) => sensorChoiceText(choices, kind, value, unit));
  const key = `${choices.values.join("|")}#${texts.join("|")}`;
  if (select.dataset?.choices !== key) {
    select.innerHTML = choices.values.map((value, i) => `<option value="${value}">${escapeHtml(texts[i])}</option>`).join("");
    if (select.dataset) select.dataset.choices = key;
  }
  select.value = choices.values.includes(saved || "") ? (saved || "") : choices.values[0];
}

function sensorTimeFormatShown(widget, entityId) {
  return sensorFormatChoices(widget, entityId) === SENSOR_FORMAT_CLASSES.timestamp;
}

function updateSensorEntityInfo(widget = selectedWidget()) {
  const type = el.fType?.value || widget?.type;
  const sensor = type === "sensor";
  const entityId = el.fEntity?.value.trim() || "";
  const chips = type === "empty_tile" || type === "wallbox_evcc" ? [] : entityClassChips(entityId);
  if (el.fEntityClass) {
    el.fEntityClass.innerHTML = chips.map((chip) => `<span class="entity-class-chip${chip.none ? " is-none" : ""}" `
      + `title="${escapeHtml(chip.title)}">${escapeHtml(chip.text)}</span>`).join("");
    el.fEntityClass.dataset.text = chips.map((chip) => chip.text).join(" · ");
    el.fEntityClass.classList.toggle("hidden", chips.length === 0);
  }
  const format = sensor ? sensorFormatChoices(widget, entityId) : null;
  const unit = sensor ? sensorUnitChoices(widget, entityId) : null;
  const shownWidget = sensor && widget?.type === "sensor" ? widget : {};
  fillSensorChoice(el.fSensorFormat, el.fSensorFormatWrap, el.fSensorFormatLabel, format, "format",
    format ? shownWidget.sensor_format || format.values[0] : "", "");
  fillSensorChoice(el.fSensorUnit, el.fSensorUnitWrap, el.fSensorUnitLabel, unit, "unit",
    shownWidget.sensor_unit || "", sensorEntityInfo(entityId).unit || "");
}

// The bar-look choice (smooth/segmented) only means anything for the linear gauge style; the
// radial style always uses the arc, so its wrapper hides whenever "Radial" is selected.
function updateGaugeBarStyleVisibility() {
  if (!el.fGaugeBarStyleWrap) return;
  const isLinear = !el.fGaugeStyle || el.fGaugeStyle.value !== "radial";
  el.fGaugeBarStyleWrap.classList.toggle("hidden", !isLinear);
  // The LED block count only applies to the segmented bar.
  if (el.fGaugeSegmentCountWrap) {
    el.fGaugeSegmentCountWrap.classList.toggle("hidden", !isLinear || el.fGaugeBarStyle?.value !== "segmented");
  }
}

// The color-by-value mode decides which of the extra fields below it are relevant: "gradient"
// needs the two endpoint colors (labeled "at minimum"/"at maximum"), "range" needs the target
// band's numbers, its own "in range" color, and the same two color pickers relabeled
// "below range"/"above range" (they write the same gauge_color_low/high fields either way, see
// applyInspector() -- only the label and which other fields are visible next to them differ).
function updateGaugeColorModeVisibility() {
  if (!el.fGaugeColorMode) return;
  const mode = el.fGaugeColorMode.value;
  const isGradient = mode === "gradient";
  const isRange = mode === "range";
  if (el.fGaugeColorLowHighWrap) el.fGaugeColorLowHighWrap.classList.toggle("hidden", !isGradient && !isRange);
  if (el.fGaugeTargetRangeWrap) el.fGaugeTargetRangeWrap.classList.toggle("hidden", !isRange);
  if (el.fGaugeTargetColorWrap) el.fGaugeTargetColorWrap.classList.toggle("hidden", !isRange);
  setTextById("fGaugeColorLowLabel", isRange ? "layout.gauge.color_below" : "layout.gauge.color_min");
  setTextById("fGaugeColorHighLabel", isRange ? "layout.gauge.color_above" : "layout.gauge.color_max");
}

function bindInspectorAutoApply(input, events = ["change"], options = {}) {
  if (!input) return;
  const handler = () => autoApplyInspector(options);
  for (const eventName of events) {
    input.addEventListener(eventName, handler);
  }
}

async function saveLayout() {
  if (isEnergyPage(selectedPage())) {
    applyEnergyPageConfig({ render: false });
  }
  normalizeLayoutWidgets(editor.layout);
  layoutHistoryBoundary();
  markDirty();
  setStatus(t("layout.status.saving"));
  const body = JSON.stringify(editor.layout);
  const bytes = new Blob([body]).size;
  const wireLimit = MAX_LAYOUT_BYTES * (editor.layout.orientation ? 2 : 1);
  if (bytes > wireLimit) {
    // Tab5 projections include transient aliases; the server enforces the stored 32 KiB limit.
    throw new Error(t("layout.status.too_large", { size: bytes, max: wireLimit }));
  }
  const response = await fetch("/api/layout", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body,
  });
  if (!response.ok) {
    let detail = await response.text();
    try {
      const json = JSON.parse(detail);
      detail = (json.errors || []).join(", ") || detail;
    } catch (_) {}
    throw new Error(detail);
  }
  setStatus(t("layout.status.saved"));
  clearDirty(body);
  // A save can fix (or introduce) a missing-entity reference; the banner otherwise only
  // catches up on the next periodic poll (up to 30 s later, see startEditor()). The device's
  // ha_client watchdog reclassifies entities asynchronously after a layout reload, not
  // instantly, so poll a couple of times shortly after too, same pattern as on startup.
  loadHaDiagnostics();
  window.setTimeout(loadHaDiagnostics, 3000);
  window.setTimeout(loadHaDiagnostics, 8000);
}

function exportLayout() {
  if (isEnergyPage(selectedPage())) {
    applyEnergyPageConfig({ render: false });
  }
  normalizeLayoutWidgets(editor.layout);
  const blob = new Blob([JSON.stringify(editor.layout, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  const deviceId = (editor.settings && editor.settings.device && editor.settings.device.id) || "panel";
  a.download = `betta-layout-${deviceId}-${new Date().toISOString().slice(0, 10)}.json`;
  a.click();
  URL.revokeObjectURL(url);
}

// A widget outside the current device's canvas most often means the file was exported from a
// panel with a different screen size (e.g. panel10's 1280x680 imported into panel4's 720x600), not
// a corrupt layout: still worth importing (everything is still editable), but worth a clear warning
// instead of the widget silently landing off-screen until it is noticed by chance.
function countWidgetsOutsideCanvas(layout) {
  let count = 0;
  for (const page of layout.pages) {
    if (!page || isFixedPage(page) || !Array.isArray(page.widgets)) continue;
    for (const widget of page.widgets) {
      const rect = widget?.rect;
      if (!rect) continue;
      if (rect.x < 0 || rect.y < 0 || rect.x + rect.w > CANVAS_WIDTH || rect.y + rect.h > CANVAS_HEIGHT) count++;
    }
  }
  return count;
}

function importLayoutFromText(text) {
  // The panel itself refuses more than APP_LAYOUT_MAX_JSON_LEN at Save; checking it here too means
  // that is known right away instead of only after editing and trying to save.
  const bytes = new Blob([text]).size;
  if (bytes > MAX_LAYOUT_BYTES) {
    throw new Error(t("layout.status.too_large", { size: bytes, max: MAX_LAYOUT_BYTES }));
  }
  layoutHistoryBoundary();
  const parsed = JSON.parse(text); // a syntax error's own message is already specific enough
  if (!parsed || !Array.isArray(parsed.pages)) {
    throw new Error(t("layout.status.invalid_json"));
  }
  const badPageIndex = parsed.pages.findIndex((page) => !page || typeof page !== "object" || typeof page.id !== "string");
  if (badPageIndex >= 0) {
    throw new Error(t("layout.status.invalid_json_page", { index: badPageIndex }));
  }
  validateLayoutStructure(parsed);
  if (parsed.orientation && editor.layout?.orientation && parsed.orientation !== editor.layout.orientation) {
    throw new Error(t("layout.status.orientation_import"));
  }
  normalizeLayoutWidgets(parsed);
  const outOfBounds = countWidgetsOutsideCanvas(parsed);
  editor.layout = parsed;
  editor.selectedPageId = parsed.pages[0]?.id || null;
  editor.selectedWidgetId = null;
  renderAll();
  setStatus(
    outOfBounds > 0
      ? t("layout.status.imported_out_of_bounds", { count: outOfBounds, w: CANVAS_WIDTH, h: CANVAS_HEIGHT })
      : t("layout.status.imported"),
  );
}
