/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Optional bindings keep their original inputs, validation, search rules and JSON keys.
// Only configured roles are visible; choosing a free role opens its existing entity picker.
const OPTIONAL_ENTITY_ROLES = {
  car_widget: [["fSecondaryEntity", "secondary"], ["fTertiaryEntity", "tertiary"],
    ["fCarPrecond", "car_precond"], ["fCarLock", "car_lock"], ["fCarTemp", "car_temp"]],
  parqet_portfolio: [["fSecondaryEntity", "secondary"], ["fParqetReturn", "parqet_return"],
    ["fTertiaryEntity", "tertiary"], ["fParqetDividends", "parqet_dividends"]],
  energy_timeline: [["fSecondaryEntity", "secondary"], ["fTertiaryEntity", "tertiary"],
    ["fEtlCharge", "etl_charge"], ["fEtlDischarge", "etl_discharge"], ["fEtlSoc", "etl_soc"], ["fEtlForecast", "etl_forecast"]],
  gauge: [["fSecondaryEntity", "secondary"], ["fTertiaryEntity", "tertiary"]],
  solar_forecast: [["fSolarForecastToday", "solar_today"], ["fSolarForecastTomorrow", "solar_tomorrow"],
    ...[3, 4, 5, 6, 7].map(day => [`fSolarForecastDay${day}`, `solar_day_${day}`])],
};

function optionalEntityRoles(type = inspectorWidgetType()) {
  return OPTIONAL_ENTITY_ROLES[type] || [];
}

// Forecast days are positional: only the next free day is offered and only the last configured day can be removed,
// so a saved layout never has a gap between days.
function optionalEntitySequential(type = inspectorWidgetType()) {
  return type === "solar_forecast";
}

function optionalEntityChoices() {
  const free = optionalEntityRoles().filter(([field]) => el[field] && !el[field].disabled && !el[field].value.trim());
  return optionalEntitySequential() ? free.slice(0, 1) : free;
}

function optionalEntityRemovable(field) {
  if (!optionalEntitySequential()) return true;
  const configured = optionalEntityRoles().filter(([key]) => el[key]?.value.trim());
  return configured.length > 0 && configured[configured.length - 1][0] === field;
}

function optionalEntityAdd() {
  const slot = el.optionalEntityRole?.value;
  if (!optionalEntityChoices().some(([, role]) => role === slot)) return;
  return openInspectorEntityPicker(slot);
}

function optionalEntityRemove(field) {
  if (!optionalEntityRoles().some(([key]) => key === field) || !el[field] || !optionalEntityRemovable(field)) return false;
  el[field].value = "";
  el[field].dispatchEvent(new Event("change", { bubbles: true }));
  return true;
}

function bindOptionalEntities() {
  el.optionalEntityAdd?.addEventListener("click", optionalEntityAdd);
  el.roomEntityAdd?.addEventListener("click", () => openInspectorEntityPicker("room_entities"));
  const fields = [...new Set(Object.values(OPTIONAL_ENTITY_ROLES).flat().map(([field]) => field))];
  for (const field of fields) {
    const label = el[field]?.closest?.("label");
    if (!label || label.querySelector(".inspector-entity-remove")) continue;
    const button = document.createElement("button");
    button.type = "button"; button.className = "inspector-entity-remove"; button.textContent = "×";
    button.hidden = true;
    button.onclick = event => {
      event.preventDefault(); event.stopPropagation(); optionalEntityRemove(field);
    };
    label.append(button);
  }
}

