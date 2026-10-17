/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Event binding, editor start and bootstrap
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
function bindUi() {
  bindLayoutHistory();
  bindWidgetActions();
  setupSettingsWorkspace();
  for (const button of el.settingsNavButtons || []) {
    button.onclick = () => setActiveSettingsSection(button.dataset.settingsSection);
  }
  if (el.togglePagesSection) {
    el.togglePagesSection.onclick = () => toggleSection("pages"); // was never bound: only widgets and inspector collapsed
  }
  if (el.toggleWidgetsSection) {
    el.toggleWidgetsSection.onclick = () => toggleSection("widgets");
  }
  applySectionCollapseState();

  el.layoutTabBtn.onclick = () => setActivePane("layout");
  el.settingsTabBtn.onclick = async () => {
    setActivePane("settings");
    await loadSettings(true);
    await loadOtaStatus(true);
  };
  el.addPageBtn.onclick = addPage;
  if (el.addEnergyPageBtn) {
    el.addEnergyPageBtn.onclick = addEnergyPage;
  }
  el.deletePageBtn.onclick = deletePage;
  el.applyPageBtn.onclick = applyPageName;
  const pageIconSelect = document.getElementById("pageIconSelect");
  if (pageIconSelect) {
    pageIconSelect.addEventListener("change", () => {
      const page = selectedPage();
      if (!page) return;
      if (pageIconSelect.value) page.icon = pageIconSelect.value;
      else delete page.icon;
      renderPages();
      markDirty();
    });
  }
  // A click on the empty canvas (not on a tile) deselects the tile, Esc does too.
  if (el.canvasWrap) {
    el.canvasWrap.addEventListener("pointerdown", (event) => {
      if (!event.target.closest(".widget-box")) deselectWidget();
    });
  }
  document.addEventListener("keydown", (event) => {
    if (!layoutShortcutAllowed(event)) return;
    if (event.key === "Escape") {
      deselectWidget();
    } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "d" && editor.selectedWidgetId) {
      event.preventDefault(); // the browser's "bookmark this page"
      duplicateWidget();
    }
  });
  initSimpleUiMenus();
  if (el.applyEnergyPageBtn) {
    el.applyEnergyPageBtn.onclick = () => applyEnergyPageConfig();
  }
  if (el.energyDetails) {
    el.energyDetails.onchange = () => applyEnergyPageConfig();
  }
  if (el.energySource) {
    el.energySource.onchange = () => applyEnergyPageConfig();
  }
  for (const input of Object.values(getEnergyInputs())) {
    if (!input) continue;
    input.onchange = () => applyEnergyPageConfig();
    input.onblur = () => applyEnergyPageConfig();
  }
  el.addSensorBtn.onclick = () => openLightEntityPicker("sensor");
  el.addButtonBtn.onclick = () => openLightEntityPicker("button");
  el.addSliderBtn.onclick = () => addWidget("slider");
  el.addGaugeBtn.onclick = () => openLightEntityPicker("gauge");
  el.addGraphBtn.onclick = () => openLightEntityPicker("graph");
  el.addEmptyTileBtn.onclick = () => addWidget("empty_tile");
  el.addLightTileBtn.onclick = () => openLightEntityPicker("light_tile");
  el.addHeatingTileBtn.onclick = () => openLightEntityPicker("heating_tile");
  el.addWeatherTileBtn.onclick = () => openLightEntityPicker("weather_tile");
  el.addWeather3DayBtn.onclick = () => openLightEntityPicker("weather_3day");
  if (el.addSolarForecastBtn) {
    el.addSolarForecastBtn.onclick = () => addWidget("solar_forecast");
  }
  if (el.addCarWidgetBtn) {
    el.addCarWidgetBtn.onclick = () => openLightEntityPicker("car_widget");
  }
  if (el.addEvccWallboxBtn) el.addEvccWallboxBtn.onclick = () => void addEvccWallboxWidget();
