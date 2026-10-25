// SPDX-License-Identifier: LicenseRef-FNCL-1.1
// Copyright (c) 2026 andre68723
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { loadEditor } from "./load_editor.mjs";

function setup() {
  const { run } = loadEditor();
  run(`
    function node(tag) {
      const n = { tagName: tag.toUpperCase(), className: "", id: "", dataset: {}, attributes: {},
        children: [], listeners: {}, hidden: false, parentElement: null, _text: "",
        setAttribute(k,v) { this.attributes[k]=v; },
        matches(selector) { return selector.split(",").some(s => {
          s=s.trim().replace(":scope > ", "");
          return s.startsWith(".") ? this.classList.contains(s.slice(1))
            : s.startsWith("#") ? this.id===s.slice(1) : tag===s;
        }); },
        closest(selector) { return this.matches(selector) ? this : this.parentElement?.closest(selector) || null; },
        querySelectorAll(selector) { return this.children.flatMap(c => [ ...(c.matches(selector) ? [c] : []), ...c.querySelectorAll(selector)]); },
        querySelector(selector) { return this.querySelectorAll(selector)[0] || null; },
        append(...children) { for (const child of children) { child.remove(); child.parentElement=this; this.children.push(child); } },
        before(other) { if(this.parentElement) { const p=this.parentElement; p.children.splice(p.children.indexOf(this),0,other); other.parentElement=p; } },
        remove() { if(this.parentElement) { const p=this.parentElement; p.children=p.children.filter(c=>c!==this); this.parentElement=null; } },
        addEventListener(type,fn) { this.listeners[type]=fn; },
        showModal() { this.open=true; }, close() { this.open=false; this.listeners.close?.(); },
        getBoundingClientRect() { return {left:100,right:400,top:100,bottom:400}; }
      };
      n.classList={contains:c=>n.className.split(" ").includes(c),add:c=>{if(!n.classList.contains(c))n.className+=(n.className?" ":"")+c;}};
      Object.defineProperty(n,"textContent",{get(){return this._text+this.children.map(c=>c.textContent).join("");},set(v){this._text=v;this.children=[];}});
      return n;
    }
    document.createElement=node; document.body=node("body");
    globalThis.root=node("section"); root.id="inspectorSection"; document.body.append(root);
    globalThis.hint=node("div"); hint.className="meta"; root.append(hint);
    globalThis.full="Counters need total or total_increasing for long-term statistics. Battery charge and discharge go together. The forecast needs the complete detailedForecast attribute.";
    hint.textContent=full;
    globalThis.click={preventDefault(){},stopPropagation(){}};
  `);
  return run;
}

for (const open of ["0", "1"]) {
  test(`Inspector help opens the complete legacy text directly in a popup (${open})`, () => {
    const run = setup();
    run(`hint.dataset={hintFull:full,hintShown:"Counters need…",hintOpen:"${open}"};
      hint.textContent="Counters need… ℹ️"; shortenHint(hint); shortenHint(hint);
      globalThis.row=hint.parentElement; row.querySelector("button").onclick(click);
      globalThis.dialog=document.body.querySelector("dialog");`);
    assert.equal(run("row.tagName"), "DIV");
    assert.equal(run("row.children.filter(c=>c.tagName==='BUTTON').length"), 1);
    assert.equal(run("row.querySelector('button').attributes['aria-haspopup']"), "dialog");
    assert.equal(run("dialog.open"), true);
    assert.equal(run("dialog.querySelectorAll('p').map(p=>p.textContent).join(' ')"), run("full"));
    assert.ok(run("dialog.querySelectorAll('p').length") > 1);
    assert.equal(run("Object.keys(hint.dataset).length"), 0);
    assert.equal(run("row.querySelector('summary')"), null);
    run(`globalThis.stopped=false; dialog.listeners.keydown({key:"Escape",stopPropagation(){stopped=true;}}); dialog.close();`);
    assert.equal(run("stopped"), true);
    assert.equal(run("document.body.querySelector('dialog')"), null);
  });
}

