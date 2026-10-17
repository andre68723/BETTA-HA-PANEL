// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723 -- see docs/release-notes.md
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { loadEditor } from './load_editor.mjs';

// Presentation is stubbed; real bindUi, model handlers, normalization, requests
// and history run together. Safari covers rendering and native pointer events.
function setup() {
  const { run } = loadEditor();
  run(`
    globalThis.nodes = {}; globalThis.docEvents = {};
    globalThis.makeNode = (id = '', tagName = 'BUTTON') => {
      const events = {}, classes = new Set(['hidden']);
      return { id, tagName, type: 'text', value: '', checked: false, style: {}, dataset: {},
        isConnected: true, children: [], options: [], events,
        classList: { add: c => classes.add(c), remove: c => classes.delete(c),
          contains: c => classes.has(c), toggle(c, on) { on ? classes.add(c) : classes.delete(c); } },
        setAttribute(k, v) { this[k] = v; }, getAttribute(k) { return this[k]; },
        addEventListener(k, fn) { (events[k] ||= []).push(fn); },
        removeEventListener(k, fn) { events[k] = (events[k] || []).filter(v => v !== fn); },
        dispatch(k, extras = {}) { const ev = { target: this, preventDefault() { this.defaultPrevented = true; },
          stopPropagation() {}, ...extras }; for (const fn of events[k] || []) fn(ev); this['on'+k]?.(ev); return ev; },
        click() { this.dispatch('click'); }, focus() { document.activeElement = this; },
        appendChild(n) { this.children.push(n); if (n.tagName === 'OPTION') {
          this.options.push(n); if (this.options.length === 1) this.value = n.value; } },
        replaceChildren() { this.children = []; this.options = []; this.value = ''; },
        getBoundingClientRect() { return { left: 20, bottom: 40 }; },
        querySelector(sel) { return this.querySelectorAll(sel)[0] || null; },
        querySelectorAll() { return this.children.filter(n => !n.disabled); },
        closest() { return null; }, setPointerCapture() {}, releasePointerCapture() {}
      };
    };
    for (const key of Object.keys(el)) if (!Array.isArray(el[key])) el[key] = makeNode(key);
    for (const id of ['layoutUndoBtn', 'layoutRedoBtn', 'widgetActionsMenu', 'widgetTransferDialog',
      'widgetTransferTitle', 'widgetTransferName', 'widgetTransferPageLabel', 'widgetTransferPage',
      'widgetTransferCancel', 'widgetTransferConfirm', 'widgetTransferError']) nodes[id] = makeNode(id);
    nodes.widgetTransferPage.tagName = 'SELECT';
    nodes.widgetTransferDialog.children = [nodes.widgetTransferPage, nodes.widgetTransferCancel, nodes.widgetTransferConfirm];
    document.getElementById = id => nodes[id] || el[id] || null;
    document.createElement = tag => makeNode('', tag.toUpperCase());
    document.addEventListener = (name, fn) => (docEvents[name] ||= []).push(fn);
    globalThis.dispatchDoc = (name, target, extra = {}) => {
      const ev = { target, key: '', preventDefault() { this.defaultPrevented = true; }, ...extra };
      for (const fn of docEvents[name] || []) fn(ev); return ev;
    };
    document.querySelector = sel => sel.includes('.modal-overlay:not(.hidden)') &&
      !nodes.widgetTransferDialog.classList.contains('hidden') ? nodes.widgetTransferDialog : null;
    window.innerWidth = 1280; window.innerHeight = 800;
    window.confirm = () => true; window.setTimeout = () => 0;
    globalThis.setInterval = () => 0;
    globalThis.Blob = class { constructor(parts) { this.size = layoutJsonBytes(parts.join('')); } };
    for (const name of ['setupSettingsWorkspace', 'applySectionCollapseState', 'initSimpleUiMenus',
      'bindAddTileDialog', 'bindUpdateCheck', 'bindCrashBanner', 'bindInspectorHints',
      'refreshBrightnessNow', 'refreshCameraBrightnessStatus', 'bindHardwareLive', 'bindBrightnessTests',
      'initEditorTheme', 'bindSecurityControls', 'bindStatusControls', 'loadHaDiagnostics',
      'renderPages', 'renderPageEditor', 'renderWidgets', 'renderInspector', 'renderCanvas',
      'updateCanvasScale', 'refreshInspectorLayout']) globalThis[name] = () => {};
    bindUi();
    editor.activePane = 'layout';
    editor.layout = { version: 1, pages: [
      { id: 'one', title: 'First', widgets: [{ id: 'energy', type: 'energy_timeline', title: 'Energy',
        entity_id: 'sensor.import', secondary_entity_id: 'sensor.export', tertiary_entity_id: 'sensor.solar',
        battery_charge_entity_id: 'sensor.charge', battery_discharge_entity_id: 'sensor.discharge',
        battery_soc_entity_id: 'sensor.soc', forecast_today_entity_id: 'sensor.forecast',
        energy_colors: {grid: '#102030', solar: '#406080'}, rect: { x: 20, y: 20, w: 420, h: 260 } }] },
      { id: 'two', title: 'Second', widgets: [] },
      { id: 'energy_page', type: ENERGY_PAGE_TYPE, energy: { source: 'ha' }, widgets: [] }
    ] };
    editor.selectedPageId = 'one'; editor.selectedWidgetId = 'energy';
    renderAll(); clearDirty(); layoutHistoryReset();
    globalThis.savedLayout = null;
    globalThis.fetch = async (_url, options) => {
      if (options?.method === 'PUT') { savedLayout = JSON.parse(options.body); return { ok: true }; }
      return { ok: true, json: async () => JSON.parse(JSON.stringify(savedLayout || editor.layout)) };
    };
    globalThis.baseline = layoutSnapshot();
  `);
  return run;
}
const state = run => JSON.parse(run('layoutSnapshot()'));

