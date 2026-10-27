/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Sensor list selections reuse the bounded, ordered multi-picker used by Stocks.
const SENSOR_LIST_FIELDS = Array.from({ length: 7 }, (_, i) => `fSensorList${i + 1}`);

function sensorListInputs() {
  return [el.fEntity, ...SENSOR_LIST_FIELDS.map(key => el[key])].filter(Boolean);
}

function sensorListLoadInspector(widget) {
  const active = widget?.type === "sensor", list = active && widget.sensor_view === "list";
  if (el.fSensorView) el.fSensorView.value = list ? "list" : "single";
  if (el.sensorListEntities) el.sensorListEntities.hidden = !list;
  SENSOR_LIST_FIELDS.forEach((key, index) => {
    if (!el[key]) return;
    el[key].value = active && Array.isArray(widget.sensor_entities) ? widget.sensor_entities[index] || "" : "";
    const label = el[key].closest?.("label");
    if (label) label.hidden = !list || !el[key].value.trim();
  });
  if (el.sensorListAdd) {
    const count = sensorListInputs().filter(input => input.value.trim()).length;
    el.sensorListAdd.textContent = t(count >= 8 ? "layout.inspector.sensor_list_manage" : "layout.inspector.sensor_list_add", { count, max: 8 });
  }
}

function sensorListSaveInspector(widget, type) {
  if (type !== "sensor") { delete widget.sensor_view; delete widget.sensor_entities; return; }
  if (el.fSensorView?.value === "list") widget.sensor_view = "list";
  else delete widget.sensor_view;
  const ids = [...new Set(sensorListInputs().map(input => input.value.trim()).filter(Boolean))].slice(0, 8);
  if (widget.sensor_view === "list" && ids.length) widget.entity_id = ids[0];
  const more = ids.filter(id => id !== widget.entity_id).slice(0, 7);
  if (more.length) widget.sensor_entities = more;
  else delete widget.sensor_entities;
}

// Empties list row `index` (0 is the first row after the primary entity); saving closes the gap.
function sensorListRemove(index) {
  if (inspectorWidgetType() !== "sensor" || !el[SENSOR_LIST_FIELDS[index]]) return;
  el[SENSOR_LIST_FIELDS[index]].value = "";
  autoApplyInspector({ refreshInspector: true, softEntityValidation: true });
}

function sensorListAdd() {
  if (inspectorWidgetType() !== "sensor" || el.fSensorView?.value !== "list") return;
  const ids = [...new Set(sensorListInputs().map(input => input.value.trim()).filter(Boolean))];
  const target = { ...inspectorEntityPickerTarget("primary"), stockSelection: ids,
    stockWidgetId: selectedWidget()?.id, sensorList: true, ruleType: "sensor" };
  openLightEntityPicker("inspector_sensor_list", { inspectorTarget: target,
    configOverride: { ...entityPickerConfigForDomain("any", "layout.inspector.sensor_list_choose", "Choose sensors"),
      allowedDomains: ["sensor", "binary_sensor"] }, initialSearch: "", resetSearch: true, selectSearch: true });
}

function bindSensorListInspector() {
  bindInspectorAutoApply(el.fSensorView, ["change"], { refreshInspector: true, softEntityValidation: true });
  el.sensorListAdd?.addEventListener("click", sensorListAdd);
  SENSOR_LIST_FIELDS.forEach((key, index) => {
    bindInspectorAutoApply(el[key], ["change"], { refreshInspector: true, softEntityValidation: true });
    const label = el[key]?.closest?.("label");
    if (!label) return;
    const up = document.createElement("button");
    up.type = "button"; up.className = "inspector-entity-move"; up.textContent = "↑";
    up.setAttribute("aria-label", t("layout.inspector.stock_move_up"));
    up.onclick = event => {
      event.preventDefault(); event.stopPropagation();
      const inputs = sensorListInputs(), at = index + 1;
      [inputs[at - 1].value, inputs[at].value] = [inputs[at].value, inputs[at - 1].value];
      autoApplyInspector({ refreshInspector: true, softEntityValidation: true });
    };
    const remove = document.createElement("button");
    remove.type = "button"; remove.className = "inspector-entity-remove"; remove.textContent = "×";
    remove.title = t("entity_picker.remove"); remove.setAttribute("aria-label", remove.title);
    remove.onclick = event => { event.preventDefault(); event.stopPropagation(); sensorListRemove(index); };
    label.append(up, remove);
  });
}

function sensorListPreview(widget, name) {
  const ids = [...new Set([widget.entity_id, ...(widget.sensor_entities || [])].filter(Boolean))].slice(0, 8);
  const rows = ids.map(id => {
    const title = editor.entities.find(entity => entity.id === id)?.name || id;
    const value = previewSensorValue({ entity_id: id }) ?? previewEntityValue(id, "--");
    const age = widget.sensor_show_age !== false ? `<small>${escapeHtml(t("preview.sensor.just_now"))}</small>` : "";
    return `<div class="tp-sensor-list-row"><span>${escapeHtml(title)}</span><b>${escapeHtml(value)}</b>${age}</div>`;
  }).join("");
  return `<div class="tp tp-sensor-list">${name("small", "")}<div class="tp-sensor-list-rows">${rows}</div></div>`;
}
