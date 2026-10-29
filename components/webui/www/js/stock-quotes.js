/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Stocks & ETFs (stock_quotes): up to eight quote sensors in order and the tile's options. The first quote is the primary
// entity field; stock_entities holds the others, kept in order and without gaps. A row can move up; an emptied row closes
// the gap when the widget is saved. Additional rows appear only after a quote is selected.

const STOCK_FIELDS = ["fStock1", "fStock2", "fStock3", "fStock4", "fStock5", "fStock6", "fStock7"];
const STOCK_PERIODS = ["1d", "1w", "30d", "ytd", "1y"];
const STOCK_DEFAULTS = { stock_view: "list", stock_period: "30d", stock_delta: "percent" };

function stockLabel(input) {
  return typeof input?.closest === "function" ? input.closest("label") : null;
}

function stockQuoteInputs() {
  return [el.fEntity, ...STOCK_FIELDS.map(key => el[key])].filter(Boolean);
}

// All three views share the same list. Empty slots are reached through Add quote, not shown as fixed rows.
function stockRefreshRows() {
  const isStock = inspectorWidgetType() === "stock_quotes";
  const inputs = stockQuoteInputs();
  const count = inputs.filter(input => input.value.trim()).length;
  STOCK_FIELDS.forEach(key => {
    const label = stockLabel(el[key]);
    if (label) label.hidden = !isStock || !el[key].value.trim();
    const move = label?.querySelector(".inspector-entity-move");
    if (move) move.hidden = !el[key].value.trim();
  });
  if (el.stockAddQuote) {
    el.stockAddQuote.hidden = !isStock;
    el.stockAddQuote.disabled = !isStock;
    el.stockAddQuote.textContent = t(count >= inputs.length ? "layout.inspector.stock_manage" : "layout.inspector.stock_add", { count, max: inputs.length });
  }
}

function stockAddQuote() {
  if (inspectorWidgetType() !== "stock_quotes") return;
  const index = stockQuoteInputs().findIndex(input => !input.value.trim());
  return openInspectorEntityPicker(index <= 0 ? "primary" : `stock_${index + 1}`, { stockMulti: true });
}

function stockPickerTarget() {
  const target = editor.lightPicker.inspectorTarget;
  return Array.isArray(target?.stockSelection) ? target : null;
}

function entityListInputs(target = stockPickerTarget()) {
  return target?.sensorList ? sensorListInputs() : stockQuoteInputs();
}

function entityListCaption(index) {
  return t(stockPickerTarget()?.sensorList ? "layout.inspector.sensor_list_entry" : "layout.inspector.stock_quote", { n: index + 1 });
}

function stockPickerToggle(id) {
  const target = stockPickerTarget();
  if (!target) return false;
  const index = target.stockSelection.indexOf(id);
  if (index >= 0) target.stockSelection.splice(index, 1);
  else {
    if (target.stockSelection.length >= entityListInputs(target).length) return false;
    target.stockSelection.push(id);
  }
  return true;
}

function stockPickerMove(index) {
  const ids = stockPickerTarget()?.stockSelection;
  if (!ids || index < 1 || index >= ids.length) return;
  [ids[index - 1], ids[index]] = [ids[index], ids[index - 1]];
}

function stockPickerApply() {
  const target = stockPickerTarget();
  if (!target || selectedWidget()?.id !== target.stockWidgetId || inspectorWidgetType() !== (target.sensorList ? "sensor" : "stock_quotes")) return false;
  const ids = target.stockSelection;
  if (!ids.length || ids.length > entityListInputs(target).length) return false;
  entityListInputs(target).forEach((input, index) => { input.value = ids[index] || ""; });
  return autoApplyInspector({ refreshInspector: true, softEntityValidation: true });
}

