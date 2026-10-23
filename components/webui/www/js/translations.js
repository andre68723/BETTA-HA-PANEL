/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Loading and applying translations, language files
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
function setSelectOptionText(select, value, key, vars) {
  if (!select || !select.options) return;
  for (const option of select.options) {
    if (option.value === value) {
      option.textContent = t(key, vars);
      return;
    }
  }
}

function renderLanguageOptions() {
  if (!el.settingsLanguage) return;

  const optionCodes = new Set();
  optionCodes.add(DEFAULT_UI_LANGUAGE);
  optionCodes.add("en");
  optionCodes.add("de");
  optionCodes.add(editor.i18nLanguage || DEFAULT_UI_LANGUAGE);
  for (const language of editor.languageCatalog || []) {
    const code = normalizeLanguageCode(language?.code, "");
    if (code) optionCodes.add(code);
  }

  const current = normalizeUiLanguage(editor.i18nLanguage || el.settingsLanguage.value);
  const sorted = Array.from(optionCodes).sort((a, b) => a.localeCompare(b));
  el.settingsLanguage.innerHTML = "";
  for (const code of sorted) {
    const option = document.createElement("option");
    option.value = code;
    const optionKey = `settings.language.option_${code}`;
    option.textContent = (editor.i18nMap && editor.i18nMap[optionKey]) || code.toUpperCase();
    el.settingsLanguage.appendChild(option);
  }
  el.settingsLanguage.value = sorted.includes(current) ? current : (sorted[0] || DEFAULT_UI_LANGUAGE);

  if (el.uploadLanguageCode) {
    el.uploadLanguageCode.value = el.settingsLanguage.value;
  }
}

async function loadLanguageCatalog() {
  const payload = await apiGet("/api/i18n/languages");
  if (!payload || !Array.isArray(payload.languages)) {
    throw new Error("Invalid language catalog");
  }
  editor.languageCatalog = payload.languages;
  return payload;
}

async function loadEffectiveTranslation(language) {
  const lang = normalizeUiLanguage(language);
  const response = await fetch(`/api/i18n/effective?lang=${encodeURIComponent(lang)}`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`${response.status} ${response.statusText}`);
  }
  return response.json();
}

async function loadI18nLanguage(language, refreshCatalog = false) {
  const lang = normalizeUiLanguage(language);

  if (refreshCatalog || !Array.isArray(editor.languageCatalog) || editor.languageCatalog.length === 0) {
    try {
      await loadLanguageCatalog();
    } catch (_) {}
  }

  let effective = {};
  try {
    effective = await loadEffectiveTranslation(lang);
  } catch (_) {
    effective = {};
  }

  const builtin = WEB_I18N_BUILTIN[lang] || {};
  const webCustom = flattenTranslationObject(effective?.web || {});
  editor.i18nMap = {
    ...(WEB_I18N_BUILTIN.en || {}),
    ...builtin,
    ...webCustom,
  };
  editor.i18nEffective = effective || {};
  editor.i18nLanguage = lang;
  document.documentElement.lang = lang;

  applyWebTranslations();
  if (typeof themePopulateSelect === "function") {
    const select = document.getElementById("settingsThemeSelect");
    const selected = select?.value;
    themePopulateSelect();
    if (select && selected) select.value = selected;
    themeRenderColorGrid(themeState.editing);
    themeSyncEditFields();
    themeRenderPreview();
  }
  renderLanguageOptions();
  if (editor.layout) {
    renderCanvas();
  }
}

