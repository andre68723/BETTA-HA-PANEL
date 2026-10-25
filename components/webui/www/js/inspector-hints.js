/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
// Inspector explanations open in a modal popup; settings retain their sentence summaries.
// Texts are set from many places, so a MutationObserver follows changes and visibility.
const HINT_MAX = 100;
const HINT_MAX_SETTINGS = 180; // a first sentence may be longer in the settings, where nothing is cut off mid-sentence
const HINT_MIN_SENTENCE = 25; // "Optional." alone is no summary: the next sentence joins it

// Legacy summary helpers stay compatible with saved markup. Inspector hints use the popup path below;
// settings shorten only at a real sentence end and leave status lines and warnings whole.
function hintLimit(el) {
  return el.closest("#inspectorSection") ? HINT_MAX : HINT_MAX_SETTINGS;
}

function hintSentenceEnd(full, limit) {
  const re = /[.!?](\s|$)/g;
  let match;
  while ((match = re.exec(full))) {
    const end = match.index + 1;
    if (end >= HINT_MIN_SENTENCE) return end < limit ? end : -1;
  }
  return -1;
}

function hintShortenable(el, full) {
  if (full.length <= HINT_MAX || el.classList.contains("no-shorten")) return false;
  if (el.closest("#inspectorSection")) return true;
  return !full.includes(" | ") && hintSentenceEnd(full, HINT_MAX_SETTINGS) > 0;
}

function hintFirstSentence(full, el) {
  const limit = el ? hintLimit(el) : HINT_MAX;
  const end = hintSentenceEnd(full, limit);
  if (end > 0) return full.slice(0, end);
  return `${full.slice(0, HINT_MAX - 20).trimEnd()}…`;
}

function hintTextSpan(el) {
  return el.querySelector(":scope > .hint-text");
}

function setHintOpen(el, open) {
  const span = hintTextSpan(el);
  const text = open ? el.dataset.hintFull : hintFirstSentence(el.dataset.hintFull, el);
  el.dataset.hintOpen = open ? "1" : "0";
  el.dataset.hintShown = text;
  if (span) span.textContent = text;
  const button = el.querySelector(":scope > .hint-toggle");
  if (button) button.setAttribute("aria-expanded", open ? "true" : "false");
}

function shortenHint(el) {
  if (el.closest("#inspectorSection") || el.closest("#pageInspectorSection")) {
    presentInspectorHint(el);
    return;
  }
  const span = hintTextSpan(el);
  if (span && el.dataset.hintShown !== undefined && span.textContent === el.dataset.hintShown) return; // our own text
  // Text set from outside replaces our span and button, so what is left is the new full text.
  const full = (span ? span.textContent : el.textContent).trim();
  if (!hintShortenable(el, full)) {
    if (span) el.textContent = full;
    delete el.dataset.hintShown;
    delete el.dataset.hintFull;
    delete el.dataset.hintOpen;
    return;
  }
  const open = el.dataset.hintOpen === "1";
  el.dataset.hintFull = full;
  el.textContent = "";
  const textSpan = document.createElement("span");
  textSpan.className = "hint-text";
  const button = document.createElement("button");
  button.type = "button";
  button.className = "hint-toggle";
  button.textContent = "ℹ️";
  button.title = t("inspector.hint_more");
  button.setAttribute("aria-label", t("inspector.hint_more"));
  button.addEventListener("click", (event) => {
    event.preventDefault();
    event.stopPropagation();
    setHintOpen(el, el.dataset.hintOpen !== "1");
  });
  el.append(textSpan, " ", button);
  setHintOpen(el, open);
}

function inspectorFullHint(el) {
  const span = hintTextSpan(el);
  const full = (el.dataset.hintFull ?? (span ? span.textContent : el.textContent)).trim();
  if (span || el.dataset.hintFull !== undefined) el.textContent = full;
  delete el.dataset.hintFull; delete el.dataset.hintShown; delete el.dataset.hintOpen;
  return full;
}