function renderStockPicker() {
  const target = stockPickerTarget();
  const list = el.stockPickerSelection, apply = el.stockPickerApply;
  if (apply) {
    apply.hidden = !target;
    apply.disabled = !target?.stockSelection.length;
    apply.textContent = t("layout.inspector.stock_apply", { count: target?.stockSelection.length || 0, max: entityListInputs(target).length });
  }
  if (!list) return;
  list.hidden = !target;
  list.replaceChildren();
  if (!target) return;
  target.stockSelection.forEach((id, index) => {
    const row = document.createElement("div");
    row.className = "stock-picker-slot";
    const slot = document.createElement("span");
    slot.textContent = entityListCaption(index);
    const name = document.createElement("strong");
    name.textContent = editor.entities.find(entity => entity.id === id)?.name || id;
    name.title = id;
    const up = document.createElement("button");
    up.type = "button"; up.className = "btn small"; up.textContent = "↑";
    up.disabled = index === 0; up.title = t("layout.inspector.stock_move_up");
    up.setAttribute("aria-label", `${up.title}: ${name.textContent}`);
    up.onclick = () => { stockPickerMove(index); renderLightEntityPicker(); };
    const remove = document.createElement("button");
    remove.type = "button"; remove.className = "btn small"; remove.textContent = "×";
    remove.title = t("entity_picker.remove");
    remove.setAttribute("aria-label", `${remove.title}: ${name.textContent}`);
    remove.onclick = () => { stockPickerToggle(id); renderLightEntityPicker(); };
    row.append(slot, name, up, remove);
    list.appendChild(row);
  });
}

function stockLoadInspector(widget) {
  const isStock = widget?.type === "stock_quotes";
  if (el.stockEntities) el.stockEntities.classList.toggle("hidden", !isStock);
  if (el.stockOptions) el.stockOptions.classList.toggle("hidden", !isStock);
  const more = isStock && Array.isArray(widget.stock_entities) ? widget.stock_entities : [];
  STOCK_FIELDS.forEach((key, index) => {
    if (el[key]) el[key].value = typeof more[index] === "string" ? more[index] : "";
  });
  if (el.fStockView) el.fStockView.value = isStock && ["chart", "grid"].includes(widget.stock_view) ? widget.stock_view : "list";
  if (el.fStockPeriod) el.fStockPeriod.value = isStock && STOCK_PERIODS.includes(widget.stock_period) ? widget.stock_period : "30d";
  if (el.fStockDelta) el.fStockDelta.value = isStock && ["absolute", "both"].includes(widget.stock_delta) ? widget.stock_delta : "percent";
  if (el.fStockTicker) el.fStockTicker.checked = isStock && widget.stock_ticker === true;
  if (el.fStockSelector) el.fStockSelector.checked = isStock && widget.stock_selector !== false;
  stockSyncSelectorRow();
  stockRefreshRows();
}

// The period buttons exist only in the chart and tile views; the saved choice is kept while the list view hides the control.
function stockSyncSelectorRow() {
  if (el.fStockSelectorRow) el.fStockSelectorRow.hidden = !["chart", "grid"].includes(el.fStockView?.value);
}

// Writes the quotes in order without gaps and the options that differ from the defaults; another type keeps none.
function stockSaveInspector(widget, widgetType) {
  for (const key of ["stock_entities", "stock_view", "stock_period", "stock_delta", "stock_ticker", "stock_selector"]) delete widget[key];
  if (widgetType !== "stock_quotes") return;
  const ids = stockQuoteInputs().map(input => input.value.trim()).filter(Boolean);
  if (ids.length) widget.entity_id = ids[0];
  const more = ids.slice(1, STOCK_FIELDS.length + 1);
  if (more.length) widget.stock_entities = more;
  const options = { stock_view: el.fStockView?.value, stock_period: el.fStockPeriod?.value, stock_delta: el.fStockDelta?.value };
  for (const [key, value] of Object.entries(options)) {
    if (value && value !== STOCK_DEFAULTS[key]) widget[key] = value;
  }
  if (el.fStockTicker?.checked) widget.stock_ticker = true;
  if (el.fStockSelector && !el.fStockSelector.checked) widget.stock_selector = false; // on unless the layout says false
  delete widget.secondary_entity_id;
  delete widget.tertiary_entity_id;
}

// Moves the quote of row `index` (0 = the first quote) one row up and saves.
function stockMoveUp(index) {
  const inputs = stockQuoteInputs();
  if (index < 1 || index >= inputs.length || !inputs[index].value.trim()) return;
  const above = inputs[index - 1].value;
  inputs[index - 1].value = inputs[index].value;
  inputs[index].value = above;
  inputs[index - 1].dispatchEvent(new Event("change", { bubbles: true }));
  inputs[index].dispatchEvent(new Event("change", { bubbles: true }));
}

