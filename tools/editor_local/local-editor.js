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
  window.localEditorReady = true;
})();