function inspectorHelpTexts(source) {
  const hints = source.matches(".meta, .field-hint") ? [source]
    : Array.from(source.querySelectorAll(".meta, .field-hint, li"));
  return hints.filter(hint => inspectorNodeShown(hint, source.parentElement))
    .map(inspectorFullHint).filter(Boolean);
}

function inspectorHelpParagraphs(text) {
  // Authored line breaks take precedence. Older single-line hints become readable sentence paragraphs.
  if (text.includes("\n")) return text.split(/\n\s*\n/).map(part => part.trim()).filter(Boolean);
  if (typeof Intl.Segmenter !== "function") return [text];
  const sentences = new Intl.Segmenter(editor.i18nLanguage || "en", { granularity: "sentence" });
  const paragraphs = [];
  for (const { segment } of sentences.segment(text)) {
    const sentence = segment.trim();
    if (!sentence) continue;
    const last = paragraphs.length - 1;
    if (last >= 0 && paragraphs[last].length < HINT_MIN_SENTENCE) paragraphs[last] += ` ${sentence}`;
    else paragraphs.push(sentence);
  }
  return paragraphs;
}

function inspectorHelpTopic() {
  const widget = selectedWidget();
  if (widget) {
    const type = widget.type === "roborock_tile" ? "vacuum_tile" : widget.type;
    if (!WIDGET_NAME_TYPES.has(type)) return null;
    // The popup repeats the guide's summary (tools/check_web.py compares them), so it links to the guide's top.
    return { type, url: "https://andre68723.github.io/BETTA-HA-PANEL/widgets/" + (type === "wallbox_evcc" ? "wallbox" : type) + ".html" };
  }
  const page = selectedPage();
  if (!page) return null;
  const type = isHousePage(page) ? "house_dashboard" : isEnergyPage(page) ? "energy_dashboard" : "blank_page";
  return { type, page: true, url: "https://andre68723.github.io/BETTA-HA-PANEL/pages.html#"
    + { house_dashboard: "room-overview", energy_dashboard: "energy-dashboard", blank_page: "free-pages" }[type] };
}

// A paragraph that starts with a short label ("Visible days: ...") reads as a heading: the label becomes bold.
function inspectorHelpLabelled(text) {
  if (text.startsWith("**")) return text;
  const match = /^([^.:!?\n*(),]{2,48}):\s/.exec(text);
  return match ? `**${match[1]}:**${text.slice(match[1].length + 1)}` : text;
}

// **bold** for labels and option names; known technical identifiers as code. Everything else is plain text.
function inspectorHelpInline(parent, text) {
  text.split(/\*\*([^*]+)\*\*/g).forEach((chunk, index) => {
    if (index % 2) {
      const strong = document.createElement("strong"); strong.textContent = chunk; parent.append(strong);
      return;
    }
    const parts = chunk.split(/(state_class|total_increasing|\btotal\b|detailedForecast|\b[a-z_]+\.\*|sensor\.[\w.<>]+)/g);
    for (let i = 0; i < parts.length; i++) {
      if (!parts[i]) continue;
      const part = document.createElement(i % 2 ? "code" : "span"); part.textContent = parts[i]; parent.append(part);
    }
  });
}

function inspectorHelpSection(dialog, title, texts) {
  const section = document.createElement("section"); section.className = "inspector-help-section";
  if (title) { const heading = document.createElement("h3"); heading.textContent = title; section.append(heading); }
  for (const text of texts.flatMap(inspectorHelpParagraphs)) {
    const heading = /^\*\*([^*\n]+)\*\*$/.exec(text);
    if (heading) {
      const label = document.createElement("h4"); label.textContent = heading[1]; section.append(label);
      continue;
    }
    const paragraph = document.createElement("p");
    inspectorHelpInline(paragraph, inspectorHelpLabelled(text));
    section.append(paragraph);
  }
  dialog.append(section); return section;
}