// Empties quote row `index` (0 is the first row after the primary entity); saving closes the gap.
function stockQuoteRemove(index) {
  if (inspectorWidgetType() !== "stock_quotes" || !el[STOCK_FIELDS[index]]) return;
  el[STOCK_FIELDS[index]].value = "";
  el[STOCK_FIELDS[index]].dispatchEvent(new Event("change", { bubbles: true }));
}

// The move-up buttons beside the rows 2..8 (inspector-layout.js turns the fields into entity rows first).
function bindStockInspector() {
  el.stockAddQuote?.addEventListener("click", stockAddQuote);
  el.stockPickerApply?.addEventListener("click", () => { if (stockPickerApply()) closeLightEntityPicker(); });
  STOCK_FIELDS.forEach((key, index) => {
    const label = stockLabel(el[key]);
    if (!label || label.querySelector(".inspector-entity-move")) return;
    const button = document.createElement("button");
    button.type = "button";
    button.className = "inspector-entity-move";
    button.textContent = "↑";
    button.title = t("layout.inspector.stock_move_up");
    button.setAttribute("aria-label", button.title);
    button.onclick = event => { event.preventDefault(); event.stopPropagation(); stockMoveUp(index + 1); };
    const remove = document.createElement("button");
    remove.type = "button"; remove.className = "inspector-entity-remove"; remove.textContent = "×";
    remove.title = t("entity_picker.remove"); remove.setAttribute("aria-label", remove.title);
    remove.onclick = event => { event.preventDefault(); event.stopPropagation(); stockQuoteRemove(index); };
    label.append(button, remove);
  });
  for (const input of stockQuoteInputs()) input.addEventListener("change", stockRefreshRows);
}

// ---- preview: the panel's rows (w_stock_quotes.c) with the editor's names, prices and units; the changes and the curves
// are samples, as the editor does not receive the quote attributes ----

const STOCK_PREVIEW_CHANGES = [0.40, -0.08, 1.12, -0.36, 0, 2.05, -1.4, 0.7];

function stockPreviewSymbol(id) {
  const object = String(id || "").replace(/^sensor\./, "");
  return object.replace(/_([a-z]{1,2})$/, ".$1").replace(/_/g, " ").toUpperCase();
}