test("Popup help follows hidden fields and changed explanations", () => {
  const run = setup();
  run(`shortenHint(hint); globalThis.row=hint.parentElement;
    hint.hidden=true; shortenHint(hint);`);
  assert.equal(run("row.hidden"), true);
  run(`hint.hidden=false; hint.textContent=full+" <img src=x onerror=alert(1)>"; shortenHint(hint);
    row.querySelector("button").onclick(click); globalThis.dialog=document.body.querySelector("dialog");`);
  assert.equal(run("row.hidden"), false);
  assert.equal(run("dialog.querySelectorAll('p').map(p=>p.textContent).join(' ')"), run("hint.textContent"));
  assert.equal(run("dialog.querySelector('img')"), null);
  run(`dialog.listeners.click({target:dialog,clientX:0,clientY:0});`);
  assert.equal(run("document.body.querySelector('dialog')"), null);
});

test("Grouped help has one popup with complete paragraphs and no nested info buttons", () => {
  const run = setup();
  run(`globalThis.source=node("div"); source.append(hint);
    globalThis.other=node("div"); other.className="meta"; other.textContent="Second complete explanation."; source.append(other);
    globalThis.hidden=node("div"); hidden.className="meta hidden"; hidden.textContent="Inactive source."; source.append(hidden);
    globalThis.row=createInspectorHelpRow(source,"Counter requirements"); root.append(row);
    shortenHint(hint); shortenHint(other); row.querySelector("button").onclick(click);
    globalThis.dialog=document.body.querySelector("dialog");`);
  assert.equal(run("row.querySelectorAll('button').length"), 1);
  assert.equal(run("dialog.querySelectorAll('p').map(p=>p.textContent).join(' ')"), run("full") + " Second complete explanation.");
});

test("help excludes explanations inside a hidden optional group and includes them after enabling it", () => {
  const run = setup();
  run(`globalThis.source=node('div'); source.append(hint);
    globalThis.optional=node('div'); optional.className='hidden';
    globalThis.extra=node('div'); extra.className='meta'; extra.textContent='Optional enabled explanation.';
    optional.append(extra); source.append(optional);
    openInspectorHelp('Options',source,{topic:{}}); globalThis.dialog=document.body.querySelector('dialog');`);
  assert.equal(run("dialog.textContent.includes('Optional enabled explanation.')"), false);
  run(`dialog.close(); optional.className=''; openInspectorHelp('Options',source,{topic:{}});
    globalThis.dialog=document.body.querySelector('dialog');`);
  assert.equal(run("dialog.textContent.includes('Optional enabled explanation.')"), true);
});

test("wallbox height warning remains directly visible rather than moving into a help popup", () => {
  const run = setup();
  run(`hint.id='evccHeaderHint'; hint.className='meta warn no-shorten';
    hint.textContent=t('evcc.header_size',{h:440,current:410}); shortenHint(hint);`);
  assert.equal(run("hint.parentElement"), run("root"));
  assert.match(run("hint.textContent"), /410/);
  assert.equal(run("root.querySelector('button')"), null);
});

test("room extra explanation opens in help while missing-entity warnings stay visible", () => {
  const run = setup();
  run(`const originalClosest=hint.closest.bind(hint);
    hint.closest=selector=>selector==='#inspectorSection .inspector-sec' ? root : originalClosest(selector);
    hint.id='fRoomExtraHint'; hint.className='meta';
    hint.textContent=t('layout.inspector.room_extra_hint'); shortenHint(hint);
    globalThis.helpRow=hint.parentElement; helpRow.querySelector('button').onclick(click);
    globalThis.dialog=document.body.querySelector('dialog');`);
  assert.equal(run("helpRow.dataset.genericHint"), "true");
  assert.equal(run("hint.classList.contains('inspector-help-content')"), true);
  assert.equal(run("dialog.textContent.includes('**')"), false);
  run(`globalThis.warning=node('div'); warning.id='fRoomExtraWarning'; warning.className='meta warn no-shorten';
    root.append(warning); warning.textContent=t('layout.inspector.room_extra_none_humidity'); shortenHint(warning);`);
  assert.equal(run("warning.parentElement"), run("root"));
});