function openInspectorHelp(title, source, options = {}) {
  const dialog = document.createElement("dialog"); dialog.id = "inspectorHelpDialog";
  dialog.className = "inspector-help-dialog"; dialog.setAttribute("aria-labelledby", "inspectorHelpTitle");
  const head = document.createElement("div"); head.className = "section-head";
  const heading = document.createElement("h2"); heading.id = "inspectorHelpTitle"; heading.textContent = title;
  const close = document.createElement("button"); close.type = "button"; close.className = "btn small";
  close.textContent = t("entity_picker.close"); close.autofocus = true; close.onclick = () => dialog.close();
  head.append(heading, close); dialog.append(head);
  const topic = options.topic ?? inspectorHelpTopic();
  if (options.overview && topic?.type) {
    const descriptionKey = "inspector.help." + (topic.page ? "page_" : "widget_") + topic.type;
    inspectorHelpSection(dialog, t(topic.page ? "inspector.help.page_overview" : "inspector.help.overview"), [t(descriptionKey)]);
  }
  const texts = source ? inspectorHelpTexts(source) : [];
  if (texts.length) inspectorHelpSection(dialog, t("inspector.help.configuration"), texts);
  for (const part of options.sections || []) {
    const partTexts = inspectorHelpTexts(part.source);
    if (partTexts.length) inspectorHelpSection(dialog, part.title, partTexts);
  }
  if (options.textSizes) {
    const section = inspectorHelpSection(dialog, t("inspector.help.text_sizes"), []);
    const values = document.createElement("dl"); values.className = "inspector-help-sizes";
    for (const role of ["meta", "label", "value", "key", "hero"]) {
      const label = document.createElement("dt"), roleKey = "inspector.help.role_" + role; label.textContent = t(roleKey);
      const value = document.createElement("dd"); value.textContent = textRoleSize(role, 0) + " px";
      values.append(label, value);
    }
    section.append(values);
    inspectorHelpSection(dialog, t("inspector.help.text_fit"), [t("inspector.help.text_fit_body")]);
  }
  const url = options.url || topic?.url;
  if (url) {
    const footer = document.createElement("footer"); footer.className = "inspector-help-footer";
    const link = document.createElement("a"); link.href = url; link.target = "_blank"; link.rel = "noopener noreferrer";
    link.textContent = t("inspector.help.documentation") + " ↗"; footer.append(link); dialog.append(footer);
  }
  // Keep Escape and editor shortcuts inside the modal; native dialog restores focus to its opener.
  dialog.addEventListener("keydown", event => event.stopPropagation());
  dialog.addEventListener("click", event => {
    if (event.target !== dialog) return;
    const rect = dialog.getBoundingClientRect();
    if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) dialog.close();
  });
  dialog.addEventListener("close", () => dialog.remove());
  document.body.append(dialog); dialog.showModal();
}

function setInspectorHelpCaption(row, text) {
  const caption = row.querySelector(".inspector-help-caption");
  if (caption.textContent !== text) caption.textContent = text;
  const button = row.querySelector("button"); button.title = text; button.setAttribute("aria-label", text);
}

function createInspectorInfoButton() {
  const button = document.createElement("button"); button.type = "button"; button.className = "inspector-info-btn";
  button.setAttribute("aria-haspopup", "dialog");
  button.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v1"/></svg>';
  return button;
}

function createInspectorHelpRow(source, title) {
  const row = document.createElement("div"); row.className = "inspector-help-row";
  const caption = document.createElement("span"); caption.className = "inspector-help-caption";
  const button = createInspectorInfoButton();
  button.onclick = event => { event.preventDefault(); event.stopPropagation(); openInspectorHelp(caption.textContent, source); };
  row.append(caption, button); source.before(row); source.classList.add("inspector-help-content"); row.append(source);
  setInspectorHelpCaption(row, title); return row;
}

function createInspectorOverviewButton() {
  const button = createInspectorInfoButton(); button.classList.add("inspector-overview-btn");
  button.title = t("inspector.help.about"); button.setAttribute("aria-label", button.title);
  button.onclick = event => {
    event.preventDefault(); event.stopPropagation();
    const widget = selectedWidget();
    openInspectorHelp(widget?.title || selectedPage()?.title || t("inspector.help.about"), null, { overview: true });
  };
  return button;
}