test('failed HTTP reload retains edits, selection and undo; successful reload resets them', async () => {
  const run = setup();
  run(`selectedWidget().title = 'Edited'; renderAll(); globalThis.before = layoutSnapshot();
    fetch = async () => ({ok:false,status:503,statusText:'Unavailable'}); el.reloadBtn.click();`);
  await Promise.resolve(); await Promise.resolve();
  assert.equal(run('layoutSnapshot()'), run('before'));
  assert.equal(run('editor.selectedWidgetId'), 'energy');
  assert.equal(run('editor.dirty'), true);
  assert.equal(run('layoutHistory.cursor'), 1);
  assert.match(run('el.status.textContent'), /failed.*retained/i);
  run('nodes.layoutUndoBtn.click()');
  assert.equal(run('layoutSnapshot()'), run('baseline'));
  assert.equal(run('editor.dirty'), false);
  run(`fetch = async () => ({ok:true,json:async()=>JSON.parse(baseline)});`);
  assert.equal(await run('loadLayout()'), true);
  assert.equal(run('layoutHistory.states.length'), 1);
  assert.equal(run('editor.dirty'), false);
});

test('invalid response and interrupted installation cannot replace the current model', async () => {
  const run = setup();
  run(`fetch = async () => ({ok:true,json:async()=>({pages:[{id:'bad',widgets:[{id:'x'}]}]})});`);
  assert.equal(await run('loadLayout()'), false);
  assert.equal(run('layoutSnapshot()'), run('baseline'));
  run(`fetch = async () => ({ok:true,json:async()=>({pages:[]})});`);
  assert.equal(await run('loadLayout()'), false);
  assert.equal(run('layoutSnapshot()'), run('baseline'));
  run(`fetch = async () => ({ok:true,json:async()=>({pages:[{id:'new',widgets:[]}],canvas_w:720,canvas_h:600})});
    globalThis.oldRender = renderAll; renderAll = () => { throw new Error('render failure'); };`);
  assert.equal(await run('loadLayout()'), false);
  assert.equal(run('layoutSnapshot()'), run('baseline'));
  assert.equal(run('CANVAS_WIDTH'), 1280);
  assert.equal(run('layoutHistory.cursor'), 0);
});

test('late reload responses cannot overwrite intervening edits or a newer reload', async () => {
  const run = setup();
  run(`globalThis.responses = []; fetch = () => new Promise(resolve => responses.push(resolve));
    globalThis.loading = loadLayout(); selectedWidget().title = 'While loading'; renderAll();
    responses[0]({ok:true,json:async()=>JSON.parse(baseline)});`);
  assert.equal(await run('loading'), false);
  assert.equal(run('selectedWidget().title'), 'While loading');
  run(`globalThis.first = loadLayout(); globalThis.second = loadLayout();
    responses[2]({ok:true,json:async()=>JSON.parse(baseline)});`);
  assert.equal(await run('second'), true);
  run(`responses[1]({ok:false,status:500,statusText:'late'});`);
  assert.equal(await run('first'), false);
  assert.equal(run('el.status.textContent'), run('t("layout.status.loaded")'));
});

