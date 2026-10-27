/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Entity pickers, entity search (add from an entity), Quick Setup
 *
 * Part of the web editor: classic scripts loaded in the order of index.html, sharing one global scope
 * (see docs/editor-source.md).
 */
function clearLightEntityPickerPoll() {
  if (editor.lightPicker.pollTimerId !== null) {
    window.clearTimeout(editor.lightPicker.pollTimerId);
    editor.lightPicker.pollTimerId = null;
  }
}

function clearLightEntityPickerSearchDebounce() {
  if (editor.lightPicker.searchDebounceId !== null) {
    window.clearTimeout(editor.lightPicker.searchDebounceId);
    editor.lightPicker.searchDebounceId = null;
  }
}

function cancelLightEntityPickerRequest() {
  const config = entityPickerConfig();
  const params = new URLSearchParams();
  params.set("domain", config.domain);
  const search = entityPickerSearchValue();
  if (search) params.set("search", search);
  fetch(`/api/ha/light_entities?${params.toString()}`, { method: "DELETE", cache: "no-store" }).catch(() => {});
}

function entityPickerConfig(widgetType = editor.lightPicker.widgetType) {
  if (editor.lightPicker.configOverride) {
    return {
      widgetType,
      ...editor.lightPicker.configOverride,
    };
  }
  const normalizedWidgetType = ENTITY_PICKER_CONFIGS[widgetType] ? widgetType : "light_tile";
  return {
    widgetType: normalizedWidgetType,
    ...ENTITY_PICKER_CONFIGS[normalizedWidgetType],
    ...entityPickerCreationDomains(normalizedWidgetType),
  };
}

function entityPickerConfigForDomain(domain, titleKey = "entity_picker.title_entity", titleFallback = "Choose Entity") {
  const keyByDomain = {
    climate: "heating_tile",
    cover: "slider",
    image: "image",
    light: "light_tile",
    media_player: "media_player",
    sensor: "sensor",
    switch: "button",
    todo: "todo_list",
    vacuum: "vacuum_tile",
    weather: "weather_tile",
  };
  const key = keyByDomain[domain] || "sensor";
  const base = ENTITY_PICKER_CONFIGS[key];
  if (!base) {
    return {
      domain,
      titleKey,
      blankKey: "entity_picker.blank",
      widgetKey: "entity_picker.title_entity",
      itemsKey: "entity_picker.items_entity",
      titleFallback,
      blankFallback: "Blank Entity",
      widgetFallback: "Entity",
      itemsFallback: `${domain} entities`,
      minSearch: 2,
      liveSearch: false,
    };
  }
  return {
    ...base,
    allowedDomains: undefined,
    domain,
    titleKey,
    titleFallback,
    ...(domain === "any" ? { itemsKey: "entity_picker.items_entity", itemsFallback: "entities" } : {}),
  };
}

function entityPickerAllowedItems(items, config) {
  return items.filter(item =>
    (!config.allowedDomains?.length || config.allowedDomains.includes(item.id.split(".")[0])) &&
    (!config.allowedEntityIds || config.allowedEntityIds.includes(item.id)) &&
    !config.excludedEntityIds?.includes(item.id));
}

function entityPickerFilterHint(config, type, slot = "primary") {
  const domains = config.allowedDomains?.length ? config.allowedDomains : [config.domain || "any"];
  const parts = [domains.includes("any") ? t("entity_picker.filter_any") :
    domains.includes("room") ? t("entity_picker.filter_room") :
      t("entity_picker.filter_domains", { domains: domains.map(domain => domain + ".*").join(", ") })];
  const role = widgetEntitySearchRole(type, slot);
  if (config.restrictionHint === "entity_picker.filter_printer_role") parts.push(t(config.restrictionHint));
  else {
    parts.push(t(`entity_picker.rule_${role}`));
    if (type === "bambu_printer" && slot !== "primary") parts.push(t("entity_picker.filter_bambu"));
    if (config.allowedEntityIds) parts.push(t(config.restrictionHint || "entity_picker.filter_assigned"));
  }
  if (config.excludedEntityIds?.length) parts.push(t("entity_picker.filter_excluded"));
  return parts.join(" ");
}

function entityPickerItemsLabel(config = entityPickerConfig()) {
  return t(config.itemsKey, {}, config.itemsFallback);
}

function entityPickerSearchValue() {
  return String(editor.lightPicker.search || "").trim();
}

function entityPickerSearchReady(config = entityPickerConfig()) {
  const minSearch = Number(config.minSearch || 0);
  return minSearch <= 0 || entityPickerSearchValue().length >= minSearch;
}

function entityPickerLiveSearchEnabled(config = entityPickerConfig()) {
  return config.liveSearch !== false;
}

function entityPickerCacheKey(domain, search = entityPickerSearchValue()) {
  return `${domain}|${String(search || "").trim().toLowerCase()}`;
}

