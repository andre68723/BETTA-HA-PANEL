/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
function localEditorShowError(error) {
  window.localEditorStartupError = error;
  const toolbar = document.getElementById('localEditorToolbar');
  if (toolbar) {
    let message = document.getElementById('localEditorError');
    if (!message) { message = document.createElement('span'); message.id = 'localEditorError'; toolbar.append(message); }
    message.textContent = ' · Editor-Fehler: ' + String(error?.message || error);
  }
}
window.addEventListener('unhandledrejection', event => localEditorShowError(event.reason));
window.addEventListener('error', event => localEditorShowError(event.error || new Error(event.message)));
const localEditorOriginalStart = startEditor;
startEditor = async function (...args) {
  try {
    const value = await localEditorOriginalStart(...args);
    window.localEditorStartupComplete = true;
    return value;
  } catch (error) {
    window.localEditorStartupError = error;
    throw error;
  }
};
(async function localEditorToolbar() {
  const config = await (await fetch('/__local__/config.json')).json();
  const active = location.pathname === '/classic.html' ? 'classic' : new URLSearchParams(location.search).get('ui') || config.ui;
  const toolbar = document.createElement('div');
  toolbar.id = 'localEditorToolbar';
  toolbar.className = 'local-editor-bar';
  toolbar.innerHTML = '<strong>Lokaler Editor</strong><span>Beispieldaten · Speichern nur lokal</span><label>Ansicht <select id="localEditorView" aria-label="Editor-Ansicht"><option value="classic">Klassisch</option></select></label>';
  document.body.prepend(toolbar);
  const select = document.getElementById('localEditorView');
  if (config.spike) select.add(new Option('Spike', 'spike'));
  select.value = active;
  select.disabled = !config.spike;
  const key = 'betta-local-editor-view-transfer';
  while (!window.localEditorStartupComplete || !editor.layout) {
    if (window.localEditorStartupError) {
      toolbar.append(document.createTextNode(" · Editor konnte nicht starten: " + window.localEditorStartupError.message));
      return;
    }
    await new Promise(resolve => setTimeout(resolve, 80));
  }
  try {
    const raw = sessionStorage.getItem(key);
    if (raw) {
      sessionStorage.removeItem(key);
      const draft = JSON.parse(raw);
      if (draft.variant === config.variant && Date.now() - draft.time < 300000 && draft.layout?.pages?.length) {
        editor.layout = draft.layout;
        editor.selectedPageId = draft.page;
        editor.selectedWidgetId = draft.widget;
        renderAll();
      }
    }
  } catch (error) { console.warn('Local view transfer unavailable:', error.message); }
  select.onchange = () => {
    try {
      sessionStorage.setItem(key, JSON.stringify({variant:config.variant,time:Date.now(),layout:editor.layout,
                                                page:editor.selectedPageId,widget:editor.selectedWidgetId}));
    } catch (error) {
      if (editor.dirty) { select.value = active; alert('Der Ansichtswechsel konnte die Änderungen nicht zwischenspeichern. Bitte zuerst lokal speichern oder exportieren.'); return; }
    }
    location.assign('/?ui=' + encodeURIComponent(select.value));
  };
  addLvglPreview(toolbar, config);
  window.localEditorReady = true;
})();

// LVGL preview: the page as the firmware's simulator draws it, for the layout as it is in the editor right now
// (saved or not). The local server renders it with tools/editor_local.py (render_lvgl) and sends a BMP.
function addLvglPreview(toolbar, config) {
  const button = document.createElement('button');
  button.type = 'button';
  button.id = 'localEditorLvgl';
  button.textContent = 'LVGL-Vorschau';
  toolbar.insertBefore(button, toolbar.querySelector('label'));
  const dialog = document.createElement('div');
  dialog.id = 'localEditorLvglDialog';
  dialog.className = 'local-editor-lvgl hidden';
  dialog.innerHTML = '<div class="local-editor-lvgl-card"><div class="local-editor-lvgl-head"><strong>LVGL-Vorschau</strong>' +
    '<label>Stil <select id="localEditorLvglStyle"><option>glow</option><option>flat</option><option>glass</option><option>matte</option></select></label>' +
    '<label>Textgröße <select id="localEditorLvglStep"><option value="0">0</option><option value="1">1</option><option value="2">2</option></select></label>' +
    '<label>Sprache <select id="localEditorLvglLang"><option value="de">de</option><option value="en">en</option></select></label>' +
    '<button type="button" id="localEditorLvglRefresh">Neu zeichnen</button><button type="button" id="localEditorLvglClose">Schließen</button>' +
    '<span id="localEditorLvglStatus"></span></div><div class="local-editor-lvgl-body"><img id="localEditorLvglImage" alt="LVGL-Vorschau der Seite"></div></div>';
  document.body.append(dialog);
  const status = document.getElementById('localEditorLvglStatus');
  const image = document.getElementById('localEditorLvglImage');
  let url = null;
  async function render() {
    status.textContent = 'Zeichne ... (der erste Lauf baut den Simulator)';
    try {
      const response = await fetch('/api/local/simulator', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({layout: editor.layout, page_id: editor.selectedPageId,
          style: document.getElementById('localEditorLvglStyle').value,
          text_step: Number(document.getElementById('localEditorLvglStep').value),
          language: document.getElementById('localEditorLvglLang').value})
      });
      if (!response.ok) throw new Error((await response.json()).error || response.statusText);
      const blob = await response.blob();
      if (url) URL.revokeObjectURL(url);
      url = URL.createObjectURL(blob);
      image.src = url;
      status.textContent = 'Seite „' + (editor.layout.pages.find(page => page.id === editor.selectedPageId)?.title || editor.selectedPageId) + '“, ' + config.variant;
    } catch (error) { status.textContent = 'Fehler: ' + error.message; }
  }
  button.onclick = () => { dialog.classList.remove('hidden'); render(); };
  document.getElementById('localEditorLvglRefresh').onclick = render;
  for (const id of ['localEditorLvglStyle', 'localEditorLvglStep', 'localEditorLvglLang']) document.getElementById(id).onchange = render;
  document.getElementById('localEditorLvglClose').onclick = () => dialog.classList.add('hidden');
  dialog.addEventListener('click', event => { if (event.target === dialog) dialog.classList.add('hidden'); });
}