test("wrapping dynamic hints refreshes section info buttons without another widget edit", () => {
  const run = setup();
  run(`document.querySelector=()=>root;
    globalThis.sectionRefreshes=0;refreshInspectorSectionHelp=()=>{sectionRefreshes++;};
    hint.textContent=full;shortenInspectorHints();`);
  assert.equal(run("sectionRefreshes"), 1);
  assert.equal(run("hint.parentElement.dataset.genericHint"), "true");
});

test("Popup paragraphs preserve authored line breaks and complete entity IDs", () => {
  const run = setup();
  assert.deepEqual(Array.from(run(`inspectorHelpParagraphs("First line.\\nSecond line.\\n\\nNext paragraph.")`)), ["First line.\nSecond line.", "Next paragraph."]);
  assert.deepEqual(Array.from(run(`inspectorHelpParagraphs("Choose sensor.demo_energy_counter. Then select the source.")`)), ["Choose sensor.demo_energy_counter.", "Then select the source."]);
  assert.deepEqual(Array.from(run(`inspectorHelpParagraphs("Optional. Choose a numeric sensor. The unit follows the sensor.")`)), ["Optional. Choose a numeric sensor.", "The unit follows the sensor."]);
});

test("Short hints and warnings stay directly visible", () => {
  const run = setup();
  run(`hint.className="meta warn"; shortenHint(hint);`);
  assert.equal(run("hint.parentElement"), run("root"));
  run(`hint.className="meta"; hint.textContent="Short explanation."; shortenHint(hint);`);
  assert.equal(run("hint.parentElement"), run("root"));
});


for (const [type, anchor] of [["house_dashboard", "room-overview"], ["energy_dashboard", "energy-dashboard"]]) {
  test(`Top-level help for ${type} has an explanation and the matching documentation link`, () => {
    const run = setup();
    run(`editor.layout = {pages:[{id:'page', type:'${type}',title:'Demo',widgets:[]}]};
      editor.selectedPageId='page'; editor.selectedWidgetId=null;
      globalThis.info=createInspectorOverviewButton(); info.onclick(click);
      globalThis.dialog=document.body.querySelector('dialog');`);
    assert.equal(run("dialog.querySelector('h3').textContent"), "This page");
    assert.ok(run("dialog.querySelectorAll('p').map(p=>p.textContent).join(' ').length") > 60);
    assert.equal(run("dialog.querySelector('a').href"), `https://andre68723.github.io/BETTA-HA-PANEL/pages.html#${anchor}`);
  });
}

test("Every widget has translated top-level help and its own documentation section", () => {
  const run = setup();
  for (const type of Array.from(run("WIDGET_NAME_TYPES"))) {
    run(`editor.layout={pages:[{id:'page', widgets:[{id:'widget',type:'${type}',title:'Demo'}]}]};
      editor.selectedPageId='page'; editor.selectedWidgetId='widget';`);
    for (const lang of ["en", "de"]) {
      run(`editor.i18nLanguage='${lang}'; editor.i18nMap=WEB_I18N_BUILTIN['${lang}'];`);
      const helpType = run("inspectorHelpTopic().type");
      const key = `inspector.help.widget_${helpType}`;
      assert.notEqual(run(`t('${key}')`), key, `${lang} ${type}`);
      // The popup repeats the guide's summary and links to the guide, which must exist.
      const guide = helpType === "wallbox_evcc" ? "wallbox" : helpType;
      assert.ok(existsSync(new URL(`../../docs/widgets/${guide}.md`, import.meta.url)), guide);
      assert.equal(run("inspectorHelpTopic().url"), `https://andre68723.github.io/BETTA-HA-PANEL/widgets/${guide}.html`);
    }
  }
});

test("Text-size info renders role sizes as a list and a separate explanation", () => {
  const run = setup();
  run(`openInspectorHelp('Text sizes',null,{textSizes:true,topic:{},url:'https://andre68723.github.io/BETTA-HA-PANEL/appearance.html#text-size'});
    globalThis.dialog=document.body.querySelector('dialog');`);
  assert.equal(run("dialog.querySelectorAll('dt').length"), 5);
  assert.equal(run("dialog.querySelectorAll('dd').every(node=>node.textContent.endsWith(' px'))"), true);
  assert.equal(run("dialog.querySelectorAll('h3').length"), 2);
});