function normalizeEntityPickerItems(items, domain) {
  if (!Array.isArray(items)) return [];
  const prefix = `${domain}.`;
  const roomDomains = ["sensor.", "binary_sensor.", "light.", "cover.", "fan.", "climate.", "media_player.", "lock.", "switch.", "vacuum."];
  return items
    .filter((item) => item && typeof item.id === "string" &&
      (domain === "any" ? item.id.includes(".") : domain === "room" ? roomDomains.some((p) => item.id.startsWith(p)) : item.id.startsWith(prefix)))
    .map((item) => ({
      id: item.id,
      name: String(item.name || item.id),
      room: String(item.room || ""),
      area_id: String(item.area_id || ""),
      icon: String(item.icon || ""),
      device_class: String(item.device_class || ""),
      unit: String(item.unit || ""),
      state_class: String(item.state_class || ""),
      has_price_series: item.has_price_series === true,
      has_solar_forecast: item.has_solar_forecast === true,
      search_flags: Number(item.search_flags) || 0,
      supported_features: typeof item.supported_features === "number" ? item.supported_features : null,
      capabilities: item.capabilities,
      floor: String(item.floor || ""),
    }));
}

// Sensors of one device are often named alike (battery, temperature, humidity): the class tells them apart.
function entityIdWithClass(item) {
  const label = entityHasDeviceClass(item.id) ? deviceClassLabel(item.device_class, item.id) : "";
  return label ? `${item.id} · ${label}` : item.id;
}

function entityPickerRoomLabel(item) {
  return item.room || t("entity_picker.unassigned_room");
}

function mergeEntityPickerItemsIntoEntities(items, domain) {
  if (!Array.isArray(items) || !items.length) return;
  const byId = new Map(editor.entities.map((entity) => [entity.id, entity]));
  for (const item of items) {
    if (!item?.id) continue;
    const existing = byId.get(item.id);
    if (existing) {
      Object.assign(existing, { search_flags: item.search_flags ?? existing.search_flags,
        supported_features: item.supported_features ?? existing.supported_features, capabilities: item.capabilities || existing.capabilities, unit: item.unit || existing.unit, state_class: item.state_class || existing.state_class,
        device_class: item.device_class || existing.device_class,
        has_price_series: item.has_price_series ?? existing.has_price_series,
        has_solar_forecast: item.has_solar_forecast ?? existing.has_solar_forecast });
      continue;
    }
    const entity = {
      id: item.id,
      name: item.name || item.id,
      domain: domain === "room" ? item.id.split(".")[0] : domain,
      icon: item.icon || "",
      capabilities: item.capabilities || {},
      search_flags: item.search_flags || 0, supported_features: item.supported_features,
      unit: item.unit || "", state_class: item.state_class || "", device_class: item.device_class || "",
      has_price_series: item.has_price_series === true, has_solar_forecast: item.has_solar_forecast === true,
    };
    editor.entities.push(entity);
    byId.set(item.id, entity);
  }
  renderEntityOptions();
}

function groupEntityPickerItemsByRoom(items) {
  const groups = new Map();
  for (const item of items) {
    const label = entityPickerRoomLabel(item);
    if (!groups.has(label)) {
      groups.set(label, []);
    }
    groups.get(label).push(item);
  }
  return [...groups.entries()]
    .map(([room, entries]) => ({
      room,
      entries: entries.sort((a, b) => a.name.localeCompare(b.name) || a.id.localeCompare(b.id)),
    }))
    .sort((a, b) => {
      const unassigned = t("entity_picker.unassigned_room");
      if (a.room === unassigned && b.room !== unassigned) return 1;
      if (b.room === unassigned && a.room !== unassigned) return -1;
      return a.room.localeCompare(b.room);
    });
}

// What a room card gets from an area: main sensor = temperature, then up to 7
// more picked round robin so every kind is present: humidity, presence,
// window/door contacts, lights, covers, fans.
// Several sensors of one kind exist per room (display CPU temperature, motion sensor,
// climate sensor): prefer the ones that look like the room's own.
function roomSensorScore(id, roomName) {
  const lower = id.toLowerCase();
  let score = 0;
  if (/(cpu|display|panel|server|soc|core|chip|processor|nas|router)/.test(lower)) score -= 10;
  if (/(klima|climate|thermo|awair|netatmo)/.test(lower)) score += 3;
  if (/(smart_air|hue_motion)/.test(lower)) score -= 1;
  const slug = deviceSlug(roomName || "");
  if (slug && lower.includes(slug)) score += 2;
  return score;
}

function roomCardCategories(entries, roomName) {
  const cat = { temperature: [], humidity: [], battery: [], presence: [], contact: [], light: [], cover: [], fan: [], climate: [], media: [], lock: [], plug: [] };
  for (const item of entries) {
    const domain = item.id.split(".")[0];
    const dc = item.device_class || "";
    if (domain === "sensor" && dc === "temperature") cat.temperature.push(item.id);
    else if (domain === "sensor" && dc === "humidity") cat.humidity.push(item.id);
    else if ((domain === "sensor" || domain === "binary_sensor") && dc === "battery") cat.battery.push(item.id);
    else if (domain === "binary_sensor" && ["motion", "occupancy", "presence", "moving"].includes(dc)) cat.presence.push(item.id);
    else if (domain === "binary_sensor" && ["window", "door", "opening", "garage_door"].includes(dc)) cat.contact.push(item.id);
    else if (domain === "light") cat.light.push(item.id);
    else if (domain === "cover") cat.cover.push(item.id);
    else if (domain === "fan") cat.fan.push(item.id);
    else if (domain === "climate") cat.climate.push(item.id);
    else if (domain === "media_player") cat.media.push(item.id);
    else if (domain === "lock") cat.lock.push(item.id);
    else if (domain === "switch") cat.plug.push(item.id);
  }
  for (const key of ["temperature", "humidity"]) {
    cat[key].sort((a, b) => roomSensorScore(b, roomName) - roomSensorScore(a, roomName));
  }
  return cat;
}

