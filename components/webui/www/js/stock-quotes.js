/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Stocks & ETFs (stock_quotes): up to eight quote sensors in order and the tile's options. The first quote is the primary
// entity field; stock_entities holds the others, kept in order and without gaps. A row can move up; an emptied row closes
// the gap when the widget is saved. Only the filled rows and one empty row are shown.

const STOCK_FIELDS = ["fStock1", "fStock2", "fStock3", "fStock4", "fStock5", "fStock6", "fStock7"];
const STOCK_PERIODS = ["1d", "1w", "30d", "ytd", "1y"];
const STOCK_DEFAULTS = { stock_view: "list", stock_period: "30d", stock_delta: "percent" };

function stockLabel(input) {
  return typeof input?.closest === "function" ? input.closest("label") : null;
}

function stockQuoteInputs() {
  return [el.fEntity, ...STOCK_FIELDS.map(key => el[key])].filter(Boolean);
}

// Shows the rows that hold a quote and the first empty one; the others wait until the one above is filled.
function stockRefreshRows() {
  const isStock = inspectorWidgetType() === "stock_quotes";
  const inputs = stockQuoteInputs();
  let shown = 1;
  while (shown < inputs.length && inputs[shown - 1].value.trim()) shown++;
  STOCK_FIELDS.forEach((key, index) => {
    const label = stockLabel(el[key]);
    if (label) label.hidden = !isStock || index + 1 >= shown;
    const move = label?.querySelector(".inspector-entity-move");
    if (move) move.hidden = !el[key].value.trim(); /* an empty row has nothing to move */
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
  if (el.fStockView) el.fStockView.value = isStock && widget.stock_view === "chart" ? "chart" : "list";
  if (el.fStockPeriod) el.fStockPeriod.value = isStock && STOCK_PERIODS.includes(widget.stock_period) ? widget.stock_period : "30d";
  if (el.fStockDelta) el.fStockDelta.value = isStock && ["absolute", "both"].includes(widget.stock_delta) ? widget.stock_delta : "percent";
  if (el.fStockTicker) el.fStockTicker.checked = isStock && widget.stock_ticker === true;
  stockRefreshRows();
}

// Writes the quotes in order without gaps and the options that differ from the defaults; another type keeps none.
function stockSaveInspector(widget, widgetType) {
  for (const key of ["stock_entities", "stock_view", "stock_period", "stock_delta", "stock_ticker"]) delete widget[key];
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

// The move-up buttons beside the rows 2..8 (inspector-layout.js turns the fields into entity rows first).
function bindStockInspector() {
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
    label.append(button);
  });
  for (const input of stockQuoteInputs()) input.addEventListener("change", stockRefreshRows);
}

// ---- preview: the panel's rows (w_stock_quotes.c) with the editor's names, prices and units; the changes and the curves
// are samples, as the editor does not receive the quote attributes ----

const STOCK_PREVIEW_CHANGES = [0.40, -0.08, 1.12, -0.36, 0, 2.05, -1.4, 0.7];

function stockPreviewSymbol(id) {
  const object = String(id || "").replace(/^sensor\./, "").replace(/^yahoofinance_/, "");
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
  if (widget.stock_view === "chart") {
    const period = STOCK_PERIODS.includes(widget.stock_period) ? widget.stock_period : "30d";
    const chips = STOCK_PERIODS.map(code => `<em class="${code === period ? "on" : ""}">${escapeHtml(t(`layout.option.stock_period_short.${code}`))}</em>`).join("");
    const sections = quotes.map((q, i) => {
      const change = delta === "absolute" ? `${number(amountOf(q), 2, true)} ${q.currency}` : delta === "both"
        ? `${number(amountOf(q), 2, true)} ${q.currency}  ${number(q.change, 2, true)} %` : `${number(q.change, 2, true)} %`;
      const points = Array.from({ length: 24 }, (_, k) => {
        const y = 50 - q.change * 6 * (k / 23) + 8 * Math.sin(k / 3 + i);
        return `${(k / 23 * 100).toFixed(1)},${Math.max(4, Math.min(96, y)).toFixed(1)}`;
      }).join(" ");
      return `<div class="tp-stock-section"><div class="tp-stock-row"><span>${escapeHtml(q.name)}${widget.stock_ticker ? ` \u00b7 ${escapeHtml(q.symbol)}` : ""}</span><b>${number(q.price, 2)} ${escapeHtml(q.currency)}</b></div>
        <small class="${sign(q.change)}">${escapeHtml(change)}</small>
        <svg class="tp-stock-spark ${sign(q.change)}" viewBox="0 0 100 100" preserveAspectRatio="none"><polyline points="${points}"/></svg></div>`;
    }).join("");
    return `<div class="tp tp-stock">${name("div", 'class="tp-name"')}<div class="tp-stock-chips">${chips}</div><div class="tp-stock-list">${sections}</div></div>`;
  }
  const rows = quotes.map(q => {
    const first = delta === "absolute" ? `${number(amountOf(q), 2, true)} ${q.currency}` : `${number(q.change, 2, true)} %`;
    const second = delta === "both" ? `<small class="${sign(q.change)}">${number(amountOf(q), 2, true)} ${escapeHtml(q.currency)}</small>` : "";
    const ticker = widget.stock_ticker ? `<small>${escapeHtml(q.symbol)}</small>` : "";
    return `<div class="tp-stock-row"><span>${escapeHtml(q.name)}${ticker}</span><b>${number(q.price, 2)} ${escapeHtml(q.currency)}</b><em class="${sign(q.change)}">${escapeHtml(first)}${second}</em></div>`;
  }).join("");
  return `<div class="tp tp-stock${height < 160 ? " tp-stock-short" : ""}">${name("div", 'class="tp-name"')}<div class="tp-stock-list">${rows}</div></div>`;
}