test('startup failure offers an empty editable canvas with the failure still visible', async () => {
  const run = setup();
  run(`editor.layout = null; fetch = async () => { throw new Error('offline'); };`);
  assert.equal(await run('loadLayout()'), false);
  assert.equal(state(run).pages.length, 1);
  assert.match(run('el.status.textContent'), /offline/);
});

test('save failure preserves history; edits made during save remain unsaved', async () => {
  const run = setup();
  run(`selectedWidget().title = 'First edit'; renderAll();
    fetch = async () => ({ok:false,text:async()=> 'offline'});`);
  await run('el.saveBtn.onclick()');
  assert.equal(run('editor.dirty'), true);
  assert.equal(run('layoutHistory.cursor'), 1);
  run(`fetch = async (_url, options) => { savedLayout = JSON.parse(options.body);
    selectedWidget().title = 'Second edit'; renderAll(); return {ok:true}; };`);
  await run('el.saveBtn.onclick()');
  assert.equal(run('savedLayout.pages[0].widgets[0].title'), 'First edit');
  assert.equal(run('editor.dirty'), true);
  run('nodes.layoutUndoBtn.click()');
  assert.equal(run('selectedWidget().title'), 'First edit');
  assert.equal(run('editor.dirty'), false);
  run('nodes.layoutRedoBtn.click()');
  assert.equal(run('selectedWidget().title'), 'Second edit');
  assert.equal(run('editor.dirty'), true);
});

test('Inspector typing coalesces until focus changes; field undo and modal shortcuts stay native', async () => {
  const run = setup();
  run(`editor.layout.pages[0].widgets[0] = {id:'sensor',type:'sensor',title:'Before',entity_id:'sensor.temp',
    rect:{x:20,y:20,w:300,h:160}}; editor.selectedWidgetId='sensor'; renderAll(); clearDirty(); layoutHistoryReset();
    el.fType.value='sensor'; el.fEntity.value='sensor.temp'; el.fTitle.tagName='INPUT';
    el.fX.value='20';el.fY.value='20';el.fW.value='300';el.fH.value='160'; el.fShowName.checked=true;
    dispatchDoc('focusin', el.fTitle);
    el.fTitle.value='A'; el.fTitle.dispatch('input'); el.fTitle.value='AB';el.fTitle.dispatch('input');
    el.fTitle.value='ABC';el.fTitle.dispatch('input');`);
  assert.equal(run('selectedWidget().title'), 'ABC');
  assert.equal(run('layoutHistory.cursor'), 1);
  assert.equal(run(`dispatchDoc('keydown', el.fTitle, {key:'z',metaKey:true}).defaultPrevented`), undefined);
  run(`dispatchDoc('focusout', el.fTitle); el.fEntity.tagName='INPUT'; dispatchDoc('focusin',el.fEntity);`);
  await Promise.resolve();
  assert.equal(run('layoutHistory.group === el.fEntity'), true);
  run(`dispatchDoc('focusout',el.fEntity);`); await Promise.resolve();
  run(`dispatchDoc('keydown', nodes.layoutUndoBtn, {key:'z',metaKey:true});`);
  assert.equal(run('selectedWidget().title'), 'Before');
  run(`dispatchDoc('keydown', nodes.layoutRedoBtn, {key:'z',metaKey:true,shiftKey:true});`);
  assert.equal(run('selectedWidget().title'), 'ABC');
  run(`openWidgetTransfer('one','sensor','copy');dispatchDoc('keydown',nodes.widgetTransferPage,{key:'z',metaKey:true});`);
  assert.equal(run('selectedWidget().title'), 'ABC');
  run(`dispatchDoc('keydown',nodes.widgetTransferPage,{key:'Escape'}); editor.activePane='settings';
    dispatchDoc('keydown',nodes.layoutUndoBtn,{key:'z',ctrlKey:true});`);
  assert.equal(run('selectedWidget().title'), 'ABC');
});