function roomCardFill(entries, roomName, info = null) {
  const cat = roomCardCategories(entries, roomName);
  // The sensors the Home Assistant area itself designates win over the guess by name.
  const designatedTemp = info && info.temperature_entity_id ? info.temperature_entity_id : "";
  const designatedHum = info && info.humidity_entity_id ? info.humidity_entity_id : "";
  if (designatedHum) cat.humidity = [designatedHum, ...cat.humidity.filter((id) => id !== designatedHum)];
  const primary = designatedTemp || cat.temperature[0] || cat.climate[0] || (entries[0] && entries[0].id) || "";
  const used = new Set([primary]);
  // Sensible order after the temperature: what a room card shows (humidity, presence, windows/doors,
  // lights, batteries) and what it can operate (covers, climate, media, fans, locks, switches).
  const order = ["humidity", "presence", "contact", "light", "cover", "battery", "climate", "media", "fan", "lock", "plug"];
  const extras = [];
  let progressed = true;
  for (let round = 0; extras.length < 7 && progressed; round++) {
    progressed = false;
    for (const key of order) {
      if (round > 0 && (key === "humidity" || key === "battery")) continue; // one of each is enough
      const id = cat[key].find((candidate) => !used.has(candidate));
      if (id && extras.length < 7) {
        extras.push(id);
        used.add(id);
        progressed = true;
      }
    }
  }
  // A second temperature sensor as a fallback when the first one is unavailable.
  const spare = cat.temperature.find((id) => !used.has(id));
  if (spare) {
    if (extras.length >= 7) extras.pop();
    extras.push(spare);
  }
  return { primary, extras };
}

function roomCardSummary(entries, roomName) {
  const cat = roomCardCategories(entries, roomName);
  const parts = [];
  if (cat.temperature.length || cat.humidity.length) parts.push(`${cat.temperature.length + cat.humidity.length} ${t("room.sensors")}`);
  if (cat.contact.length) parts.push(`${cat.contact.length} ${t("room.contacts")}`);
  if (cat.light.length) parts.push(`${cat.light.length} ${t("room.lights")}`);
  if (cat.cover.length) parts.push(`${cat.cover.length} ${t("room.covers")}`);
  if (cat.fan.length) parts.push(`${cat.fan.length} ${t("room.fans")}`);
  if (cat.presence.length) parts.push(`${cat.presence.length} ${t("room.presence")}`);
  if (cat.climate.length) parts.push(`${cat.climate.length} ${t("room.climate")}`);
  if (cat.media.length) parts.push(`${cat.media.length} ${t("room.media")}`);
  if (cat.battery.length) parts.push(`${cat.battery.length} ${t("room.batteries")}`);
  return parts.join(", ");
}