function applyWebTranslations() {
  layoutHistoryControls();
  setTextById("layoutTabBtn", "tabs.layout");
  setTextById("settingsTabBtn", "tabs.settings");
  setTextById("sidebarTitleText", "sidebar.title");
  setTextById("sidebarSubtitle", "sidebar.subtitle");
  setTextById("deviceIdMetaLabel", "sidebar.device_label");
  setTextById("boardMetaLabel", "sidebar.type_label");
  setTextById("buildTimeMetaLabel", "sidebar.build_label");
  setTextById("settingsHardwareResetBtn", "settings.display.reset");
  setTextById("settingsAppearanceResetBtn", "settings.reset.button");
  setTextById("settingsTopbarResetBtn", "settings.reset.button");
  setTextById("settingsCameraTestBtn", "settings.camera.test_button");
  renderAppVersion();
  renderFirmwareInfo();

  setTextById("safeModeTitle", "safe_mode.title");
  setTextById("safeModeHint", "safe_mode.hint");
  setTextById("pagesHeading", "layout.pages.heading");
  setTextById("addPageBtn", "layout.pages.add");
  setTextById("addEnergyPageBtn", "layout.pages.add_energy");
  setTextById("deletePageBtn", "layout.pages.delete");
  setPlaceholderById("pageTitleInput", "layout.pages.title_placeholder");
  setTextById("applyPageBtn", "layout.pages.apply_title");
  setTextById("pageTitleLabel", "layout.pages.title_label");
  setTextById("pageIconLabel", "layout.pages.icon_label");
  setTextById("pageIconHint", "layout.pages.icon_hint");
  renderPageIconOptions();
  setTextById("pageInspectorHint", "layout.pages.inspector_hint");
  setTextById("energyPageHeading", "layout.energy.heading");
  setTextById("energyPageHint", "layout.energy.hint");
  setTextById("energySourceLabel", "layout.energy.source");
  setTextById("energyDetailsLabel", "layout.energy.details");
  setTextById("energyDetailsPopupOption", "layout.energy.details_popup");
  setTextById("energyDetailsSideOption", "layout.energy.details_side");
  setTextById("energyDetailsHint", "layout.energy.details_hint");
  setTextById("energySourceHaOption", "layout.energy.source_ha");
  setTextById("energySourceManualOption", "layout.energy.source_manual");
  setTextById("energyHomePowerLabel", "layout.energy.home_power");
  setTextById("energySolarPowerLabel", "layout.energy.solar_power");
  setTextById("energyGridPowerLabel", "layout.energy.grid_power");
  setTextById("energyGridImportLabel", "layout.energy.grid_import");
  setTextById("energyGridExportLabel", "layout.energy.grid_export");
  setTextById("energyBatteryPowerLabel", "layout.energy.battery_power");
  setTextById("energyBatteryChargeLabel", "layout.energy.battery_charge");
  setTextById("energyBatteryDischargeLabel", "layout.energy.battery_discharge");
  setTextById("energyBatterySocLabel", "layout.energy.battery_soc");
  setTextById("energyGasLabel", "layout.energy.gas");
  setTextById("energyWaterLabel", "layout.energy.water");
  setTextById("energyGasWaterHint", "layout.energy.gas_water_hint");
  setTextById("energySolarPowerHint", "layout.energy.solar_power_hint");
  setTextById("energyBatterySocHint", "layout.energy.battery_soc_hint");
  setTextById("applyEnergyPageBtn", "layout.energy.apply");

  setTextById("widgetsHeading", "layout.widgets.heading");
  setPlaceholderById("widgetSearchInput", "layout.widgets.search_placeholder");
  setTextById("addSensorBtn", "layout.widgets.add_sensor");
  setTextById("addButtonBtn", "layout.widgets.add_button");
  setTextById("addSliderBtn", "layout.widgets.add_slider");
  setTextById("addGaugeBtn", "layout.widgets.add_gauge");
  setTextById("addGraphBtn", "layout.widgets.add_graph");
  setTextById("addEmptyTileBtn", "layout.widgets.add_empty_tile");
  setTextById("addLightTileBtn", "layout.widgets.add_light_tile");
  setTextById("addHeatingTileBtn", "layout.widgets.add_heating_tile");
  setTextById("addWeatherTileBtn", "layout.widgets.add_weather_tile");
  setTextById("addWeather3DayBtn", "layout.widgets.add_weather_3day");
  setTextById("addSolarForecastBtn", "layout.widgets.add_solar_forecast");
  setTextById("addCarWidgetBtn", "layout.widgets.add_car");
  evccTranslate();
setTextById("addBambuPrinterBtn", "layout.widgets.add_bambu_printer");
  setTextById("addTodoListBtn", "layout.widgets.add_todo");
  setTextById("addMediaPlayerBtn", "layout.widgets.add_media_player");
  setTextById("addSonosFavoritesBtn", "layout.widgets.add_sonos_favorites");
  setTextById("addVacuumTileBtn", "layout.widgets.add_vacuum");
  setTextById("addCoverTileBtn", "layout.widgets.add_cover");
  setTextById("addClimateTileBtn", "layout.widgets.add_climate");
  setTextById("addRoomTileBtn", "layout.widgets.add_room");
  setTextById("addSceneTileBtn", "layout.widgets.add_scene");
  setTextById("addSelectTileBtn", "layout.widgets.add_select");
  setTextById("addDatetimeTileBtn", "layout.widgets.add_datetime");
  setTextById("addLockTileBtn", "layout.widgets.add_lock");
  setTextById("addAlarmTileBtn", "layout.widgets.add_alarm");
  setTextById("addEnergyTimelineBtn", "layout.widgets.add_energy_timeline");
  setTextById("addPriceForecastBtn", "layout.widgets.add_price_forecast");
  setTextById("addParqetPortfolioBtn", "layout.widgets.add_parqet_portfolio");
  setTextById("addStockQuotesBtn", "layout.widgets.add_stock_quotes");
  setTextById("followPageText", "layout.follow_page");
  document.getElementById("followPageLabel")?.setAttribute("title", t("layout.follow_page_title"));
  for (let n = 1; n <= 7; n++) setTextById(`fStock${n}Label`, "layout.inspector.stock_quote", { n: n + 1 });
  setTextById("fStockHint", "layout.inspector.stock_hint");
  setTextById("fStockViewLabel", "layout.inspector.stock_view");
  setTextById("fStockViewList", "layout.option.stock_view.list");
  setTextById("fStockViewChart", "layout.option.stock_view.chart");
  setTextById("fStockPeriodLabel", "layout.inspector.stock_period");
  for (const code of ["1d", "1w", "30d", "ytd", "1y"]) {
    setTextById(`fStockPeriod${code === "ytd" ? "Ytd" : code}`, `layout.option.stock_period.${code}`);
  }
  setTextById("fStockDeltaLabel", "layout.inspector.stock_delta");
  setTextById("fStockDeltaPercent", "layout.option.stock_delta.percent");
  setTextById("fStockDeltaAbsolute", "layout.option.stock_delta.absolute");
  setTextById("fStockDeltaBoth", "layout.option.stock_delta.both");
  setTextById("fStockTickerLabel", "layout.inspector.stock_ticker");
  setTextById("fCameraLiveLabel", "layout.inspector.camera_live");
  setTextById("cameraLiveHint", "layout.inspector.camera_live_hint");
  setTextById("fBambuCameraLabel", "layout.inspector.bambu_camera");
  setTextById("fLockConfirmLabel", "layout.inspector.lock_confirm");
  setTextById("fPriceDaySwitchLabel", "layout.inspector.price_day_switch");
  setTextById("fPriceCheapTimesLabel", "layout.inspector.price_cheap_times");
  setTextById("fPriceLineLabel", "layout.inspector.price_line");
  setTextById("fPriceAverageLabel", "layout.inspector.price_average");
  setTextById("fParqetHint", "layout.inspector.parqet_hint");
  setTextById("fParqetDividendsLabel", "layout.inspector.parqet_dividends");
  setTextById("fParqetReturnLabel", "layout.inspector.parqet_return");
  setTextById("fParqetSelectorLabel", "layout.inspector.parqet_selector");
  setTextById("fParqetViewLabel", "layout.inspector.parqet_view");
  setTextById("fParqetIntervalLabel", "layout.inspector.parqet_interval");
  setSelectOptionText(el.fParqetView, "performance", "layout.option.parqet_view.performance");
  setSelectOptionText(el.fParqetView, "holdings", "layout.option.parqet_view.holdings");
  setSelectOptionText(el.fParqetInterval, "", "layout.option.parqet_interval.max");
  setTextById("fParqetHideAmountsLabel", "layout.inspector.parqet_hide_amounts");
  setTextById("alarmActionsLabel", "layout.inspector.alarm_actions");
  setTextById("fAlarmDisarmLabel", "layout.inspector.alarm_action_disarm");
  setTextById("fAlarmArmHomeLabel", "layout.inspector.alarm_action_arm_home");
  setTextById("fAlarmArmAwayLabel", "layout.inspector.alarm_action_arm_away");
  setTextById("fAlarmArmNightLabel", "layout.inspector.alarm_action_arm_night");
  setTextById("fAlarmArmVacationLabel", "layout.inspector.alarm_action_arm_vacation");
  setTextById("fAlarmArmCustomLabel", "layout.inspector.alarm_action_arm_custom_bypass");
  setTextById("fHistoryPopupLabel", "layout.inspector.history_popup");
  setTextById("addScriptTileBtn", "layout.widgets.add_script");
  setTextById("addDoorbellTileBtn", "layout.widgets.add_doorbell");
  setTextById("addBinaryTileBtn", "layout.widgets.add_binary");
  setTextById("fRoomEntitiesLabel", "layout.inspector.room_entities");
  setTextById("fBinaryDisplayModeLabel", "layout.inspector.binary_display");
  setTextById("fVacuumDefaultModeLabel", "layout.inspector.vacuum_default_mode");
  setTextById("fVacuumBatteryLabel", "layout.inspector.vacuum_battery");
  setTextById("fVacuumDefaultModeHint", "layout.inspector.vacuum_default_mode_hint");
  setSelectOptionText(el.fBinaryDisplayMode, "", "layout.option.binary_display.default");
  setSelectOptionText(el.fBinaryDisplayMode, "since", "layout.option.binary_display.since");
  setSelectOptionText(el.fBinaryDisplayMode, "icon", "layout.option.binary_display.icon");
  setSelectOptionText(el.fBinaryDisplayMode, "door", "layout.option.binary_display.door");
  setTextById("fRoomEntitiesHint", "layout.inspector.room_entities_hint");
  setTextById("fRoomExtraModeLabel", "layout.inspector.room_extra");
  setTextById("fRoomControlsLabel", "layout.inspector.room_controls");
  setTextById("fRoomControlsHint", "layout.inspector.room_controls_hint");
  setTextById("fRoomExtraModeDefault", "layout.inspector.room_extra_default");
  setTextById("fRoomExtraModeHumidity", "layout.inspector.room_extra_humidity");
  setTextById("fRoomExtraModeCover", "layout.inspector.room_extra_cover");
  setTextById("fRoomExtraModeMedia", "layout.inspector.room_extra_media");
  setTextById("fRoomExtraEntityLabel", "layout.inspector.room_extra_entity");
  setTextById("addContactTileBtn", "layout.widgets.add_contact");
  setTextById("addCameraTileBtn", "layout.widgets.add_camera");
  setTextById("addFanTileBtn", "layout.widgets.add_fan");
  setTextById("duplicateWidgetBtn", "layout.widgets.duplicate");
  setTextById("deleteWidgetBtn", "layout.widgets.delete");
  setTextById("lightEntityPickerTitle", "entity_picker.title");
  setTextById("lightEntityPickerRefreshBtn", "entity_picker.refresh");
  setTextById("lightEntityPickerCloseBtn", "entity_picker.close");
  setTextById("lightEntityPickerBlankBtn", "entity_picker.blank");
  setPlaceholderById("lightEntityPickerSearch", "entity_picker.search_placeholder");

  setTextById("inspectorHeading", "layout.inspector.heading");
  setTextById("fTitleLabel", "layout.inspector.title");
  setTextById("fEntityLabel", "layout.inspector.entity");
  setTextById("fSecondaryEntityLabel", "layout.inspector.secondary_entity");
  setTextById("sonosFavoritesHint", "layout.inspector.sonos_favorites_hint");
  setTextById("fTertiaryEntityLabel", "layout.inspector.tertiary_entity_car");
  setTextById("fSolarForecastTodayLabel", "layout.inspector.solar_today");
  setTextById("fSolarForecastTomorrowLabel", "layout.inspector.solar_tomorrow");
  setTextById("fSolarForecastDay3Label", "layout.inspector.solar_day_3");
  setTextById("fSolarForecastDay4Label", "layout.inspector.solar_day_4");
  setTextById("fSolarForecastDay5Label", "layout.inspector.solar_day_5");
  setTextById("fEtlStyleLabel", "layout.inspector.etl_style");
  setTextById("fEtlStyleRows", "layout.inspector.etl_style_rows");
  setTextById("fEtlStyleNested", "layout.inspector.etl_style_nested");
  setTextById("fEtlChargeLabel", "layout.inspector.etl_charge");
  setTextById("fEtlDischargeLabel", "layout.inspector.etl_discharge");
  setTextById("fEtlSocLabel", "layout.inspector.etl_soc");
  setTextById("fEtlForecastLabel", "layout.inspector.etl_forecast");
  setTextById("fBambuNozzleLabel", "layout.inspector.bambu_nozzle");
  setTextById("fBambuBedLabel", "layout.inspector.bambu_bed");
  setTextById("fBambuProgressLabel", "layout.inspector.bambu_progress");
  setTextById("fBambuRemainingLabel", "layout.inspector.bambu_remaining");
  setTextById("fBambuStageLabel", "layout.inspector.bambu_stage");
  setTextById("fBambuJobLabel", "layout.inspector.bambu_job");
  setTextById("fBambuPauseLabel", "layout.inspector.bambu_pause");
  setTextById("fBambuResumeLabel", "layout.inspector.bambu_resume");
  setTextById("fBambuStopLabel", "layout.inspector.bambu_stop");
  setTextById("bambuHint", "layout.inspector.bambu_hint");
  setTextById("fBambuPrinterLabel", "layout.inspector.bambu_printer");
  setTextById("bambuDetailsSummary", "layout.inspector.bambu_entities");
  setTextById("fBambuDetectBtn", "layout.inspector.bambu_detect");
  setTextById("fCarPrecondLabel", "layout.inspector.car_precond");
  setTextById("fCarLockLabel", "layout.inspector.car_lock");
  setTextById("fCarTempLabel", "layout.inspector.car_temp");
  setTextById("carExtraHint", "layout.inspector.car_extra_hint");
  setTextById("fSolarForecastBarMaxLabel", "layout.inspector.solar_bar_max");
  setTextById("fForecastDaysLabel", "layout.inspector.forecast_days");
  setTextById("fAlignForecastLabel", "layout.inspector.align_forecast");
  setSelectOptionText(document.getElementById("fForecastDays"), "", "layout.inspector.forecast_keep");
  setSelectOptionText(document.getElementById("fForecastDays"), "0", "layout.inspector.forecast_auto");
  setTextById("fWeatherShowPrecipLabel", "layout.inspector.weather_show_precip");
  setTextById("fShowNameLabel", "layout.inspector.show_name");
  setTextById("fSensorShowAgeLabel", "layout.inspector.sensor_show_age");
  updateSensorEntityInfo(); // the format and unit choices follow the device class and carry their own texts
  setTextById("fSolarForecastBarOrientationLabel", "layout.inspector.solar_bar_orientation");
  setTextById("fGaugeStyleLabel", "layout.inspector.gauge_style");
  for (const opt of el.fGaugeStyle?.options || []) opt.textContent = t(`layout.inspector.gauge_style_${opt.value}`);
  for (const opt of el.fGaugeBarStyle?.options || []) opt.textContent = t(`layout.inspector.gauge_bar_style_${opt.value}`);
  setTextById("fGaugeBarStyleLabel", "layout.inspector.gauge_bar_style");
  setTextById("fGaugeSegmentCountLabel", "layout.inspector.gauge_segment_count");
  setTextById("fGaugeMinLabel", "layout.inspector.gauge_min");
  setTextById("fGaugeMaxLabel", "layout.inspector.gauge_max");
  setTextById("fGaugeHint", "layout.inspector.gauge_hint");
  setTextById("fGaugeColorModeLabel", "layout.gauge.color_mode");
  for (const opt of el.fGaugeColorMode?.options || []) opt.textContent = t(`layout.gauge.color_mode_${opt.value || "fill"}`);
  setTextById("fGaugeTargetLowLabel", "layout.gauge.target_low");
  setTextById("fGaugeTargetHighLabel", "layout.gauge.target_high");
  setTextById("fGaugeTargetColorLabel", "layout.gauge.target_color");
  updateGaugeColorModeVisibility();
  document.querySelectorAll("[data-inspector-entity-target]").forEach((button) => {
    button.textContent = t("entity_picker.search");
    button.setAttribute("aria-label", t("entity_picker.search"));
  });
  setTextById("fButtonModeLabel", "layout.inspector.button_mode");
  setTextById("fButtonAccentColorLabel", "layout.inspector.button_accent_color");
  setTextById("fSliderEntityDomainLabel", "layout.inspector.slider_entity_domain");
  setTextById("fSliderDirectionLabel", "layout.inspector.slider_direction");
  setTextById("fSliderAccentColorLabel", "layout.inspector.slider_accent_color");
  setTextById("fGraphLineColorLabel", "layout.inspector.graph_line_color");
  setTextById("fGraphTimeWindowMinLabel", "layout.inspector.graph_time_window_min");
  setTextById("fGraphHaHistoryLabel", "layout.inspector.graph_ha_history");
  setTextById("fGraphHaHistoryHint", "layout.inspector.graph_ha_history_hint");
  setTextById("fGraphPointCountLabel", "layout.inspector.graph_point_count");
  setTextById("fGraphDisplayModeLabel", "layout.inspector.graph_display_mode");
  setTextById("fGraphBarBucketMinLabel", "layout.inspector.graph_bar_bucket_min");
  setSelectOptionText(el.fGraphDisplayMode, "line", "layout.option.graph_display_mode.line");
  setSelectOptionText(el.fGraphDisplayMode, "line_smooth_points", "layout.option.graph_display_mode.line_smooth_points");
  setSelectOptionText(el.fGraphDisplayMode, "line_smooth", "layout.option.graph_display_mode.line_smooth");
  setSelectOptionText(el.fGraphDisplayMode, "bars", "layout.option.graph_display_mode.bars");
  setSelectOptionText(el.fSolarForecastBarOrientation, "horizontal", "layout.option.solar_bar_orientation.horizontal");
  setSelectOptionText(el.fSolarForecastBarOrientation, "vertical", "layout.option.solar_bar_orientation.vertical");
  setTextById("applyInspectorBtn", "layout.inspector.apply");
  setSelectOptionText(el.fButtonMode, "auto", "layout.option.button_mode.auto");
  setSelectOptionText(el.fButtonMode, "play_pause", "layout.option.button_mode.play_pause");
  setSelectOptionText(el.fButtonMode, "stop", "layout.option.button_mode.stop");
  setSelectOptionText(el.fButtonMode, "next", "layout.option.button_mode.next");
  setSelectOptionText(el.fButtonMode, "previous", "layout.option.button_mode.previous");
  setSelectOptionText(el.fSliderEntityDomain, "auto", "layout.option.slider_entity_domain.auto");
  setSelectOptionText(el.fSliderEntityDomain, "light", "layout.option.slider_entity_domain.light");
  setSelectOptionText(el.fSliderEntityDomain, "media_player", "layout.option.slider_entity_domain.media_player");
  setSelectOptionText(el.fSliderEntityDomain, "cover", "layout.option.slider_entity_domain.cover");
  setSelectOptionText(el.fSliderDirection, "auto", "layout.option.slider_direction.auto");
  setSelectOptionText(el.fSliderDirection, "left_to_right", "layout.option.slider_direction.left_to_right");
  setSelectOptionText(el.fSliderDirection, "right_to_left", "layout.option.slider_direction.right_to_left");
  setSelectOptionText(el.fSliderDirection, "bottom_to_top", "layout.option.slider_direction.bottom_to_top");
  setSelectOptionText(el.fSliderDirection, "top_to_bottom", "layout.option.slider_direction.top_to_bottom");

  setTextById("layoutActionsHeading", "layout.actions.heading");
  setTextById("layoutImportSummary", "layout.actions.import");
  setTextById("addPageMenuBtn", "layout.pages.menu_btn");
  setTextById("addTileBtn", "layout.widgets.add_tile");
  for (const key of ["Normal", "Room", "House", "Energy"]) {
    setTextById(`addPageMenu${key}Label`, `layout.pages.menu_${key.toLowerCase()}`);
    setTextById(`addPageMenu${key}Desc`, `layout.pages.menu_${key.toLowerCase()}_desc`);
  }
  setTextById("addPageMenuGroupHa", "layout.pages.menu_group_ha");
  setTextById("addPageMenuGroupSpecial", "layout.pages.menu_group_special");
  setTextById("addTileTitle", "add_tile.title");
  setPlaceholderById("addTileSearch", "add_tile.search_placeholder");
  setTextById("roomImportTitle", "room_import.title");
  setTextById("roomImportHint", "room_import.hint");
  setTextById("roomImportConfirmBtn", "room_import.confirm");
  setTextById("roomImportBackBtn", "room_import.back");
  setTextById("houseHeading", "house.heading");
  setTextById("houseInfoTitle", "house.info_title");
  const infoList = document.getElementById("houseInfoList");
  if (infoList) {
    infoList.replaceChildren();
    for (let i = 1; i <= 5; i++) {
      const li = document.createElement("li");
      li.textContent = t(`house.info${i}`);
      infoList.appendChild(li);
    }
  }
  setTextById("houseImportBtn", "house.import");
  document.querySelectorAll("#addTileTypes .add-tile-group-label").forEach((node, index) => {
    const key = ["layout.menu.group_control", "layout.menu.group_data", "layout.menu.group_info"][index];
    if (key) node.textContent = t(key);
  });
  setActionTooltipById("reloadBtn", "layout.actions.reload");
  setActionTooltipById("saveBtn", "layout.actions.save");
  setActionTooltipById("layoutJsonMenuBtn", "layout.actions.json");
  setActionTooltipById("layoutTextSizeInfoBtn", "settings.ui.text_step_details");
  setActionTooltipById("importFile", "layout.actions.import");
  setActionTooltipById("jsonPaste", "layout.actions.paste_placeholder");
  setTextById("stackedSaveBtn", "layout.actions.save");
  setTextById("unsavedBadge", "layout.status.unsaved_badge");
  setTextById("undoToastBtn", "undo.button");
  setTextById("exportBtn", "layout.actions.export");
  setTextById("importBtn", "layout.actions.import");
  setPlaceholderById("jsonPaste", "layout.actions.paste_placeholder");

  setTextById("provWifiTitle", "provision.wifi.title");
  setTextById("provWifiSubtitle", "provision.wifi.subtitle");
  setTextById("provWifiSsidLabel", "provision.wifi.ssid");
  setTextById("provWifiCountryCodeLabel", "provision.wifi.country_code");
  setTextById("provWifiPasswordLabel", "provision.wifi.password");
  setPlaceholderById("provWifiPassword", "provision.wifi.password_placeholder");
  setTextById("provWifiShowPasswordLabel", "provision.wifi.show_password");
  setTextById("provScanWifiBtn", "common.scan");
  setTextById("provHaTitle", "provision.ha.title");
  setTextById("provHaSubtitle", "provision.ha.subtitle");
  setTextById("provHaUrlLabel", "provision.ha.ws_url");
  setTextById("provHaUseUrlBtn", "provision.ha.use_url");
  setTextById("provHaTokenLabel", "provision.ha.token");
  setTextById("provHaShowTokenLabel", "provision.ha.show_token");
  setTextById("provWifiSaveBtn", "common.save_reboot");
  setTextById("provHaSaveBtn", "common.save_reboot");

  setTextById("settingsWifiHeading", "settings.wifi.heading");
  setTextById("settingsWifiSsidLabel", "settings.wifi.ssid");
  setTextById("settingsWifiCountryCodeLabel", "settings.wifi.country_code");
  setTextById("settingsWifiBssidLabel", "settings.wifi.bssid");
  setTextById("settingsWifiPasswordLabel", "settings.wifi.password");
  setPlaceholderById("settingsWifiPassword", "settings.wifi.password_placeholder");
  setTextById("scanWifiBtn", "common.scan_wifi");

  setTextById("settingsHaHeading", "settings.ha.heading");
  setTextById("settingsHaUrlLabel", "settings.ha.ws_url");
  setTextById("settingsHaTokenLabel", "settings.ha.token");
  setPlaceholderById("settingsHaToken", "settings.ha.token_placeholder");
  setTextById("settingsHaRestEnabledLabel", "settings.ha.rest_fallback");

  setTextById("settingsMqttHeading", "settings.mqtt.heading");
  const mqttAbout = document.getElementById("settingsMqttAboutText");
  if (mqttAbout) mqttAbout.innerHTML = t("settings.mqtt.about");
  setTextById("settingsMqttEnabledLabel", "settings.mqtt.enabled");
  setTextById("settingsMqttHostLabel", "settings.mqtt.host");
  setTextById("settingsMqttPortLabel", "settings.mqtt.port");
  setTextById("settingsMqttTlsLabel", "settings.mqtt.tls");
  setTextById("settingsMqttUsernameLabel", "settings.mqtt.username");
  setTextById("settingsMqttPasswordLabel", "settings.mqtt.password");
  setPlaceholderById("settingsMqttPassword", "settings.mqtt.password_placeholder");
  setTextById("settingsMqttTopicPrefixLabel", "settings.mqtt.topic_prefix");
  setTextById("settingsMqttDeviceNameLabel", "settings.mqtt.device_name");
  setTextById("settingsMqttDeviceHint", "settings.mqtt.device_hint");
  setTextById("settingsMqttExamplesSummary", "settings.mqtt.examples");
  setTextById("settingsDeviceIdLabel", "settings.device.id");
  setTextById("settingsDeviceNameLabel", "settings.device.name");
  setTextById("settingsBrightnessHeading", "settings.display.in_use");
  setTextById("settingsSoundHeading", "settings.sound.heading");
  setTextById("settingsSoundEnabledLabel", "settings.sound.enabled");
  setTextById("settingsSoundVolumeLabel", "settings.sound.volume");
  setTextById("settingsSoundNightMuteLabel", "settings.sound.night_mute");
  setTextById("settingsSoundTestBtn", "settings.sound.test");
  setTextById("settingsSoundHint", "settings.sound.hint");
  setTextById("settingsNightHeading", "settings.night.heading");
  if (typeof renderBrightnessTest === "function") renderBrightnessTest();
  setTextById("settingsCameraHeading", "settings.camera.heading");
  setTextById("settingsDisplayLiveHint", "settings.display.live");
  setTextById("settingsNightStartLabel", "settings.display.from");
  setTextById("settingsNightEndLabel", "settings.display.until");
  setTextById("settingsCameraAdvancedSummary", "settings.camera.refine");
  setTextById("settingsSaverAdvancedSummary", "settings.saver.refine");
  setTextById("settingsRotationHeading", "settings.rotation.heading");
  setTextById("settingsTranslationManageSummary", "settings.translation.manage");
  setTextById("settingsDeviceHeading", "settings.device.heading");
  setTextById("settingsDeviceRoomLabel", "settings.device.room");
  setTextById("settingsDeviceRoomHint", "settings.device.room_hint");
  setPlaceholderById("settingsDeviceRoom", "settings.device.room_placeholder");
  setPlaceholderById("settingsMqttDeviceName", "settings.mqtt.device_name_placeholder");
  setTextById("settingsMqttExampleHeading", "settings.mqtt.example_heading");
  const mqttExampleHint = document.getElementById("settingsMqttExampleHint");
  if (mqttExampleHint) mqttExampleHint.innerHTML = t("settings.mqtt.example_hint");
  const mqttExampleCode = document.getElementById("settingsMqttExampleCode");
  if (mqttExampleCode) mqttExampleCode.textContent = t("settings.mqtt.example_yaml");
  const mqttExampleNote = document.getElementById("settingsMqttExampleNote");
  if (mqttExampleNote) mqttExampleNote.innerHTML = t("settings.mqtt.example_note");
  setTextById("settingsMqttAdvancedHeading", "settings.mqtt.advanced_heading");
  const mqttAdvancedHint = document.getElementById("settingsMqttAdvancedHint");
  if (mqttAdvancedHint) mqttAdvancedHint.innerHTML = t("settings.mqtt.advanced_hint");
  renderMqttAdvancedExample();
  const mqttEmojiHint = document.getElementById("settingsMqttEmojiHint");
  if (mqttEmojiHint) mqttEmojiHint.textContent = t("settings.mqtt.emoji_note");

  setTextById("settingsTopbarHeading", "settings.topbar.heading");
  setTextById("haDiagnosticsHint", "ha_diagnostics.missing_hint");
  for (const scanId of ["provWifiScanResults", "settingsWifiScanResults"]) {
    const scanSelect = document.getElementById(scanId);
    if (scanSelect && scanSelect.options.length === 1 && scanSelect.options[0].value === "") {
      scanSelect.options[0].textContent = t("wifi.scan.option_not_run");
    }
  }
  setTextById("settingsTopbarSlot1Label", "settings.topbar.slot_1");
  setTextById("settingsTopbarSlot2Label", "settings.topbar.slot_2");
  setTextById("settingsTopbarSlot3Label", "settings.topbar.slot_3");
  setTextById("settingsTopbarWeatherEntityLabel", "settings.topbar.weather_entity");
  setTextById("settingsTopbarStockEntityLabel", "settings.topbar.stock_entity");
  setSelectOptionText(el.settingsTopbarSlot1, "", "settings.topbar.option_none");
  setSelectOptionText(el.settingsTopbarSlot1, "date", "settings.topbar.option_date");
  setSelectOptionText(el.settingsTopbarSlot1, "weather", "settings.topbar.option_weather");
  setSelectOptionText(el.settingsTopbarSlot1, "stock", "settings.topbar.option_stock");
  setSelectOptionText(el.settingsTopbarSlot2, "", "settings.topbar.option_none");
  setSelectOptionText(el.settingsTopbarSlot2, "date", "settings.topbar.option_date");
  setSelectOptionText(el.settingsTopbarSlot2, "weather", "settings.topbar.option_weather");
  setSelectOptionText(el.settingsTopbarSlot2, "stock", "settings.topbar.option_stock");
  setSelectOptionText(el.settingsTopbarSlot3, "", "settings.topbar.option_none");
  setSelectOptionText(el.settingsTopbarSlot3, "date", "settings.topbar.option_date");
  setSelectOptionText(el.settingsTopbarSlot3, "weather", "settings.topbar.option_weather");
  setSelectOptionText(el.settingsTopbarSlot3, "stock", "settings.topbar.option_stock");

  setTextById("settingsHardwareHeading", "settings.hardware.heading");
  setTextById("settingsHardwareActiveBrightnessLabel", "settings.hardware.active_brightness");
  setTextById("settingsHardwareDimBrightnessLabel", "settings.hardware.dim_brightness");
  setTextById("settingsHardwareDimTimeoutLabel", "settings.hardware.dim_timeout");
  setTextById("settingsSaverHeading", "settings.saver.heading");
  setTextById("settingsSaverModeLabel", "settings.saver.mode");
  setSelectOptionText(el.settingsSaverMode, "disabled", "settings.saver.disabled");
  setSelectOptionText(el.settingsSaverMode, "off", "settings.saver.off");
  setSelectOptionText(el.settingsSaverMode, "weather", "settings.saver.weather");
  setSelectOptionText(el.settingsSaverMode, "pages", "settings.saver.pages");
  setTextById("settingsSaverTimeoutLabel", "settings.saver.timeout");
  setTextById("settingsSaverWeatherEntityLabel", "settings.saver.entity");
  setTextById("settingsSaverWeatherSearchBtn", "entity_picker.search");
  setTextById("settingsSaverWeatherMinutesLabel", "settings.saver.weather_minutes");
  setTextById("settingsSaverWeatherBrightnessLabel", "settings.saver.weather_brightness");
  setTextById("settingsSaverPageIntervalLabel", "settings.saver.page_interval");
  setTextById("settingsSaverPagesMinutesLabel", "settings.saver.pages_minutes");
  setTextById("settingsSaverPageWakeLabel", "settings.saver.page_wake");
  setSelectOptionText(el.settingsSaverPageWake, "home", "settings.saver.page_wake_home");
  setSelectOptionText(el.settingsSaverPageWake, "stay", "settings.saver.page_wake_stay");
  setTextById("settingsSaverPixelRefreshLabel", "settings.saver.pixel_refresh");
  syncSaverFields();
  setTextById("settingsHardwareAutoRotationLabel", "settings.hardware.auto_rotation");
  setTextById("settingsHardwareAutoBrightnessLabel", "settings.hardware.auto_brightness");
  setTextById("settingsBrightnessRuleHint", "settings.hardware.brightness_rule");
  setTextById("settingsHardwarePresenceWakeLabel", "settings.hardware.presence_wake");
  setTextById("settingsHardwarePresenceSensitivityLabel", "settings.hardware.presence_sensitivity");
  setTextById("settingsHardwareNightPresenceWakeLabel", "settings.hardware.night_presence_wake");
  setTextById("settingsHardwareCameraHint", "settings.hardware.camera_hint");
  setTextById("settingsHardwareNightBrightnessLabel", "settings.hardware.night_brightness");
  setTextById("settingsHardwareNightEnabledLabel", "settings.hardware.night_enabled");

  setTextById("settingsNtpServerLabel", "settings.time.ntp_server");
  setTextById("settingsTimezoneLabel", "settings.time.timezone");
  setTextById("settingsTimezonePresetLabel", "settings.time.zone");
  if (typeof renderTimezonePresets === "function") renderTimezonePresets(); // the "custom" entry follows the language

  setTextById("settingsLanguageLabel", "settings.ui.language");
  setTextById("settingsUiStyleLabel", "settings.ui.style");
  setTextById("settingsUiTextStepLabel", "settings.ui.text_step");
  if (el.settingsUiTextStep) {
    for (const opt of el.settingsUiTextStep.options) opt.textContent = t(`settings.ui.text_step.${opt.value}`);
    updateTextStepInfo();
  }
  setTextById("settingsAppearanceHeading", "settings.appearance.heading");
  setTextById("settingsThemeSelectLabel", "settings.theme.select");
  setTextById("settingsThemeImportLabel", "settings.theme.import");
  setTextById("settingsThemeNewIdLabel", "settings.theme.new_id");
  setTextById("settingsThemeNewNameLabel", "settings.theme.new_name");
  setTextById("settingsThemePreviewLabel", "settings.theme.preview");
  setTextById("inspectorGroupDataLabel", "layout.inspector.group_data");
  setTextById("inspectorGroupStyleLabel", "layout.inspector.group_style");
  setTextById("inspectorGroupPositionLabel", "layout.inspector.group_position");
  setTextById("fHeatingStyleVariantLabel", "layout.inspector.heating_style");
  setTextById("fHeatingArcOpeningLabel", "layout.inspector.arc_opening");
  setSelectOptionText(el.fHeatingStyleVariant, "default", "layout.option.heating_style.default");
  setSelectOptionText(el.fHeatingStyleVariant, "arc_semi", "layout.option.heating_style.arc_semi");
  for (const pos of ["left", "right", "top", "bottom"]) {
    setSelectOptionText(el.fHeatingArcOpening, pos, `layout.option.arc_opening.${pos}`);
  }
  // The tile dialog lists the same widgets as the buttons; reuse their labels without the "+ ".
  for (const item of document.querySelectorAll(".add-tile-type[data-add-target]")) {
    const source = document.getElementById(item.dataset.addTarget);
    if (source) {
      item.textContent = source.textContent.replace(/^\+\s*/, "");
    }
  }
  setTextById("settingsAppearanceHint", "settings.appearance.hint");
  setTextById("settingsThemeSubheading", "settings.theme.subheading");
  setTextById("settingsThemeEditorSummary", "settings.theme.editor_summary");
  setTextById("settingsThemeApplyBtn", "settings.theme.apply");
  setTextById("settingsThemeExportBtn", "settings.theme.export");
  setTextById("settingsThemeDeleteBtn", "settings.theme.delete");
  setTextById("settingsThemeImportBtn", "settings.theme.import_btn");
  setTextById("settingsThemeSaveCustomBtn", "settings.theme.save_custom");
  setTextById("settingsThemeResetBtn", "settings.theme.reset");
  document.querySelectorAll(".entity-picker-btn, #settingsTopbarWeatherSearchBtn, #settingsTopbarStockSearchBtn").forEach((btn) => { btn.textContent = t("entity_picker.search"); });
  setTextById("settingsTimeHeading", "settings.time.subheading");
  setTextById("settingsRotationHeading", "settings.rotation.heading");
  setTextById("settingsTranslationManageSummary", "settings.translation.manage");
  setTextById("settingsLanguageHeading", "settings.language.subheading");
  if (el.settingsUiStyle) {
    for (const opt of el.settingsUiStyle.options) {
      opt.textContent = t(`settings.appearance.${opt.value}`);
    }
    updateStyleInfo();
  }
  setTextById("reloadLanguagesBtn", "settings.ui.reload_languages");
  setTextById("downloadLanguageBtn", "settings.ui.download_json");
  setTextById("uploadLanguageCodeLabel", "settings.ui.upload_code");
  setTextById("uploadLanguageFileLabel", "settings.ui.upload_file");
  setTextById("uploadLanguageBtn", "settings.ui.upload_button");


  setTextById("settingsStatusHeading", "settings.status.heading");
  setTextById("settingsStatusCpuLabel", "settings.status.cpu");
  setTextById("settingsStatusRamLabel", "settings.status.ram");
  setTextById("settingsStatusPsramLabel", "settings.status.psram");
  setTextById("settingsStatusC6Label", "settings.status.c6_label");
  setTextById("settingsStatusC6DetailsLabel", "settings.status.c6_details");
  setTextById("settingsStatusC6Hint", "settings.status.c6_hint");
  setTextById("settingsStatusUptimeLabel", "settings.status.uptime");
  setTextById("settingsBackupHeading", "settings.backup.heading");
  setTextById("settingsAlertsHeading", "settings.alerts.heading");
  setTextById("settingsAlertsInfo", "settings.alerts.info");
  setTextById("settingsAlertsExample", "settings.alerts.example");
  setTextById("settingsAlertsAdd", "settings.alerts.add");
  setTextById("settingsAlertsSave", "settings.alerts.save");
  setTextById("settingsAlertsSaveNote", "settings.alerts.save_note");
  renderAlertList();
  setTextById("settingsBackupInfo", "settings.backup.info");
  setTextById("settingsBackupDownload", "settings.backup.download");
  setTextById("settingsBackupFileLabel", "settings.backup.file");
  setTextById("settingsBackupRestore", "settings.backup.restore");
  setTextById("settingsFactoryHeading", "settings.factory.heading");
  setTextById("settingsFactoryWarning", "settings.factory.warning");
  setTextById("settingsFactoryBtn", "settings.factory.button");
  setTextById("settingsRestartBtn", "settings.restart.button");
  setTextById("settingsStatusResetLabel", "settings.status.reset");
  setTextById("settingsStatusChipLabel", "settings.status.chip");
  setTextById("settingsStatusBootloaderLabel", "settings.status.bootloader");
  setTextById("settingsLogSummary", "settings.log.summary");
  setTextById("settingsRebootSummary", "settings.reboot.summary");
  setTextById("settingsLogRefresh", "settings.log.refresh");
  setTextById("settingsLogDownload", "settings.log.download");
  setTextById("coredumpHeading", "settings.status.coredump_heading");
  if (editor.crashReports) renderCrashReports({ items: editor.crashReports });
  setTextById("settingsSecurityHeading", "settings.security.heading");
  setTextById("settingsSecurityHint", "settings.security.hint");
  setTextById("settingsSecurityNewLabel", "settings.security.new");
  setTextById("settingsSecurityRepeatLabel", "settings.security.repeat");
  setTextById("settingsSecuritySet", "settings.security.set");
  setTextById("settingsSecurityClear", "settings.security.clear");
  renderSecurityStatus();
  setTextById("settingsOtaHeading", "settings.ota.heading");
  setTextById("settingsFirmwareInfoHeading", "settings.firmware_info.heading");
  setTextById("settingsFirmwareInfoColComponent", "settings.firmware_info.col_component");
  setTextById("settingsFirmwareInfoColVersion", "settings.firmware_info.col_version");
  setTextById("settingsFirmwareInfoColDescription", "settings.firmware_info.col_description");
  setTextById("settingsOtaUrlLabel", "settings.ota.url");
  setTextById("settingsOtaManualSummary", "settings.ota.manual");
  setPlaceholderById("settingsOtaUrl", "settings.ota.url_placeholder");
  setTextById("startOtaUrlBtn", "settings.ota.flash_url");
  setTextById("checkUpdateNowBtn", "update.check_now");
  setTextById("settingsOtaUpdateCheckEnabledLabel", "update.setting_enabled");
  setTextById("settingsOtaIncludePrereleasesLabel", "update.setting_prereleases");
  setTextById("settingsOtaAutoInstallLabel", "update.auto.setting");
  setTextById("settingsOtaAutoInstallHint", "update.auto.warning");
  setTextById("updateBannerOpen", "update.banner_open");
  setTextById("crashBannerOpen", "crash.banner_open");
  const crashDismiss = document.getElementById("crashBannerDismiss");
  if (crashDismiss) {
    crashDismiss.setAttribute("aria-label", t("ha_diagnostics.dismiss"));
    crashDismiss.title = t("ha_diagnostics.dismiss");
  }
  if (el.updateBannerDismiss) {
    el.updateBannerDismiss.setAttribute("aria-label", t("ha_diagnostics.dismiss"));
    el.updateBannerDismiss.title = t("ha_diagnostics.dismiss");
  }
  renderUpdateBanner();
  renderOtaUpdateCheck();
  setTextById("settingsOtaFileLabel", "settings.ota.file");
  setTextById("uploadOtaBtn", "settings.ota.upload");

  setTextById("settingsActionsHeading", "settings.actions.heading");
  setTextById("reloadSettingsBtn", "settings.actions.reload");
  setTextById("saveSettingsBtn", "settings.actions.save");
  setTextById("settingsActionsHint", "settings.actions.hint");
  applySettingsNavTranslations();

  if (el.settingsTranslationInfo && !el.settingsTranslationInfo.classList.contains("error")) {
    el.settingsTranslationInfo.textContent = t("settings.translation.info");
  }

  if (el.uploadLanguageCode) {
    el.uploadLanguageCode.placeholder = "nl";
  }
  /* The theme toggle's title/aria-label are set once at page load (initEditorTheme(), before the
   * real device language has loaded, so always in English) and never refreshed after -- found by
   * actually looking at a German session's DOM: the tooltip stayed "Switch to light mode" next to
   * otherwise-German text. Re-applying it here, on every translation refresh, keeps it in sync. */
  const themeToggle = document.getElementById("themeToggleBtn");
  if (themeToggle) {
    const label = t(document.documentElement.dataset.theme === "light" ? "theme.to_dark" : "theme.to_light");
    themeToggle.title = label;
    themeToggle.setAttribute("aria-label", label);
  }
  // Same reasoning as the theme toggle above: the Pages/Widgets/Inspector collapse buttons' visible
  // label is just "+"/"-", and their aria-label/title is otherwise only (re-)computed when actually
  // clicked, so it would sit in the bootstrap-time fallback language until first use otherwise.
  applySectionCollapseState();
  refreshInspectorLayout();
}