function presentInspectorHint(hint) {
  const full = inspectorFullHint(hint);
  let row = hint.parentElement;
  if (row?.dataset.genericHint !== "true") {
    /* In a widget's sections every explanation goes behind the section's info icon, whatever its length (a
     * .field-hint, or a .meta named ...Hint); status lines and warnings stay where they are. */
    const explanation = hint.closest("#inspectorSection .inspector-sec") !== null &&
      (hint.classList.contains("field-hint") || (hint.classList.contains("meta") && /Hint$/.test(hint.id)));
    if (hint.closest(".inspector-help-content, .info-details") || hint.classList.contains("no-shorten")
        || hint.classList.contains("warn") || (!explanation && full.length <= HINT_MAX)) return;
    row = createInspectorHelpRow(hint, t("inspector.help")); row.dataset.genericHint = "true";
  }
  setInspectorHelpCaption(row, t("inspector.help"));
  const hidden = hint.hidden || hint.classList.contains("hidden") || !full;
  if (row.hidden !== hidden) row.hidden = hidden;
}

// The Inspector and the settings pages share one root: the workspace body.
function hintRoot() {
  return document.querySelector(".workspace-body");
}

function shortenInspectorHints() {
  const root = hintRoot();
  if (!root) return;
  root.querySelectorAll(".field-hint, .meta").forEach((hint) => {
    if (!hint.closest("#canvas")) shortenHint(hint);
  });
  for (const row of root.querySelectorAll(".inspector-help-row")) {
    setInspectorHelpCaption(row, row.querySelector(".inspector-help-caption").textContent);
  }
  for (const button of root.querySelectorAll(".inspector-overview-btn")) {
    const hidden = !inspectorHelpTopic();
    if (button.hidden !== hidden) button.hidden = hidden;
    button.title = t(inspectorHelpTopic()?.page ? "inspector.help.about_page" : "inspector.help.about"); button.setAttribute("aria-label", button.title);
  }
  // Hint wrapping also runs after layout refresh, so update the section icons in the same pass.
  refreshInspectorSectionHelp();
}

function bindInspectorHints() {
  const root = hintRoot();
  if (!root || typeof MutationObserver === "undefined") return;
  for (const id of ["inspectorHeading", "pageInspectorHeading"]) {
    const heading = document.getElementById(id);
    if (heading && !heading.parentElement.querySelector(".inspector-overview-btn")) {
      const title = document.createElement("div"); title.className = "inspector-title-with-help";
      heading.before(title); title.append(heading, createInspectorOverviewButton());
    }
  }
  const textSizeInfo = document.getElementById("layoutTextSizeInfoBtn");
  if (textSizeInfo) textSizeInfo.onclick = event => {
    event.preventDefault(); event.stopPropagation();
    openInspectorHelp(t("settings.ui.text_step_details"), null, { textSizes: true, topic: {}, url:
      "https://andre68723.github.io/BETTA-HA-PANEL/appearance.html#text-size" });
  };
  for (const info of root.querySelectorAll("#pageInspectorSection details.info-details")) {
    const summary = info.querySelector("summary"), source = document.createElement("div");
    source.append(...Array.from(info.children).filter(child => child !== summary));
    const row = createInspectorHelpRow(source, summary.textContent); row.id = info.id;
    row.querySelector(".inspector-help-caption").id = summary.id; info.replaceWith(row);
  }
  const options = { subtree: true, childList: true, characterData: true, attributes: true, attributeFilter: ["hidden", "class"] };
  const observer = new MutationObserver((records) => {
    // The canvas is rebuilt on every edit and holds no hints: its mutations are not worth a scan.
    const outside = records.some((r) => !(r.target.nodeType === 1 ? r.target : r.target.parentElement)?.closest("#canvas"));
    if (outside) {
      // Presentation changes must not enqueue another scan of our own newly written hints.
      observer.disconnect();
      try { shortenInspectorHints(); } finally { observer.observe(root, options); }
    }
  });
  observer.observe(root, options);
  shortenInspectorHints();
}