function renderLightEntityPicker(data = {}) {
  if (!el.lightEntityPickerRooms || !el.lightEntityPickerStatus) return;

  const config = entityPickerConfig();
  const domain = config.domain;
  const search = entityPickerSearchValue();
  const cacheKey = entityPickerCacheKey(domain, search);
  const widgetLabel = t(config.widgetKey, {}, config.widgetFallback);
  const itemsLabel = entityPickerItemsLabel(config);
  const searchReady = entityPickerSearchReady(config);
  const liveSearch = entityPickerLiveSearchEnabled(config);
  const inspectorTarget = editor.lightPicker.inspectorTarget;
  const ruleType = inspectorTarget ? inspectorTarget.ruleType ?? inspectorRuleType() :
    (config.displayMode === "door" ? "contact_tile" : (config.widgetType || editor.lightPicker.widgetType));
  const ruleSlot = inspectorTarget?.slot || "primary";
  const sourceItems = Object.prototype.hasOwnProperty.call(data, "items")
    ? data.items
    : editor.lightPicker.items;
  const cachedItems = normalizeEntityPickerItems(sourceItems, domain);
  let items = entityPickerAllowedItems(cachedItems, config);
  if (domain !== "room" && config.restrictionHint !== "entity_picker.filter_printer_role") {
    // Rank (or restrict) the list by device class for the field being filled, like the inspector suggestions.
    items = applyEntityClassRule(items, ruleType, ruleSlot);
  }
  if (el.lightEntityPickerFilter) el.lightEntityPickerFilter.textContent = entityPickerFilterHint(config, ruleType, ruleSlot);

  if (el.lightEntityPickerTitle) {
    el.lightEntityPickerTitle.textContent = t(config.titleKey, {}, config.titleFallback);
  }
  if (el.lightEntityPickerBlankBtn) {
    el.lightEntityPickerBlankBtn.textContent = t(config.blankKey, {}, config.blankFallback);
    // No blank tile for the doorbell: it needs an event entity to be useful.
    el.lightEntityPickerBlankBtn.classList.toggle("hidden", Boolean(inspectorTarget) || editor.lightPicker.widgetType === "doorbell_tile");
  }
  // Filling an inspector field: Remove clears an optional binding, and a typed entity ID Home Assistant does not list
  // can be used as it is.
  if (el.lightEntityPickerRemoveBtn) {
    el.lightEntityPickerRemoveBtn.textContent = t("entity_picker.remove");
    el.lightEntityPickerRemoveBtn.classList.toggle("hidden", Array.isArray(inspectorTarget?.stockSelection) || !inspectorTarget?.slot || inspectorTarget.slot === "primary"
      || Boolean(inspectorTarget.append) || !inspectorTarget.input?.value.trim());
  }
  if (el.lightEntityPickerUseIdBtn) {
    const typed = search.toLowerCase();
    el.lightEntityPickerUseIdBtn.textContent = t("entity_picker.use_id", { id: typed });
    el.lightEntityPickerUseIdBtn.classList.toggle("hidden", Array.isArray(inspectorTarget?.stockSelection) || !inspectorTarget || !/^[a-z0-9_]+\.[a-z0-9_]+$/.test(typed)
      || items.some(item => item.id === typed) || (strictEntitySourceType(ruleType) && !widgetEntitySourceMatches({ id: typed }, ruleType, ruleSlot)));
  }
  renderEntityPickerCurrent(stockPickerTarget() ? null : inspectorTarget);
  renderStockPicker();
  if (el.lightEntityPickerRefreshBtn) {
    el.lightEntityPickerRefreshBtn.textContent = liveSearch ? t("entity_picker.refresh") : t("entity_picker.search");
  }
  if (el.lightEntityPickerSearch && el.lightEntityPickerSearch.value !== editor.lightPicker.search) {
    el.lightEntityPickerSearch.value = editor.lightPicker.search;
  }

  editor.lightPicker.items = items;
  if (items.length > 0 || data.status === "ready" || data.status === "refreshing") {
    // Cache the unfiltered response: another field may allow different domains or Room candidates.
    if (data.status === "ready" || data.status === "refreshing") {
      editor.lightPicker.itemsByDomain[cacheKey] = cachedItems;
      editor.lightPicker.truncatedByDomain[cacheKey] = data.truncated === true;
    }
    editor.lightPicker.hasLoaded = true;
    editor.lightPicker.loadedByDomain[cacheKey] = true;
    mergeEntityPickerItemsIntoEntities(items, domain);
  }
  editor.lightPicker.lastStatus = data.status || editor.lightPicker.lastStatus;
  if (domain === "room" && Array.isArray(data.areas)) {
    editor.lightPicker.areas = new Map(data.areas.map((area) => [area.name, area]));
  }

  const pending = data.pending === true || editor.lightPicker.loading;
  let statusText = "";
  if (data.status === "disconnected") {
    statusText = t("entity_picker.disconnected");
  } else if (!searchReady && !editor.lightPicker.items.length) {
    statusText = t("entity_picker.search_hint", { count: config.minSearch, items: itemsLabel });
  } else if (!liveSearch && searchReady && !editor.lightPicker.hasLoaded && !pending && !editor.lightPicker.items.length) {
    statusText = t("entity_picker.search_ready", { items: itemsLabel });
  } else if (data.status === "refreshing") {
    statusText = t("entity_picker.refreshing_items", { items: itemsLabel });
  } else if (pending && !editor.lightPicker.items.length) {
    statusText = t("entity_picker.pending");
  } else if (!editor.lightPicker.items.length) {
    statusText = t("entity_picker.empty_items", { items: itemsLabel });
  }
  if (data.truncated) {
    statusText = statusText ? `${statusText}\n${t("entity_picker.truncated")}` : t("entity_picker.truncated");
  }
  el.lightEntityPickerStatus.textContent = statusText;

  const loaded = Number.isFinite(Number(data.loaded)) ? Number(data.loaded) : editor.lightPicker.items.length;
  const rawTotal = Number.isFinite(Number(data.total)) ? Number(data.total) : 0;
  const limit = Number.isFinite(Number(data.limit)) && Number(data.limit) > 0 ? Number(data.limit) : loaded;
  const target = rawTotal > 0 ? Math.min(rawTotal, limit) : limit;
  const progressVisible = searchReady && (pending || rawTotal > 0 || data.truncated === true);
  if (el.lightEntityPickerProgress && el.lightEntityPickerProgressBar && el.lightEntityPickerProgressText) {
    el.lightEntityPickerProgress.classList.toggle("hidden", !progressVisible);
    const pct = target > 0 ? Math.min(100, Math.max(0, (loaded * 100) / target)) : (pending ? 8 : 0);
    el.lightEntityPickerProgressBar.style.width = `${pct}%`;
    el.lightEntityPickerProgressText.textContent = rawTotal > 0
      ? t("entity_picker.progress_total", {
        loaded: Math.min(loaded, target),
        target,
        total: rawTotal,
      })
      : t("entity_picker.progress", { loaded, target: target || "..." });
  }

  const groups = groupEntityPickerItemsByRoom(editor.lightPicker.items);
  el.lightEntityPickerRooms.innerHTML = "";
  if (domain === "room" && !inspectorTarget) {
    // Room card: pick an area; the card is pre-filled from what is in it.
    for (const group of groups) {
      if (group.room === t("entity_picker.unassigned_room")) continue;
      const button = document.createElement("button");
      button.className = "light-picker-entity";
      button.type = "button";
      button.innerHTML = `<strong></strong><span></span>`;
      button.querySelector("strong").textContent = group.room;
      button.querySelector("span").textContent = roomCardSummary(group.entries, group.room);
      button.onclick = () => {
        const fill = roomCardFill(group.entries, group.room, editor.lightPicker.areas?.get(group.room));
        closeLightEntityPicker();
        const widget = addWidget("room_tile", {
          entityId: fill.primary,
          title: group.room,
          roomEntities: fill.extras,
        });
        if (widget) {
          setStatus(t("entity_picker.room_added", { room: group.room, count: fill.extras.length + 1 }));
        }
      };
      el.lightEntityPickerRooms.appendChild(button);
    }
    return;
  }
  for (const group of groups) {
    const details = document.createElement("details");
    details.className = "light-picker-room";
    details.open = groups.length <= 3 || group.entries.length <= 8;

    const summary = document.createElement("summary");
    summary.textContent = `${group.room} (${group.entries.length})`;
    details.appendChild(summary);

    const list = document.createElement("div");
    list.className = "light-picker-room-list";
    for (const item of group.entries) {
      const button = document.createElement("button");
      button.className = "light-picker-entity";
      button.type = "button";
      button.title = item.id;
      button.innerHTML = `<strong></strong><span></span>`;
      button.querySelector("strong").textContent = item.name || item.id;
      button.querySelector("span").textContent = entityIdWithClass(item);
      button.classList.toggle("is-current", !inspectorTarget?.append && item.id === inspectorTarget?.input?.value.trim());
      const selection = stockPickerTarget()?.stockSelection;
      if (selection) {
        const selected = selection.indexOf(item.id);
        button.classList.add("stock-picker-choice");
        button.setAttribute("role", "checkbox");
        button.setAttribute("aria-checked", String(selected >= 0));
        button.classList.toggle("is-current", selected >= 0);
        button.disabled = selected < 0 && selection.length >= entityListInputs().length;
        if (selected >= 0) button.querySelector("strong").textContent =
          `${entityListCaption(selected)}: ${item.name || item.id}`;
      }
      button.onclick = () => {
        if (stockPickerTarget()) {
          if (stockPickerToggle(item.id)) renderLightEntityPicker();
          return;
        }
        if (editor.lightPicker.inspectorTarget?.input) {
          if (!assignInspectorEntity(editor.lightPicker.inspectorTarget, item.id)) return;
          closeLightEntityPicker();
          setStatus(t("entity_picker.assigned", { entity: item.id }));
          return;
        }
        addWidget(config.widgetType || editor.lightPicker.widgetType, {
          entityId: item.id,
          title: item.name || item.id,
          displayMode: config.displayMode,
        });
        closeLightEntityPicker();
        setStatus(t("entity_picker.added_widget", { widget: widgetLabel, entity: item.id }));
      };
      list.appendChild(button);
    }
    details.appendChild(list);
    el.lightEntityPickerRooms.appendChild(details);
  }
}