test('a complete pointer drag is one undo step, including cancellation', () => {
  const run = setup();
  run(`globalThis.box = makeNode('box'); globalThis.handle = makeNode('handle');
    box.querySelector = () => handle; selectWidgetLive = () => {}; attachDragAndResize(box, selectedWidget());
    box.dispatch('pointerdown',{button:0,pointerId:1,clientX:20,clientY:20});
    box.dispatch('pointermove',{pointerId:1,clientX:70,clientY:30});
    box.dispatch('pointermove',{pointerId:1,clientX:100,clientY:60});
    box.dispatch('pointercancel',{pointerId:1});`);
  assert.equal(run('layoutHistory.cursor'), 1);
  assert.equal(run('selectedWidget().rect.x'), 100);
  run('nodes.layoutUndoBtn.click()');
  assert.equal(run('selectedWidget().rect.x'), 20);
  run('nodes.layoutRedoBtn.click()');
  assert.equal(run('selectedWidget().rect.x'), 100);
});

for (const mode of ['copy','move']) test(`${mode} menu and target confirmation preserve all bindings through save/reload and undo`, async () => {
  const run = setup();
  run(`globalThis.original = JSON.stringify(selectedWidget()); globalThis.anchor = makeNode('anchor');
    openWidgetActions('one','energy',anchor); nodes.widgetActionsMenu.children[${mode === 'copy' ? 1 : 2}].click();`);
  assert.equal(run('nodes.widgetTransferPage.options.length'), 1); // Energy page is excluded.
  run(`nodes.widgetTransferPage.value='two';nodes.widgetTransferConfirm.click();`);
  assert.equal(run('editor.selectedPageId'), 'two');
  const copied = state(run).pages[1].widgets[0];
  const original = JSON.parse(run('original'));
  assert.deepEqual({...copied, id: original.id, rect: original.rect}, original);
  assert.equal(copied.entity_id, original.entity_id);
  assert.equal(copied.battery_charge_entity_id, original.battery_charge_entity_id);
  assert.equal(copied.forecast_today_entity_id, original.forecast_today_entity_id);
  assert.deepEqual(copied.energy_colors, original.energy_colors);
  assert.equal(copied.id === original.id, mode === 'move');
  assert.equal(state(run).pages[0].widgets.length, mode === 'copy' ? 1 : 0);
  assert.equal(run('layoutHistory.cursor'), 1);
  await run('el.saveBtn.onclick()');
  assert.deepEqual(JSON.parse(run('JSON.stringify(savedLayout)')), state(run));
  run('nodes.layoutUndoBtn.click()');
  assert.equal(run('layoutSnapshot()'), run('baseline'));
  assert.equal(run('editor.dirty'), true);
  run('nodes.layoutRedoBtn.click()');
  assert.equal(run('editor.dirty'), false);
  assert.equal(await run('loadLayout()'), true);
  assert.deepEqual(state(run).pages[1].widgets[0], copied);
});

test('transfer refusal is atomic for full, special, missing destinations and stored UTF-8 budget', () => {
  const run = setup();
  for (const target of ['one', 'energy_page', 'missing']) {
    assert.throws(() => run(`transferWidget('one','energy','${target}','move')`));
    assert.equal(run('layoutSnapshot()'), run('baseline'));
  }
  run(`editor.layout.pages[1].widgets = Array.from({length:32},(_,i)=>({id:'s'+i,type:'sensor',entity_id:'sensor.x',rect:{x:0,y:0,w:300,h:160}}));
    renderAll(); globalThis.full = layoutSnapshot();`);
  assert.throws(() => run(`transferWidget('one','energy','two','move')`));
  assert.equal(run('layoutSnapshot()'), run('full'));
  run(`editor.layout.pages[1].widgets=[]; selectedWidget().title='ü'.repeat(17000); renderAll();globalThis.large=layoutSnapshot();`);
  assert.throws(() => run(`transferWidget('one','energy','two','copy')`));
  assert.equal(run('layoutSnapshot()'), run('large'));
});

test('portrait transfers keep the landscape alias and update the visible portrait position', () => {
  const run = setup();
  run(`editor.layout.orientation='portrait';selectedWidget().landscape_rect={x:10,y:10,w:420,h:260};
    globalThis.result=transferWidget('one','energy','two','copy');`);
  assert.deepEqual(JSON.parse(run('JSON.stringify(result.portrait_rect)')), JSON.parse(run('JSON.stringify(result.rect)')));
  assert.equal(run('result.landscape_rect.x'), 10);
  assert.ok(run('layoutStoredBytes(editor.layout)') < run('layoutJsonBytes(layoutSnapshot())'));
});