function stockPreviewMarkup(widget, name, height) {
  const ids = [widget.entity_id, ...(Array.isArray(widget.stock_entities) ? widget.stock_entities : [])].filter(Boolean);
  const locale = t("preview.parqet.locale");
  const number = (v, digits, plus) => (plus && v > 0 ? "+" : "") + v.toLocaleString(locale, { minimumFractionDigits: digits, maximumFractionDigits: digits });
  const sign = v => (v > 0 ? "up" : v < 0 ? "down" : "");
  const quote = (id, index) => {
    const entity = editor.entities.find(item => item?.id === id);
    const raw = Number(editor.states.get(id));
    const price = Number.isFinite(raw) && String(editor.states.get(id) ?? "") !== "" ? raw : 100 + index * 37.5;
    const unit = entity?.unit || "EUR";
    const currency = unit === "EUR" ? "\u20ac" : unit === "USD" ? "$" : unit;
    return { name: entity?.name && entity.name !== id ? entity.name : stockPreviewSymbol(id), symbol: stockPreviewSymbol(id), price, currency,
      change: STOCK_PREVIEW_CHANGES[index % STOCK_PREVIEW_CHANGES.length] };
  };
  const quotes = (ids.length ? ids : ["sensor.demo"]).map(quote);
  const delta = widget.stock_delta === "absolute" ? "absolute" : widget.stock_delta === "both" ? "both" : "percent";
  const amountOf = q => q.price * q.change / 100;
  if (widget.stock_view === "chart" || widget.stock_view === "grid") {
    const period = STOCK_PERIODS.includes(widget.stock_period) ? widget.stock_period : "30d";
    const chips = widget.stock_selector === false ? "" : STOCK_PERIODS.map(code => `<em class="${previewControlClass(code === period)}">${escapeHtml(t(`layout.option.stock_period_short.${code}`))}</em>`).join("");
    const sections = quotes.map((q, i) => {
      const change = delta === "absolute" ? `${number(amountOf(q), 2, true)} ${q.currency}` : delta === "both"
        ? `${number(amountOf(q), 2, true)} ${q.currency}  ${number(q.change, 2, true)} %` : `${number(q.change, 2, true)} %`;
      // The period's reference price after the change, as the panel's "since 126.84 €" (w_stock_quotes.c).
      const since = t("preview.stock.since", { price: `${number(q.price - amountOf(q), 2)} ${q.currency}` });
      const points = Array.from({ length: 24 }, (_, k) => {
        const y = 50 - q.change * 6 * (k / 23) + 8 * Math.sin(k / 3 + i);
        return `${(k / 23 * 100).toFixed(1)},${Math.max(4, Math.min(96, y)).toFixed(1)}`;
      }).join(" ");
      return `<div class="tp-stock-section"><div class="tp-stock-row"><span>${escapeHtml(q.name)}${widget.stock_ticker ? ` \u00b7 ${escapeHtml(q.symbol)}` : ""}</span><b>${number(q.price, 2)} ${escapeHtml(q.currency)}</b></div>
        <small><em class="${sign(q.change)}">${escapeHtml(change)}</em> ${escapeHtml(since)}</small>
        <svg class="tp-stock-spark ${sign(q.change)}" viewBox="0 0 100 100" preserveAspectRatio="none"><polyline points="${points}"/></svg></div>`;
    }).join("");
    if (widget.stock_view === "grid") {
      // One card per quote in two columns, one when the tile is narrow (w_stock_quotes.c).
      const cards = quotes.map((q, i) => {
        const arrow = q.change > 0 ? "↑ " : q.change < 0 ? "↓ " : "";
        const change = delta === "absolute" ? `${number(amountOf(q), 2, true)} ${q.currency}`
          : `${arrow}${number(q.change, 2, true)} %`;
        const amount = delta === "both"
          ? `<small class="tp-stock-card-amount ${sign(q.change)}">${number(amountOf(q), 2, true)} ${escapeHtml(q.currency)}</small>` : "";
        const points = Array.from({ length: 24 }, (_, k) => {
          const y = 50 - q.change * 6 * (k / 23) + 8 * Math.sin(k / 3 + i);
          return `${(k / 23 * 100).toFixed(1)},${Math.max(4, Math.min(96, y)).toFixed(1)}`;
        }).join(" ");
        return `<div class="tp-stock-card"><div class="tp-stock-row"><span>${escapeHtml(q.name)}</span>${widget.stock_ticker ? `<small>${escapeHtml(q.symbol)}</small>` : ""}</div>
          <div class="tp-stock-row"><b>${number(q.price, 2)} ${escapeHtml(q.currency)}</b><em class="${sign(q.change)}">${escapeHtml(change)}</em></div>${amount}
          <svg class="tp-stock-spark ${sign(q.change)}" viewBox="0 0 100 100" preserveAspectRatio="none"><polyline points="${points}"/></svg></div>`;
      }).join("");
      return `<div class="tp tp-stock">${name("div", 'class="tp-name"')}${chips ? `<div class="tp-stock-chips">${chips}</div>` : ""}<div class="tp-stock-grid">${cards}</div></div>`;
    }
    return `<div class="tp tp-stock">${name("div", 'class="tp-name"')}${chips ? `<div class="tp-stock-chips">${chips}</div>` : ""}<div class="tp-stock-list">${sections}</div></div>`;
  }
  const rows = quotes.map(q => {
    const first = delta === "absolute" ? `${number(amountOf(q), 2, true)} ${q.currency}` : `${number(q.change, 2, true)} %`;
    const second = delta === "both" ? `<small class="${sign(q.change)}">${number(amountOf(q), 2, true)} ${escapeHtml(q.currency)}</small>` : "";
    const ticker = widget.stock_ticker ? `<small>${escapeHtml(q.symbol)}</small>` : "";
    return `<div class="tp-stock-row"><span>${escapeHtml(q.name)}${ticker}</span><b>${number(q.price, 2)} ${escapeHtml(q.currency)}</b><em class="${sign(q.change)}">${escapeHtml(first)}${second}</em></div>`;
  }).join("");
  return `<div class="tp tp-stock${height < 160 ? " tp-stock-short" : ""}">${name("div", 'class="tp-name"')}<div class="tp-stock-list">${rows}</div></div>`;
}