async function fetchLightEntityPicker(options = {}) {
  const refresh = options.refresh === true;
  const pollCount = Number(options.pollCount || 0);
  const config = entityPickerConfig();
  const domain = config.domain;
  const search = entityPickerSearchValue();
  const itemsLabel = entityPickerItemsLabel(config);
  if (!entityPickerSearchReady(config)) {
    editor.lightPicker.loading = false;
    renderLightEntityPicker({
      status: "idle",
      pending: false,
      items: editor.lightPicker.items,
    });
    return;
  }
  const requestSeq = ++editor.lightPicker.requestSeq;
  editor.lightPicker.loading = true;
  if (pollCount === 0 || refresh) {
    const startStatus = refresh && editor.lightPicker.items.length > 0 ? "refreshing" : "pending";
    renderLightEntityPicker({
      status: startStatus,
      pending: true,
      items: editor.lightPicker.items,
    });
  }

  const params = new URLSearchParams();
  params.set("domain", domain);
  if (search) params.set("search", search);
  if (refresh) params.set("refresh", "1");

  try {
    const data = await apiGet(`/api/ha/light_entities${params.toString() ? `?${params.toString()}` : ""}`);
    if (requestSeq !== editor.lightPicker.requestSeq) return;
    editor.lightPicker.loading = data.pending === true;
    renderLightEntityPicker(data);

    if (data.pending === true && pollCount < LIGHT_ENTITY_PICKER_MAX_POLLS) {
      clearLightEntityPickerPoll();
      editor.lightPicker.pollTimerId = window.setTimeout(() => {
        editor.lightPicker.pollTimerId = null;
        void fetchLightEntityPicker({ refresh: false, pollCount: pollCount + 1 });
      }, LIGHT_ENTITY_PICKER_POLL_MS);
    }
  } catch (err) {
    if (requestSeq !== editor.lightPicker.requestSeq) return;
    editor.lightPicker.loading = false;
    const message = t("entity_picker.fetch_failed_items", { items: itemsLabel, error: err.message });
    el.lightEntityPickerStatus.textContent = message;
    setStatus(message, true);
  }
}