function refreshOptionalEntities() {
  const roles = optionalEntityRoles(), fields = roles.map(([field]) => field);
  const allFields = [...new Set(Object.values(OPTIONAL_ENTITY_ROLES).flat().map(([field]) => field))];
  for (const field of allFields) {
    const input = el[field], label = input?.closest?.("label");
    if (!label) continue;
    const active = fields.includes(field), configured = !!input.value.trim();
    label.hidden = active && !configured;
    const remove = label.querySelector(".inspector-entity-remove");
    if (remove) {
      remove.hidden = !active || !configured || !optionalEntityRemovable(field);
      remove.title = t("entity_picker.remove");
      remove.setAttribute("aria-label", `${remove.title}: ${label.querySelector(".inspector-entity-role")?.textContent || ""}`);
    }
  }
  if (el.optionalEntityControls) {
    const choices = optionalEntityChoices(), select = el.optionalEntityRole, previous = select.value;
    el.optionalEntityControls.hidden = choices.length === 0;
    select.replaceChildren();
    for (const [, slot] of choices) {
      const target = inspectorEntityPickerTarget(slot);
      const option = document.createElement("option");
      option.value = slot;
      option.textContent = t(target.titleKey, {}, target.titleFallback).replace(/\s*\(.*$/, "");
      select.appendChild(option);
    }
    select.value = choices.some(([, slot]) => slot === previous) ? previous : choices[0]?.[1] || "";
    select.setAttribute("aria-label", t("layout.inspector.optional_role"));
    el.optionalEntityAdd.textContent = t("layout.inspector.optional_add");
    el.optionalEntityAdd.disabled = choices.length === 0;
  }
  refreshRoomEntityList();
}

function roomEntityWrite(ids) {
  el.fRoomEntities.value = [...new Set(ids)].filter(id => id && id !== el.fEntity.value.trim()).slice(0, 7).join("\n");
  el.fRoomEntities.dispatchEvent(new Event("change", { bubbles: true }));
}

function roomEntityMove(index) {
  const ids = roomEntitiesFromInspector();
  if (inspectorWidgetType() !== "room_tile" || index < 1 || index >= ids.length) return;
  [ids[index - 1], ids[index]] = [ids[index], ids[index - 1]];
  roomEntityWrite(ids);
}

function roomEntityRemove(index) {
  if (inspectorWidgetType() !== "room_tile") return;
  roomEntityWrite(roomEntitiesFromInspector().filter((_, at) => at !== index));
}

function roomEntityEdit(index) {
  if (inspectorWidgetType() !== "room_tile") return;
  const ids = roomEntitiesFromInspector(), widget = selectedWidget();
  if (!ids[index]) return;
  let value = ids[index];
  // The standard picker writes through this input adapter, then emits its normal change event.
  const input = {
    get value() { return value; }, set value(next) { value = next; },
    focus() {},
    dispatchEvent() {
      if (selectedWidget() !== widget) return;
      ids[index] = value; roomEntityWrite(ids);
    },
  };
  const domains = ["sensor", "binary_sensor", "light", "cover", "fan", "climate", "media_player", "lock", "switch", "vacuum"];
  const target = { input, domain: "any", allowedDomains: domains, slot: "room_entities", ruleType: "room_tile",
    titleKey: "layout.inspector.room_entities", titleFallback: "Room entities",
    excludedEntityIds: [el.fEntity.value, ...ids.filter((_, at) => at !== index)] };
  openLightEntityPicker("inspector_room_entities", { inspectorTarget: target,
    configOverride: { ...entityPickerConfigForDomain("any", target.titleKey, target.titleFallback),
      allowedDomains: domains, excludedEntityIds: target.excludedEntityIds },
    initialSearch: entitySiblingSearch(value), resetSearch: true, selectSearch: true });
}

function refreshRoomEntityList() {
  if (!el.roomEntityList || !el.roomEntityAdd) return;
  const active = inspectorWidgetType() === "room_tile", ids = roomEntitiesFromInspector();
  el.roomEntityList.hidden = !active;
  el.roomEntityAdd.hidden = !active;
  el.roomEntityAdd.disabled = ids.length >= 7;
  el.roomEntityAdd.textContent = t("layout.inspector.room_add", { count: ids.length, max: 7 });
  el.roomEntityList.replaceChildren();
  const label = el.fRoomEntities.closest?.("label");
  if (label) label.hidden = active;
  if (!active) return;
  ids.forEach((id, index) => {
    const row = document.createElement("div"); row.className = "inspector-room-entity";
    const name = editor.entities.find(entity => entity.id === id)?.name || id;
    const edit = document.createElement("button"); edit.type = "button"; edit.className = "inspector-entity-row";
    edit.textContent = `${index + 1}. ${name}`; edit.title = id; edit.onclick = () => roomEntityEdit(index);
    const up = document.createElement("button"); up.type = "button"; up.className = "inspector-entity-move";
    up.textContent = "↑"; up.disabled = index === 0;
    up.setAttribute("aria-label", `${t("layout.inspector.stock_move_up")}: ${name}`);
    up.onclick = () => roomEntityMove(index);
    const remove = document.createElement("button"); remove.type = "button"; remove.className = "inspector-entity-remove";
    remove.textContent = "×"; remove.setAttribute("aria-label", `${t("entity_picker.remove")}: ${name}`);
    remove.onclick = () => roomEntityRemove(index);
    row.append(edit, up, remove); el.roomEntityList.appendChild(row);
  });
}