test("solar alignment help has separate bold headings and paragraphs in both languages", () => {
  const run = setup();
  for (const language of ["en", "de"]) {
    run(`editor.i18nLanguage='${language}'; editor.i18nMap=WEB_I18N_BUILTIN['${language}']; hint.textContent=t('layout.inspector.readability_hint');
      openInspectorHelp('Solar',hint,{topic:{}}); globalThis.dialog=document.body.querySelector('dialog');`);
    const headings = Array.from(run("dialog.querySelectorAll('h4').map(n=>n.textContent)"));
    assert.equal(headings.length, 3);
    assert.ok(headings.every(text => !text.endsWith(":")));
    assert.equal(headings[1], language === "de" ? "Tage am Wetter ausrichten" : "Line up days with the weather tile");
    assert.equal(run("dialog.querySelectorAll('p').length"), 4);
    assert.ok(run("dialog.querySelectorAll('strong').length") >= 3);
    run("dialog.close()");
  }
});

test("every built-in widget overview contains emphasis and preserves its documentation link", () => {
  const run = setup();
  for (const type of Array.from(run("WIDGET_NAME_TYPES"))) {
    run(`editor.layout={pages:[{id:'page',widgets:[{id:'widget',type:'${type}',title:'Demo'}]}]};
      editor.selectedPageId='page';editor.selectedWidgetId='widget';`);
    for (const language of ["en", "de"]) {
      run(`editor.i18nLanguage='${language}'; editor.i18nMap=WEB_I18N_BUILTIN['${language}']; openInspectorHelp('Demo',null,{overview:true});
        globalThis.dialog=document.body.querySelector('dialog');`);
      assert.ok(run("dialog.querySelectorAll('strong').length") > 0, `${language} ${type}`);
      assert.equal(run("dialog.querySelector('a').href"), run("inspectorHelpTopic().url"));
      run("dialog.close()");
    }
  }
});

for (const language of ['en', 'de']) for (const type of ['energy_timeline', 'energy_price_forecast', 'parqet_portfolio']) {
  test(`${type} source requirements open behind info with practical context (${language})`, () => {
    const run = setup();
    run(`editor.i18nLanguage=${JSON.stringify(language)};editor.i18nMap=WEB_I18N_BUILTIN[${JSON.stringify(language)}];
      el.entitySourceWarning=node('div');el.entitySourceWarning.className='meta warn no-shorten';
      el.entitySourceWarning.classList.toggle=(name,hidden)=>{el.entitySourceWarning.hidden=hidden};
      el.entitySourceHint=hint;hint.id='entitySourceHint';
      el.entitySourceHint.classList.toggle=()=>{};
      el.energyTimelineHint=hint;
      refreshEntitySourceWarning({type:${JSON.stringify(type)}});
      shortenHint(hint);globalThis.row=hint.parentElement;row.querySelector('button').onclick(click);
      globalThis.dialog=document.body.querySelector('dialog');`);
    assert.equal(run("dialog.open"), true);
    assert.ok(run("dialog.querySelectorAll('h4').length") >= 3, "structured context rather than an unexplained attribute list");
    assert.equal(run("el.entitySourceWarning.textContent"), "", "requirements do not become an inline warning");
    const text = run("dialog.textContent");
    if (type === 'energy_timeline') {
      assert.match(text, /Recorder/); assert.match(text, /total_increasing/); assert.match(text, /detailedForecast/);
      assert.match(text, /state-of-charge|Ladestand/);
    } else if (type === 'energy_price_forecast') {
      assert.match(text, /CO₂/); assert.match(text, /EUR\/kWh/); assert.match(text, /raw_today/);
      assert.match(text, /If no prices|Wenn keine Preise/);
    } else { assert.match(text, /Monetary|Geldbetrag/); assert.match(text, /fees|Gebühren/); assert.match(text, /XIRR/); }
  });
}