// ===== Entity results of the "+ Widget" dialog ===========================================
// A Home Assistant entity found by the dialog's search, with a chip per widget that can show it.
const ENTITY_FIRST_DOOR_CLASSES = ["door", "window", "opening", "garage_door", "lock"];

// Tile choices for an entity, best first. Each: { key, type, displayMode?, sliderDomain? }.
function entityFirstDomainSuggestions(entityId, deviceClass) {
  const domain = String(entityId).split(".")[0];
  const dc = String(deviceClass || "");
  switch (domain) {
    case "light":
      return [{ key: "light_tile", type: "light_tile" }, { key: "button", type: "button" }, { key: "slider", type: "slider", sliderDomain: "light" }];
    case "switch":
    case "input_boolean":
      return [{ key: "button", type: "button" }];
    case "sensor":
      return [{ key: "sensor", type: "sensor" }, { key: "graph", type: "graph" }];
    case "binary_sensor": {
      const door = { key: "door", type: "binary_tile", displayMode: "door" };
      const bin = { key: "binary", type: "binary_tile" };
      return ENTITY_FIRST_DOOR_CLASSES.includes(dc) ? [door, bin] : [bin];
    }
    case "event":
      return [{ key: "event", type: "binary_tile" }];
    case "select":
    case "input_select":
      return [{ key: "select", type: "select_tile" }];
    case "input_datetime":
    case "time":
    case "date":
    case "datetime":
      return [{ key: "datetime", type: "datetime_tile" }];
    case "lock":
      return [{ key: "lock", type: "lock_tile" }, { key: "scene", type: "scene_tile" }];
    case "alarm_control_panel":
      return [{ key: "alarm", type: "alarm_tile" }];
    case "scene":
    case "script":
    case "button":
    case "input_button":
      return [{ key: "scene", type: "scene_tile" }];
    case "climate":
      return [{ key: "heating", type: "heating_tile" }, { key: "climate", type: "climate_tile" }];
    case "cover":
      return [{ key: "cover", type: "cover_tile" }, { key: "slider", type: "slider", sliderDomain: "cover" }];
    case "fan":
      return [{ key: "fan", type: "fan_tile" }];
    case "camera":
    case "image":
      return [{ key: "camera", type: "camera_tile" }];
    case "vacuum":
      return [{ key: "vacuum", type: "vacuum_tile" }];
    case "media_player":
      return [{ key: "media", type: "media_player" }];
    case "weather":
      return [{ key: "weather", type: "weather_tile" }, { key: "forecast", type: "weather_3day" }];
    case "todo":
      return [{ key: "todo", type: "todo_list" }];
    default:
      return [];
  }
}

function entityFirstSuggestions(entityId, deviceClass, metadata = {}) {
  const entity = { id: entityId, device_class: deviceClass || "", ...metadata };
  return entityFirstDomainSuggestions(entityId, deviceClass).filter(choice =>
    entityMatchesWidgetType(entity, choice.type, choice.sliderDomain) &&
    widgetEntitySourceMatches(entity, choice.displayMode === "door" ? "contact_tile" : choice.type));
}

function entityFirstAddTile(item, choice) {
  const widget = addWidget(choice.type, {
    entityId: item.id,
    title: item.name || item.id,
    displayMode: choice.displayMode,
  });
  if (widget && choice.sliderDomain) {
    widget.slider_entity_domain = choice.sliderDomain;
    widget.slider_direction = choice.sliderDomain === "cover" ? "bottom_to_top" : DEFAULT_SLIDER_DIRECTION;
    renderAll();
  }
  if (widget) {
    setStatus(t("wizard.entity.added", { tile: t(`wizard.entity.t.${choice.key}`), entity: item.id }));
  }
}

// One result row of the picker: the entity and a chip per tile that fits it.
function entityFirstRow(item) {
  const row = document.createElement("div");
  row.className = "light-picker-entity entity-first-row";
  row.title = item.id;
  const text = document.createElement("div");
  text.className = "entity-first-text";
  const name = document.createElement("strong");
  name.textContent = item.name || item.id;
  const id = document.createElement("span");
  id.textContent = entityIdWithClass(item);
  text.append(name, id);
  row.appendChild(text);
  const chips = document.createElement("div");
  chips.className = "entity-first-chips";
  const choices = entityFirstSuggestions(item.id, item.device_class, item);
  for (const choice of choices) {
    const chip = document.createElement("button");
    chip.type = "button";
    chip.className = "entity-first-chip";
    chip.textContent = t(`wizard.entity.t.${choice.key}`);
    chip.onclick = () => {
      closeLightEntityPicker();
      closeAddTileDialog();
      entityFirstAddTile(item, choice);
    };
    chips.appendChild(chip);
  }
  if (!choices.length) {
    const none = document.createElement("span");
    none.className = "meta";
    none.textContent = t("wizard.entity.none");
    chips.appendChild(none);
  }
  row.appendChild(chips);
  return row;
}
// ===== end entity results ===================================================================