test('menu keyboard escape and dialog cancellation restore the original trigger focus', () => {
  const run = setup();
  run(`globalThis.anchor=makeNode('anchor');openWidgetActions('one','energy',anchor);
    dispatchDoc('keydown',document.activeElement,{key:'Escape'});`);
  assert.equal(run('document.activeElement === anchor'), true);
  run(`openWidgetActions('one','energy',anchor);nodes.widgetActionsMenu.children[1].click();nodes.widgetTransferCancel.click();`);
  assert.equal(run('document.activeElement === anchor'), true);
  assert.equal(run('anchor["aria-expanded"]'), 'false');
  run(`openWidgetActions('one','energy',anchor);openWidgetActions('one','energy',anchor);`);
  assert.equal(run('widgetActions'), null);
});

test('import is undoable, but invalid duplicate IDs leave edits intact', () => {
  const run = setup();
  run(`importLayoutFromText(JSON.stringify({pages:[{id:'new',widgets:[]}]}));`);
  assert.equal(run('layoutHistory.cursor'), 1);
  run('nodes.layoutUndoBtn.click()');
  assert.equal(run('layoutSnapshot()'), run('baseline'));
  assert.throws(() => run(`importLayoutFromText(JSON.stringify({pages:[{id:'x',widgets:[]},{id:'x',widgets:[]}]}))`));
  assert.equal(run('layoutSnapshot()'), run('baseline'));
});

test('new edits clear redo; selection and live states do not consume history; history is bounded', () => {
  const run = setup();
  run(`editor.selectedPageId='two';editor.selectedWidgetId=null;editor.states.set('sensor.import','12');renderAll();`);
  assert.equal(run('layoutHistory.cursor'), 0);
  run(`editor.selectedPageId='one';editor.selectedWidgetId='energy';
    for(let i=0;i<70;i++){ selectedWidget().title='edit'+i;renderAll(); }
    nodes.layoutUndoBtn.click(); selectedWidget().title='branch'; renderAll();`);
  assert.equal(run('layoutHistory.states.length'), 51);
  assert.equal(run('nodes.layoutRedoBtn.disabled'), true);
  assert.ok(run('layoutHistory.bytes') <= 2 * 1024 * 1024);
});

test('delete toast undo restores options and expires on a later edit', () => {
  const run = setup();
  run('deleteWidget();el.undoToastBtn.onclick()');
  assert.equal(run('layoutSnapshot()'), run('baseline'));
  run(`deleteWidget();editor.layout.pages[1].title='Renamed';renderAll();`);
  assert.equal(run('pendingUndo'), null);
  run('nodes.layoutUndoBtn.click();nodes.layoutUndoBtn.click()');
  assert.equal(run('layoutSnapshot()'), run('baseline'));
});

test('224-widget ceiling allows moving but rejects copying without losing either page', () => {
  const run = setup();
  run(`editor.layout.pages=Array.from({length:8},(_,p)=>({id:'p'+p,widgets:p<7?
    Array.from({length:32},(_,w)=>({id:'w'+(p*32+w),type:'empty_tile',rect:{x:0,y:0,w:160,h:160}})):[]}));
    editor.selectedPageId='p0';editor.selectedWidgetId='w0';renderAll();clearDirty();layoutHistoryReset();
    globalThis.maximal=layoutSnapshot();`);
  assert.throws(() => run(`transferWidget('p0','w0','p7','copy')`), /224/);
  assert.equal(run('layoutSnapshot()'), run('maximal'));
  run(`transferWidget('p0','w0','p7','move');`);
  assert.equal(run('editor.layout.pages[0].widgets.length'), 31);
  assert.equal(run('editor.layout.pages[7].widgets[0].id'), 'w0');
  run('nodes.layoutUndoBtn.click()');
  assert.equal(run('layoutSnapshot()'), run('maximal'));
});

test('large portrait snapshots evict old history within the byte budget; generated IDs fit firmware', () => {
  const run = setup();
  run(`selectedWidget().title='ü'.repeat(30000);renderAll();layoutHistoryReset();
    for(let i=0;i<50;i++){selectedWidget().rect.x=i;renderAll();}
    editor.layout.pages[1].id='destination_page_with_long_name';`);
  assert.ok(run('layoutHistory.states.length') < 51);
  assert.ok(run('layoutHistory.bytes') <= 2 * 1024 * 1024);
  assert.ok(run(`layoutJsonBytes(createWidgetIdForPage(editor.layout.pages[1],'energy_timeline'))`) < 32);
});