function downloadLanguageJson() {
  const lang = normalizeUiLanguage(el.settingsLanguage?.value || editor.i18nLanguage);
  const web = {};
  for (const [key, value] of Object.entries(editor.i18nMap || {})) {
    web[key] = value;
  }

  const payload = {
    meta: {
      code: lang,
      exported_at: new Date().toISOString(),
    },
    web,
    lvgl: editor.i18nEffective?.lvgl || {},
  };

  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `betta-i18n-${lang}.json`;
  a.click();
  URL.revokeObjectURL(url);
}

async function uploadLanguageJson() {
  const lang = normalizeLanguageCode(el.uploadLanguageCode?.value, "");
  if (!lang) {
    throw new Error(t("settings.translation.invalid_code"));
  }
  const file = el.uploadLanguageFile?.files?.[0];
  if (!file) {
    throw new Error(t("settings.translation.no_file"));
  }

  const text = await file.text();
  let parsed = null;
  try {
    parsed = JSON.parse(text);
  } catch (_) {
    throw new Error(t("settings.translation.invalid_json"));
  }
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new Error(t("settings.translation.object_required"));
  }

  const response = await fetch(`/api/i18n/custom?lang=${encodeURIComponent(lang)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(parsed),
  });
  if (!response.ok) {
    let detail = await response.text();
    try {
      const json = JSON.parse(detail);
      detail = json.error || detail;
    } catch (_) {}
    throw new Error(detail);
  }
}