// Changing an inspector entity: the current one stays on top of the dialog with Keep (and Remove for an optional
// one), so opening the search never hides what is set.
function renderEntityPickerCurrent(target) {
  const box = el.lightEntityPickerCurrent;
  if (!box) return;
  const id = target && !target.append ? (target.input?.value || "").trim() : "";
  box.classList.toggle("hidden", !id);
  if (!id) { if (el.lightEntityPickerRemoveBtn && box.contains(el.lightEntityPickerRemoveBtn)) box.after(el.lightEntityPickerRemoveBtn); return; }
  const info = entityDisplayInfo(id), state = editor.states?.get(id);
  const name = info.name && info.name !== id ? info.name : id;
  const dc = entityHasDeviceClass(id) ? deviceClassLabel(info.device_class, id) : "";
  if (!box.firstChild) {
    box.innerHTML = '<span class="entity-picker-current-tick" aria-hidden="true">\u2713</span><strong></strong><span class="meta"></span><span class="entity-picker-current-actions"></span>';
    const keep = document.createElement("button"); keep.type = "button"; keep.className = "btn small";
    keep.onclick = closeLightEntityPicker; box.lastChild.append(keep);
  }
  box.querySelector("strong").textContent = dc ? `${name} \u00B7 ${dc}` : name;
  const value = state === undefined || state === "" ? "" : [state, info.unit].filter(Boolean).join(" ");
  box.querySelector(".meta").textContent = value ? `${id} \u00B7 ${value}` : id;
  box.title = t("entity_picker.current");
  const actions = box.querySelector(".entity-picker-current-actions");
  actions.firstChild.textContent = t("entity_picker.keep");
  if (el.lightEntityPickerRemoveBtn && !actions.contains(el.lightEntityPickerRemoveBtn)) actions.append(el.lightEntityPickerRemoveBtn);
}

// The search an inspector entity starts with: its ID without the last word, so the list shows its siblings
// ("dg_arbeitszimmer_klima" for sensor.dg_arbeitszimmer_klima_temperature); "" for short IDs.
function entitySiblingSearch(id) {
  const words = String(id || "").split(".")[1]?.split("_") || [];
  return words.length >= 3 ? words.slice(0, -1).join("_") : "";
}

function openLightEntityPicker(widgetType = "light_tile", options = {}) {
  editor.lightPicker.inspectorTarget = options.inspectorTarget || null;
  editor.lightPicker.configOverride = options.configOverride || null;
  const config = entityPickerConfig(widgetType);
  if (!el.lightEntityPickerOverlay) {
    addWidget(config.widgetType || widgetType);
    editor.lightPicker.inspectorTarget = null;
    editor.lightPicker.configOverride = null;
    return;
  }
  editor.lightPicker.widgetType = widgetType;
  editor.lightPicker.domain = config.domain;
  const initialSearch = typeof options.initialSearch === "string" ? options.initialSearch.trim() : "";
  editor.lightPicker.search = initialSearch || (options.resetSearch === true ? "" : editor.lightPicker.searchByDomain[config.domain] || "");
  if (el.lightEntityPickerSearch) {
    el.lightEntityPickerSearch.value = editor.lightPicker.search;
  }
  const cacheKey = entityPickerCacheKey(config.domain);
  editor.lightPicker.items = editor.lightPicker.itemsByDomain[cacheKey] || [];
  editor.lightPicker.hasLoaded = editor.lightPicker.loadedByDomain[cacheKey] === true;
  const shouldAutoFetch =
    !editor.lightPicker.hasLoaded &&
    entityPickerSearchReady(config) &&
    (entityPickerLiveSearchEnabled(config) || initialSearch.length > 0);
  el.lightEntityPickerOverlay.classList.remove("hidden");
  renderLightEntityPicker({
    status: editor.lightPicker.hasLoaded ? "ready" : (shouldAutoFetch ? "pending" : "idle"),
    pending: shouldAutoFetch,
    items: editor.lightPicker.items,
  });
  if (shouldAutoFetch) {
    void fetchLightEntityPicker();
  }
  // Typing replaces a start search the editor chose.
  if (options.selectSearch && el.lightEntityPickerSearch) { el.lightEntityPickerSearch.focus(); el.lightEntityPickerSearch.select(); }
}