if (el.fEvccDetectBtn) el.fEvccDetectBtn.onclick = () => void evccDetectLoadpoints();
if (el.fEvccLoadpoint) el.fEvccLoadpoint.addEventListener("change", () => evccChooseLoadpoint(el.fEvccLoadpoint.value));
for (const input of evccInspectorInputs()) {
  input.addEventListener("change", () => { if (input === el.fEvccMode) el.fEntity.value = input.value; autoApplyInspector(); });
  input.addEventListener("blur", () => { if (input === el.fEvccMode) el.fEntity.value = input.value; autoApplyInspector(); });
}
if (el.addBambuPrinterBtn) {
    el.addBambuPrinterBtn.onclick = () => void addBambuPrinterWidget();
  }
  if (el.fBambuDetectBtn) el.fBambuDetectBtn.onclick = () => void bambuDetectPrinters({ autoPick: false });
  if (el.fBambuPrinter) {
    el.fBambuPrinter.addEventListener("change", () => bambuChoosePrinter(el.fBambuPrinter.value));
  }
  if (el.addTodoListBtn) {
    el.addTodoListBtn.onclick = () => openLightEntityPicker("todo_list");
  }
  if (el.addMediaPlayerBtn) {
    el.addMediaPlayerBtn.onclick = () => openLightEntityPicker("media_player");
  }
  if (el.addSonosFavoritesBtn) {
    el.addSonosFavoritesBtn.onclick = () => openLightEntityPicker("sonos_favorites", { resetSearch: true });
  }
  if (el.addFanTileBtn) {
    el.addFanTileBtn.onclick = () => openLightEntityPicker("fan_tile");
  }
  if (el.addCameraTileBtn) {
    el.addCameraTileBtn.onclick = () => openLightEntityPicker("camera_tile");
  }
  if (el.addContactTileBtn) {
    el.addContactTileBtn.onclick = () => openLightEntityPicker("contact_tile");
  }
  if (el.addSceneTileBtn) el.addSceneTileBtn.onclick = () => openLightEntityPicker("scene_tile");
  if (el.addSelectTileBtn) el.addSelectTileBtn.onclick = () => openLightEntityPicker("select_tile");
  if (el.addEnergyTimelineBtn) el.addEnergyTimelineBtn.onclick = () => openLightEntityPicker("energy_timeline");
  if (el.addPriceForecastBtn) el.addPriceForecastBtn.onclick = () => openLightEntityPicker("price_forecast");
  if (el.addDatetimeTileBtn) el.addDatetimeTileBtn.onclick = () => openLightEntityPicker("datetime_tile");
  if (el.addLockTileBtn) el.addLockTileBtn.onclick = () => openLightEntityPicker("lock_tile");
  if (el.addAlarmTileBtn) el.addAlarmTileBtn.onclick = () => openLightEntityPicker("alarm_tile");
  if (el.addScriptTileBtn) el.addScriptTileBtn.onclick = () => openLightEntityPicker("script_tile");
  if (el.addDoorbellTileBtn) el.addDoorbellTileBtn.onclick = () => openLightEntityPicker("doorbell_tile");
  if (el.addBinaryTileBtn) el.addBinaryTileBtn.onclick = () => openLightEntityPicker("binary_tile");
  if (el.addRoomTileBtn) {
    el.addRoomTileBtn.onclick = () => openLightEntityPicker("room_tile");
  }
  if (el.addClimateTileBtn) {
    el.addClimateTileBtn.onclick = () => openLightEntityPicker("climate_tile");
  }
  if (el.addCoverTileBtn) {
    el.addCoverTileBtn.onclick = () => openLightEntityPicker("cover_tile");
  }
  if (el.addVacuumTileBtn) {
    el.addVacuumTileBtn.onclick = () => openLightEntityPicker("vacuum_tile");
  }
  if (el.lightEntityPickerRefreshBtn) {
    el.lightEntityPickerRefreshBtn.onclick = () => {
      const config = entityPickerConfig();
      const cacheKey = entityPickerCacheKey(config.domain);
      editor.lightPicker.hasLoaded = false;
      editor.lightPicker.loadedByDomain[cacheKey] = false;
      clearLightEntityPickerPoll();
      clearLightEntityPickerSearchDebounce();
      void fetchLightEntityPicker({ refresh: true });
    };
  }
  if (el.lightEntityPickerSearch) {
    el.lightEntityPickerSearch.oninput = () => {
      const config = entityPickerConfig();
      const search = el.lightEntityPickerSearch.value || "";
      editor.lightPicker.search = search;
      editor.lightPicker.searchByDomain[config.domain] = search;
      const cacheKey = entityPickerCacheKey(config.domain, search);
      editor.lightPicker.items = editor.lightPicker.itemsByDomain[cacheKey] || [];
      editor.lightPicker.hasLoaded = editor.lightPicker.loadedByDomain[cacheKey] === true;
      clearLightEntityPickerPoll();
      clearLightEntityPickerSearchDebounce();
      const shouldAutoFetch =
        !editor.lightPicker.hasLoaded && entityPickerSearchReady(config) && entityPickerLiveSearchEnabled(config);
      editor.lightPicker.requestSeq += 1;
      editor.lightPicker.loading = shouldAutoFetch;
      renderLightEntityPicker({
        status: editor.lightPicker.hasLoaded ? "ready" : (shouldAutoFetch ? "pending" : "idle"),
        pending: shouldAutoFetch,
        items: editor.lightPicker.items,
      });
      if (shouldAutoFetch) {
        editor.lightPicker.searchDebounceId = window.setTimeout(() => {
          editor.lightPicker.searchDebounceId = null;
          void fetchLightEntityPicker({ refresh: true });
        }, ENTITY_PICKER_SEARCH_DEBOUNCE_MS);
      }
    };
    el.lightEntityPickerSearch.onkeydown = (event) => {
      if (event.key !== "Enter") return;
      event.preventDefault();
      const config = entityPickerConfig();
      const cacheKey = entityPickerCacheKey(config.domain);
      editor.lightPicker.hasLoaded = false;
      editor.lightPicker.loadedByDomain[cacheKey] = false;
      clearLightEntityPickerPoll();
      clearLightEntityPickerSearchDebounce();
      void fetchLightEntityPicker({ refresh: true });
    };
  }
  if (el.lightEntityPickerCloseBtn) {
    el.lightEntityPickerCloseBtn.onclick = closeLightEntityPicker;
  }
  if (el.lightEntityPickerBlankBtn) {
    el.lightEntityPickerBlankBtn.onclick = () => {
      // The picker key (for example "doorbell_tile" or "script_tile") maps to the real widget type.
      const config = entityPickerConfig();
      addWidget(config.widgetType || editor.lightPicker.widgetType || "light_tile", { displayMode: config.displayMode });
      closeLightEntityPicker();
    };
  }
  if (el.lightEntityPickerOverlay) {
    el.lightEntityPickerOverlay.addEventListener("click", (event) => {
      if (event.target === el.lightEntityPickerOverlay) {
        closeLightEntityPicker();
      }
    });
  }
  bindAddTileDialog();
  if (el.duplicateWidgetBtn) el.duplicateWidgetBtn.onclick = () => duplicateWidget();
  el.deleteWidgetBtn.onclick = deleteWidget;
  if (el.undoToastBtn) {
    el.undoToastBtn.onclick = () => {
      const restore = pendingUndo;
      hideUndoToast();
      if (restore) restore();
    };
  }
  if (el.widgetSearchInput) {
    el.widgetSearchInput.oninput = () => {
      editor.widgetFilter = el.widgetSearchInput.value;
      renderWidgets();
    };
  }
  if (el.applyInspectorBtn) {
    el.applyInspectorBtn.onclick = () => applyInspector();
  }
  el.reloadBtn.onclick = () => {
    if (editor.dirty && !window.confirm(t("layout.confirm_discard_reload"))) {
      return;
    }
    loadLayout();
  };
  el.fType.onchange = () => {
    const sliderDomain = normalizeSliderEntityDomain(el.fSliderEntityDomain?.value);
    const buttonMode = normalizeButtonMode(el.fButtonMode?.value);
    if (el.buttonOptions) {
      el.buttonOptions.classList.toggle("hidden", el.fType.value !== "button");
    }
    if (el.sliderOptions) {
      el.sliderOptions.classList.toggle("hidden", el.fType.value !== "slider");
    }
    if (el.graphOptions) {
      el.graphOptions.classList.toggle("hidden", el.fType.value !== "graph");
    }
    if (el.fType.value === "button") {
      if (el.fButtonMode) {
        el.fButtonMode.value = buttonMode;
      }
      if (el.fButtonAccentColor) {
        el.fButtonAccentColor.value = normalizeHexColor(el.fButtonAccentColor.value, DEFAULT_BUTTON_ACCENT_COLOR);
      }
    } else {
      if (el.fButtonMode) {
        el.fButtonMode.value = DEFAULT_BUTTON_MODE;
      }
      if (el.fButtonAccentColor) {
        el.fButtonAccentColor.value = DEFAULT_BUTTON_ACCENT_COLOR;
      }
    }
    if (el.fType.value === "slider") {
      if (el.fSliderEntityDomain) {
        el.fSliderEntityDomain.value = sliderDomain;
      }
      if (el.fSliderDirection) {
        el.fSliderDirection.value = normalizeSliderDirection(el.fSliderDirection.value);
      }
      if (el.fSliderAccentColor) {
        el.fSliderAccentColor.value = normalizeHexColor(el.fSliderAccentColor.value, DEFAULT_SLIDER_ACCENT_COLOR);
      }
    }
    if (el.fType.value === "graph") {
      if (el.fGraphLineColor) {
        el.fGraphLineColor.value = normalizeHexColor(el.fGraphLineColor.value, DEFAULT_GRAPH_LINE_COLOR);
      }
      if (el.fGraphTimeWindowMin) {
        el.fGraphTimeWindowMin.value = String(normalizeGraphTimeWindowMin(el.fGraphTimeWindowMin.value));
      }
      if (el.fGraphPointCount) {
        const normalizedGraphPoints = normalizeGraphPointCount(el.fGraphPointCount.value);
        el.fGraphPointCount.value = normalizedGraphPoints > 0 ? String(normalizedGraphPoints) : "";
      }
      if (el.fGraphDisplayMode) {
        el.fGraphDisplayMode.value = normalizeGraphDisplayMode(el.fGraphDisplayMode.value);
      }
      if (el.fGraphBarBucketMin) {
        el.fGraphBarBucketMin.value = String(normalizeGraphBarBucketMin(el.fGraphBarBucketMin.value));
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
    }
    renderEntityOptions();
    const currentEntity = el.fEntity.value.trim();
    const effectiveButtonMode = el.fType.value === "button"
      ? normalizeButtonMode(el.fButtonMode?.value)
      : DEFAULT_BUTTON_MODE;
    if (!entityMatchesWidgetType({ id: currentEntity }, el.fType.value, sliderDomain, effectiveButtonMode)) {
      el.fEntity.value = pickDefaultEntityForWidgetType(el.fType.value, sliderDomain, effectiveButtonMode);
    }
    if (el.fType.value === "heating_tile") {
      const sensorEntity = el.fSecondaryEntity.value.trim();
      if (!sensorEntity.startsWith("sensor.")) {
        el.fSecondaryEntity.value = pickDefaultSecondaryEntity("heating_tile", "sensor");
      }
    } else if (el.fType.value === "sonos_favorites") {
      const sensorEntity = el.fSecondaryEntity.value.trim();
      el.fSecondaryEntity.value = sensorEntity.startsWith("sensor.") ? sensorEntity : DEFAULT_SONOS_FAVORITES_SENSOR;
    } else if (el.fType.value === "car_widget") {
      const sensorEntity = el.fSecondaryEntity.value.trim();
      if (sensorEntity && !sensorEntity.startsWith("sensor.")) {
        el.fSecondaryEntity.value = "";
      }
      const rangeEntity = el.fTertiaryEntity?.value.trim() || "";
      if (el.fTertiaryEntity && rangeEntity && !rangeEntity.startsWith("sensor.")) {
        el.fTertiaryEntity.value = "";
      }
    } else {
      el.fSecondaryEntity.value = "";
      if (el.fTertiaryEntity) {
        el.fTertiaryEntity.value = "";
      }
    }
  };
  if (el.fEntity) {
    el.fEntity.oninput = () => scheduleEntityAutocomplete("primary");
    el.fEntity.onfocus = () => scheduleEntityAutocomplete("primary", true);
    el.fEntity.onchange = () => autoApplyInspector();
    el.fEntity.onblur = () => autoApplyInspector();
  }
  if (el.fButtonMode) {
    el.fButtonMode.onchange = () => {
      el.fButtonMode.value = normalizeButtonMode(el.fButtonMode.value);
      if (inspectorWidgetType() !== "button") return;
      renderEntityOptions();
      scheduleEntityAutocomplete("primary", true);
      const currentEntity = el.fEntity.value.trim();
      const buttonMode = inspectorButtonMode();
      if (!entityMatchesWidgetType({ id: currentEntity }, "button", DEFAULT_SLIDER_ENTITY_DOMAIN, buttonMode)) {
        el.fEntity.value = pickDefaultEntityForWidgetType("button", DEFAULT_SLIDER_ENTITY_DOMAIN, buttonMode);
      }
      autoApplyInspector();
    };
  }
  if (el.fSliderEntityDomain) {
    el.fSliderEntityDomain.onchange = () => {
      el.fSliderEntityDomain.value = normalizeSliderEntityDomain(el.fSliderEntityDomain.value);
      if (inspectorWidgetType() !== "slider") return;
      scheduleEntityAutocomplete("primary", true);
      const currentEntity = el.fEntity.value.trim();
      const sliderDomain = inspectorSliderEntityDomain();
      if (!entityMatchesWidgetType({ id: currentEntity }, "slider", sliderDomain)) {
        el.fEntity.value = pickDefaultEntityForWidgetType("slider", sliderDomain);
      }
      autoApplyInspector();
    };
  }
  if (el.fSecondaryEntity) {
    el.fSecondaryEntity.oninput = () => scheduleEntityAutocomplete("secondary");
    el.fSecondaryEntity.onfocus = () => scheduleEntityAutocomplete("secondary", true);
    el.fSecondaryEntity.onchange = () => autoApplyInspector();
    el.fSecondaryEntity.onblur = () => autoApplyInspector();
  }
  if (el.fTertiaryEntity) {
    el.fTertiaryEntity.oninput = () => scheduleEntityAutocomplete("tertiary");
    el.fTertiaryEntity.onfocus = () => scheduleEntityAutocomplete("tertiary", true);
    el.fTertiaryEntity.onchange = () => autoApplyInspector();
    el.fTertiaryEntity.onblur = () => autoApplyInspector();
  }
  document.querySelectorAll("[data-inspector-entity-target]").forEach((button) => {
    button.addEventListener("click", () => {
      openInspectorEntityPicker(button.dataset.inspectorEntityTarget || "");
    });
  });
  bindInspectorAutoApply(el.fTitle, ["input"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fButtonAccentColor, ["input", "change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSliderDirection, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSliderAccentColor, ["input", "change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphLineColor, ["input", "change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphTimeWindowMin, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphPointCount, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphDisplayMode, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fGraphBarBucketMin, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fHeatingStyleVariant, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fHeatingArcOpening, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastToday, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastTomorrow, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastDay3, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastDay4, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastDay5, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fRoomEntities, ["change", "blur"], { softEntityValidation: true });
  // Expand the default window before applying the Recorder selection to the model.
  el.fGraphHaHistory?.addEventListener("change", () => {
    if (el.fGraphHaHistory.checked && el.fGraphTimeWindowMin &&
        Number(el.fGraphTimeWindowMin.value) === DEFAULT_GRAPH_TIME_WINDOW_MIN) {
      el.fGraphTimeWindowMin.value = String(RECORDER_GRAPH_TIME_WINDOW_MIN);
    }
  });
  bindInspectorAutoApply(el.fGraphHaHistory, ["change"], { refreshInspector: true, softEntityValidation: true });
  // Choosing a room mode first refills the entity list, then auto-apply saves it.
  el.fRoomExtraMode?.addEventListener("change", () => {
    const widget = selectedWidget();
    if (widget?.type === "room_tile") refreshRoomExtraControls(widget.entity_id, roomEntitiesFromInspector(), el.fRoomExtraMode.value, "");
  });
  bindInspectorAutoApply(el.fRoomExtraMode, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fRoomExtraEntity, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fEtlStyle, ["change"], { softEntityValidation: true });
  for (const input of [el.fEtlCharge, el.fEtlDischarge, el.fEtlSoc, el.fEtlForecast]) {
    bindInspectorAutoApply(input, ["change", "blur"], { softEntityValidation: true });
  }
  for (const input of [el.fBambuNozzle, el.fBambuBed, el.fBambuProgress, el.fBambuRemaining, el.fBambuStage, el.fBambuJob, el.fBambuPause, el.fBambuResume, el.fBambuStop, el.fBambuCamera]) {
    bindInspectorAutoApply(input, ["change", "blur"], { softEntityValidation: true });
  }
  bindInspectorAutoApply(el.fCarPrecond, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fCarLock, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fCarTemp, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fBinaryDisplayMode, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fVacuumDefaultMode, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fVacuumBattery, ["change", "blur"], { softEntityValidation: true });
  if (el.fTertiaryEntity) el.fTertiaryEntity.addEventListener("change", () => loadVacuumModeOptions(el.fTertiaryEntity.value));
  // Opening the list re-reads the programs (the entity may have been added to the panel after the last look).
  if (el.fVacuumDefaultMode) {
    el.fVacuumDefaultMode.addEventListener("mousedown", () => {
      if (el.fVacuumDefaultMode.options.length <= 2) loadVacuumModeOptions(el.fTertiaryEntity?.value || selectedWidget()?.tertiary_entity_id);
    });
  }
  bindInspectorAutoApply(el.fSolarForecastBarMax, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSolarForecastBarOrientation, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fWeatherShowPrecip, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fRoomControls, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fForecastDays, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fAlignForecast, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fShowName, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fSensorShowAge, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fCameraLive, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fLockConfirm, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fPriceDaySwitch, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fPriceCheapTimes, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fPriceLine, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fPriceAverage, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fAlarmDisarm, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fAlarmArmHome, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fAlarmArmAway, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fAlarmArmNight, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fAlarmArmVacation, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fAlarmArmCustom, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fHistoryPopup, ["change"], { softEntityValidation: true });
  if (el.fGaugeStyle) {
    el.fGaugeStyle.addEventListener("change", updateGaugeBarStyleVisibility);
  }
  bindInspectorAutoApply(el.fGaugeStyle, ["change"], { softEntityValidation: true });
  if (el.fGaugeBarStyle) {
    el.fGaugeBarStyle.addEventListener("change", updateGaugeBarStyleVisibility);
  }
  bindInspectorAutoApply(el.fGaugeBarStyle, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeSegmentCount, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeMin, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeMax, ["change", "blur"], { softEntityValidation: true });
  if (el.fGaugeColorMode) {
    el.fGaugeColorMode.addEventListener("change", updateGaugeColorModeVisibility);
  }
  bindInspectorAutoApply(el.fGaugeColorMode, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeTargetLow, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeTargetHigh, ["change", "blur"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeColorLow, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeColorHigh, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fGaugeTargetColor, ["change"], { softEntityValidation: true });
  bindInspectorAutoApply(el.fX, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fY, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fW, ["change"], { refreshInspector: true, softEntityValidation: true });
  bindInspectorAutoApply(el.fH, ["change"], { refreshInspector: true, softEntityValidation: true });
  if (el.fMovePage) {
    el.fMovePage.onchange = () => {
      if (el.fMovePage.value) moveSelectedWidgetToPage(el.fMovePage.value);
    };
  }
  el.reloadSettingsBtn.onclick = async () => {
    if (editor.settingsDirty && !window.confirm(t("settings.confirm_discard_reload"))) return;
    await loadSettings();
    await loadOtaStatus(true);
  };
  if (el.startOtaUrlBtn) {
    el.startOtaUrlBtn.onclick = () => {
      void startOtaFromUrl();
    };
  }
  bindUpdateCheck();
  bindCrashBanner();
  initInspectorLayout();
  bindInspectorHints();
  if (el.uploadOtaBtn) {
    el.uploadOtaBtn.onclick = () => {
      void uploadOtaFile();
    };
  }
  el.scanWifiBtn.onclick = () => scanWifiNetworks("settings");
  el.settingsWifiScanResults.onchange = () => {
    const option = el.settingsWifiScanResults.selectedOptions?.[0];
    const ssid = option?.dataset?.ssid || "";
    if (ssid) {
      el.settingsWifiSsid.value = ssid;
    }
    const bssid = normalizeBssid(option?.dataset?.bssid || "");
    if (bssid && el.settingsWifiBssid) {
      el.settingsWifiBssid.value = bssid;
    }
  };
  if (el.settingsWifiCountryCode) {
    el.settingsWifiCountryCode.oninput = () => {
      const cleaned = (el.settingsWifiCountryCode.value || "")
        .toUpperCase()
        .replace(/[^A-Z]/g, "")
        .slice(0, 2);
      el.settingsWifiCountryCode.value = cleaned;
    };
  }
  if (el.settingsWifiBssid) {
    el.settingsWifiBssid.oninput = () => {
      const cleaned = (el.settingsWifiBssid.value || "")
        .toUpperCase()
        .replace(/[^0-9A-F:-]/g, "")
        .slice(0, 17);
      el.settingsWifiBssid.value = cleaned;
    };
  }
  if (el.settingsTopbarWeatherEntity) {
    el.settingsTopbarWeatherEntity.oninput = () => scheduleSettingsWeatherAutocomplete();
    el.settingsTopbarWeatherEntity.onfocus = () => scheduleSettingsWeatherAutocomplete(true);
  }
  if (el.settingsSaverWeatherEntity) {
    el.settingsSaverWeatherEntity.oninput = () => scheduleSettingsWeatherAutocomplete(false, el.settingsSaverWeatherEntity);
    el.settingsSaverWeatherEntity.onfocus = () => scheduleSettingsWeatherAutocomplete(true, el.settingsSaverWeatherEntity);
  }
  if (el.settingsSaverWeatherSearchBtn) {
    el.settingsSaverWeatherSearchBtn.onclick = () => openSettingsTopbarEntityPicker("weather", el.settingsSaverWeatherEntity);
  }
  if (el.settingsTopbarWeatherSearchBtn) {
    el.settingsTopbarWeatherSearchBtn.onclick = () => openSettingsTopbarEntityPicker("weather");
  }
  if (el.settingsTopbarStockEntity) {
    el.settingsTopbarStockEntity.oninput = () => scheduleSettingsStockAutocomplete(el.settingsTopbarStockEntity);
    el.settingsTopbarStockEntity.onfocus = () => scheduleSettingsStockAutocomplete(el.settingsTopbarStockEntity, true);
  }
  if (el.settingsTopbarStockSearchBtn) {
    el.settingsTopbarStockSearchBtn.onclick = () => openSettingsTopbarEntityPicker("stock");
  }
  if (el.settingsHardwareActiveBrightness) {
    el.settingsHardwareActiveBrightness.oninput = () => {
      setPercentSlider(
        el.settingsHardwareActiveBrightness,
        el.settingsHardwareActiveBrightnessValue,
        el.settingsHardwareActiveBrightness.value,
      );
      enforceBrightnessOrder("active");
    };
  }
  if (el.settingsHardwareDimBrightness) {
    el.settingsHardwareDimBrightness.oninput = () => {
      setPercentSlider(
        el.settingsHardwareDimBrightness,
        el.settingsHardwareDimBrightnessValue,
        el.settingsHardwareDimBrightness.value,
      );
      enforceBrightnessOrder("dim");
    };
  }
  if (el.settingsHardwarePresenceSensitivity) {
    el.settingsHardwarePresenceSensitivity.oninput = () => {
      if (el.settingsHardwarePresenceSensitivityValue) {
        el.settingsHardwarePresenceSensitivityValue.textContent = el.settingsHardwarePresenceSensitivity.value;
      }
    };
  }
  if (el.settingsHardwareNightBrightness) {
    el.settingsHardwareNightBrightness.oninput = () => {
      setPercentSlider(
        el.settingsHardwareNightBrightness,
        el.settingsHardwareNightBrightnessValue,
        el.settingsHardwareNightBrightness.value,
        0,
      );
      enforceBrightnessOrder("night");
    };
  }
  refreshBrightnessNow();
  setInterval(refreshBrightnessNow, 5000);
  refreshCameraBrightnessStatus();
  setInterval(refreshCameraBrightnessStatus, 5000);
  if (el.settingsHardwareAutoBrightness) {
    el.settingsHardwareAutoBrightness.addEventListener("change", refreshCameraBrightnessStatus);
  }
  if (el.settingsCameraTestBtn) {
    el.settingsCameraTestBtn.onclick = startCameraTest;
  }
  bindHardwareLive();
  bindBrightnessTests();
  initEditorTheme();
  if (el.settingsDeviceRoom) el.settingsDeviceRoom.addEventListener("input", renderDeviceId);
  bindSecurityControls();
  bindStatusControls();
  if (el.uploadLanguageCode) {
    el.uploadLanguageCode.oninput = () => {
      const cleaned = (el.uploadLanguageCode.value || "")
        .toLowerCase()
        .replace(/[^a-z0-9_-]/g, "")
        .slice(0, 15);
      el.uploadLanguageCode.value = cleaned;
    };
  }
  if (el.settingsUiStyle) {
    el.settingsUiStyle.onchange = () => {
      applyUiStyleAttribute(el.settingsUiStyle.value);
      updateStyleInfo();
    };
  }
  if (el.settingsHardwareResetBtn) el.settingsHardwareResetBtn.onclick = resetDisplayDefaults;
  if (el.settingsAppearanceResetBtn) el.settingsAppearanceResetBtn.onclick = resetAppearanceDefaults;
  if (el.settingsTopbarResetBtn) el.settingsTopbarResetBtn.onclick = resetTopbarDefaults;
  if (el.settingsLanguage) {
    el.settingsLanguage.onchange = async () => {
      const lang = normalizeUiLanguage(el.settingsLanguage.value);
      if (!editor.settings) editor.settings = {};
      if (!editor.settings.ui) editor.settings.ui = {};
      editor.settings.ui.language = lang;
      await loadI18nLanguage(lang);
      if (el.uploadLanguageCode) {
        el.uploadLanguageCode.value = lang;
      }
      renderSettings();
    };
  }
  if (el.reloadLanguagesBtn) {
    el.reloadLanguagesBtn.onclick = async () => {
      try {
        await loadLanguageCatalog();
        renderLanguageOptions();
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.info");
        }
      } catch (err) {
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.upload_fail", { error: err.message });
          el.settingsTranslationInfo.classList.add("error");
        }
      }
    };
  }
  if (el.downloadLanguageBtn) {
    el.downloadLanguageBtn.onclick = async () => {
      const lang = normalizeUiLanguage(el.settingsLanguage?.value || editor.i18nLanguage);
      await loadI18nLanguage(lang);
      downloadLanguageJson();
    };
  }
  if (el.uploadLanguageBtn) {
    el.uploadLanguageBtn.onclick = async () => {
      if (el.settingsTranslationInfo) {
        el.settingsTranslationInfo.classList.remove("error");
      }
      try {
        const targetLang = normalizeLanguageCode(el.uploadLanguageCode?.value, "");
        await uploadLanguageJson();
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.upload_ok", { lang: targetLang });
          el.settingsTranslationInfo.classList.remove("error");
        }
        if (el.uploadLanguageFile) {
          el.uploadLanguageFile.value = "";
        }
        await loadI18nLanguage(targetLang || editor.i18nLanguage, true);
        renderSettings();
      } catch (err) {
        if (el.settingsTranslationInfo) {
          el.settingsTranslationInfo.textContent = t("settings.translation.upload_fail", { error: err.message });
          el.settingsTranslationInfo.classList.add("error");
        }
      }
    };
  }
  if (el.provScanWifiBtn) {
    el.provScanWifiBtn.onclick = () => scanWifiNetworks("provisioning");
  }
  if (el.provWifiScanResults) {
    el.provWifiScanResults.onchange = () => {
      const option = el.provWifiScanResults.selectedOptions?.[0];
      const ssid = option?.dataset?.ssid || "";
      if (ssid && el.provWifiSsid) {
        el.provWifiSsid.value = ssid;
      }
    };
  }
  if (el.provWifiCountryCode) {
    el.provWifiCountryCode.oninput = () => {
      const cleaned = (el.provWifiCountryCode.value || "")
        .toUpperCase()
        .replace(/[^A-Z]/g, "")
        .slice(0, 2);
      el.provWifiCountryCode.value = cleaned;
    };
  }
  if (el.provWifiShowPassword && el.provWifiPassword) {
    el.provWifiShowPassword.onchange = () => {
      el.provWifiPassword.type = el.provWifiShowPassword.checked ? "text" : "password";
    };
  }
  if (el.provHaShowToken && el.provHaToken) {
    el.provHaShowToken.onchange = () => {
      el.provHaToken.type = el.provHaShowToken.checked ? "text" : "password";
    };
  }
  if (el.provHaUseUrlBtn && el.provHaUrl) {
    el.provHaUseUrlBtn.onclick = () => {
      el.provHaUrl.value = el.provHaUrl.placeholder;
      el.provHaUrl.dispatchEvent(new Event("input", { bubbles: true }));
      el.provHaUrl.focus();
    };
  }
  if (el.provWifiSaveBtn) {
    el.provWifiSaveBtn.onclick = async () => {
      try {
        await saveWifiProvisioning();
      } catch (err) {
        setProvisioningInfo("wifi", t("provision.save_failed", { error: err.message }), true);
      }
    };
  }
  if (el.provHaSaveBtn) {
    el.provHaSaveBtn.onclick = async () => {
      try {
        await saveHaProvisioning();
      } catch (err) {
        setProvisioningInfo("ha", t("provision.save_failed", { error: err.message }), true);
      }
    };
  }
  el.saveSettingsBtn.onclick = async () => {
    try {
      await saveSettings();
    } catch (err) {
      setStatus(t("status.settings_save_failed", { error: err.message }), true);
    }
  };
  el.saveBtn.onclick = async () => {
    try {
      await saveLayout();
    } catch (err) {
      setStatus(t("layout.status.save_failed", { error: err.message }), true);
    }
  };
  // Settings keep a Save shortcut in the sticky header below 1100 px; Layout always has its own header button.
  if (el.stackedSaveBtn) {
    el.stackedSaveBtn.onclick = () => (editor.activePane === "settings" ? el.saveSettingsBtn : el.saveBtn).click();
  }
  // Stacked, the page and widget lists would push the canvas below the fold; they start collapsed (the page chips of the
  // collapsed Pages card still switch pages). Side by side nothing changes.
  if (isStackedLayout()) {
    setSectionCollapsed("pages", true);
    setSectionCollapsed("widgets", true);
  }
  el.exportBtn.onclick = () => { exportLayout(); closeAllDropdowns(); };
  el.importBtn.onclick = () => {
    const text = el.jsonPaste.value.trim();
    if (!text) return;
    try {
      importLayoutFromText(text);
      closeAllDropdowns();
    } catch (err) {
      setStatus(t("layout.status.import_failed", { error: err.message }), true);
    }
  };
  el.importFile.onchange = async (event) => {
    const file = event.target.files?.[0];
    if (!file) return;
    const text = await file.text();
    try {
      importLayoutFromText(text);
      closeAllDropdowns();
    } catch (err) {
      setStatus(t("layout.status.file_import_failed", { error: err.message }), true);
    }
  };
}

async function startEditor() {
  if (editor.editorStarted) return;
  editor.editorStarted = true;
  setProvisioningVisible(false);
  setActivePane("layout");
  await Promise.all([loadLayout(), loadEntities(), refreshStates()]);
  await loadEnergyPreview();
  await loadHaDiagnostics();
  restoreEditorLocation();
  /* Poll diagnostics again shortly after startup so the banner appears automatically
   * once the ha_client watchdog has classified the missing entities (typically ~5-8 s
   * after WebSocket auth). Further refreshes are less frequent. */
  window.setTimeout(loadHaDiagnostics, 3000);
  window.setTimeout(loadHaDiagnostics, 8000);
  window.setTimeout(loadHaDiagnostics, 15000);
  window.setInterval(refreshStates, 5000);
  window.setInterval(loadEnergyPreview, 15000);
  window.setInterval(loadHaDiagnostics, 30000);
}

// First-time setup: the browser language, when the editor has a built-in translation for it.
function browserSetupLanguage() {
  const supported = ["de", "en"];
  const candidates = Array.isArray(navigator.languages) && navigator.languages.length
    ? navigator.languages
    : [navigator.language || ""];
  for (const entry of candidates) {
    const base = String(entry || "").toLowerCase().split("-")[0];
    if (supported.includes(base)) return base;
  }
  return DEFAULT_UI_LANGUAGE;
}

async function bootstrap() {
  bindUi();
  initThemeSection();
  window.addEventListener("resize", updateCanvasScale);
  el.settingsContentPane?.addEventListener("input", updateSettingsDirty);
  el.settingsContentPane?.addEventListener("change", updateSettingsDirty);
  window.addEventListener("beforeunload", (event) => {
    if (!editor.dirty && !editor.settingsDirty) return;
    // Most browsers ignore this string and show their own generic wording, but the
    // property still has to be set to trigger the "leave site?" prompt at all.
    event.preventDefault();
    event.returnValue = t("layout.confirm_discard_unload");
  });
  setStatus(t("status.idle"));
  await loadAppVersion();
  const settings = await loadSettingsForBootstrap();
  if (!settings) {
    showBootError();
    return;
  }
  const stage = provisioningStageForSettings(settings);
  // A panel that has no Wi-Fi yet and still has the default language: follow the browser and store it
  // together with the Wi-Fi settings, so the panel screens speak the same language afterwards.
  if (stage === "wifi" && settings && !settings.wifi?.configured &&
      normalizeUiLanguage(settings.ui?.language) === DEFAULT_UI_LANGUAGE) {
    const lang = browserSetupLanguage();
    if (lang !== DEFAULT_UI_LANGUAGE) {
      editor.provisioningLanguage = lang;
      await loadI18nLanguage(lang, true);
    }
  }
  if (stage) {
    showProvisioningStage(stage, settings);
    return;
  }
  await startEditor();
}