function closeLightEntityPicker() {
  clearLightEntityPickerPoll();
  clearLightEntityPickerSearchDebounce();
  editor.lightPicker.requestSeq += 1;
  cancelLightEntityPickerRequest();
  editor.lightPicker.loading = false;
  if (el.lightEntityPickerOverlay) {
    el.lightEntityPickerOverlay.classList.add("hidden");
  }
  editor.lightPicker.inspectorTarget = null;
  editor.lightPicker.configOverride = null;
  if (el.lightEntityPickerBlankBtn) {
    el.lightEntityPickerBlankBtn.classList.remove("hidden");
  }
  // A search may have named an entity the panel does not subscribe (an Energy timeline's battery counter).
  refreshInspectorEntityRows();
}

// "+ Tile": one dialog for every way to add a tile. The search filters the tile types below it by name
// and, from two characters on, also lists Home Assistant entities with a chip per fitting tile
// (entityFirstRow()); a tile type button opens that type's own entity picker, as the menu used to.
// More Home Assistant matches than this only make the list long; the status asks for a narrower search.
const ADD_TILE_MAX_RESULTS = 40;
let addTileSearchTimer = null;
let addTileSearchSeq = 0;

function openAddTileDialog() {
  if (!el.addTileOverlay) return;
  const page = selectedPage();
  if (!page || isFixedPage(page)) {
    setStatus(t("layout.status.energy_page_only"), true);
    return;
  }
  addTileSearchSeq += 1;
  if (el.addTileSearch) el.addTileSearch.value = "";
  if (el.addTileResults) el.addTileResults.textContent = "";
  if (el.addTileSearchStatus) el.addTileSearchStatus.textContent = "";
  const hint = document.getElementById("addTileFilter");
  if (hint) hint.textContent = t("add_tile.filter");
  filterAddTileTypes("");
  const types = document.getElementById("addTileTypes");
  if (types) sortItemsAlphabetically(types, "add-tile-type", "add-tile-group");
  el.addTileOverlay.classList.remove("hidden");
  el.addTileSearch?.focus();
}

function closeAddTileDialog() {
  addTileSearchSeq += 1;
  el.addTileOverlay?.classList.add("hidden");
}

function filterAddTileTypes(query) {
  const q = String(query || "").trim().toLowerCase();
  for (const group of document.querySelectorAll("#addTileTypes .add-tile-group")) {
    let inGroup = 0;
    for (const btn of group.querySelectorAll(".add-tile-type")) {
      const show = !q || btn.textContent.toLowerCase().includes(q);
      btn.classList.toggle("hidden", !show);
      if (show) inGroup += 1;
    }
    group.classList.toggle("hidden", inGroup === 0);
  }
}

async function runAddTileSearch() {
  const status = el.addTileSearchStatus;
  const results = el.addTileResults;
  if (!el.addTileSearch || !status || !results) return;
  const search = el.addTileSearch.value.trim();
  const seq = ++addTileSearchSeq;
  results.textContent = "";
  if (search.length < 2) {
    status.textContent = "";
    return;
  }
  status.textContent = t("entity_picker.pending");
  for (let poll = 0; poll < LIGHT_ENTITY_PICKER_MAX_POLLS; poll++) {
    let data;
    try {
      data = await apiGet(`/api/ha/light_entities?${new URLSearchParams({ domain: "any", search })}`);
    } catch (error) {
      if (seq === addTileSearchSeq) status.textContent = t("entity_picker.fetch_failed_items", { items: t("wizard.entity.items"), error: error.message });
      return;
    }
    if (seq !== addTileSearchSeq) return;
    const items = normalizeEntityPickerItems(data.items, "any").filter(item => entityFirstSuggestions(item.id, item.device_class, item).length);
    results.textContent = "";
    for (const item of items.slice(0, ADD_TILE_MAX_RESULTS)) results.appendChild(entityFirstRow(item));
    if (data.pending !== true) {
      status.textContent = !items.length
        ? t("entity_picker.empty_items", { items: t("wizard.entity.items") })
        : items.length > ADD_TILE_MAX_RESULTS
          ? t("add_tile.entities_more", { shown: ADD_TILE_MAX_RESULTS, count: items.length })
          : t("add_tile.entities_found");
      return;
    }
    await new Promise((resolve) => window.setTimeout(resolve, LIGHT_ENTITY_PICKER_POLL_MS));
    if (seq !== addTileSearchSeq) return;
  }
}

function bindAddTileDialog() {
  if (el.addTileBtn) el.addTileBtn.onclick = () => openAddTileDialog();
  if (el.addTileCloseBtn) el.addTileCloseBtn.onclick = () => closeAddTileDialog();
  el.addTileOverlay?.addEventListener("click", (event) => {
    if (event.target === el.addTileOverlay) closeAddTileDialog();
  });
  if (el.addTileSearch) {
    el.addTileSearch.oninput = () => {
      filterAddTileTypes(el.addTileSearch.value);
      window.clearTimeout(addTileSearchTimer);
      addTileSearchTimer = window.setTimeout(runAddTileSearch, ENTITY_PICKER_SEARCH_DEBOUNCE_MS);
    };
  }
  for (const btn of document.querySelectorAll("#addTileTypes .add-tile-type[data-add-target]")) {
    btn.onclick = () => {
      closeAddTileDialog();
      document.getElementById(btn.dataset.addTarget)?.click();
    };
  }
}
