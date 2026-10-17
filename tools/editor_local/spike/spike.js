/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
 * Local prototype adapter. The production editor sources remain unchanged.
 */
(async function startSpike() {
  while (!window.localEditorReady || !editor.layout) await new Promise(resolve => setTimeout(resolve, 80));
  const templates = await (await fetch('/spike-templates.json')).json();
  const $ = id => document.getElementById(id);
  const shell = $('editorShell');
  const workspace = document.querySelector('.workspace');
  const body = document.querySelector('.workspace-body');
  const aside = document.querySelector('.actions-panel');
  const sidebar = document.querySelector('.sidebar');
  const paths = {
    save: 'M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h12l4 4v12a2 2 0 0 1-2 2ZM7 3v6h9V3M7 21v-8h10v8',
    export: 'M12 3v12m-4-4 4 4 4-4M4 15v6h16v-6',
    undo: 'm8 4-5 5 5 5M3 9h11a7 7 0 0 1 6 10',
    grid: 'M3 3h18v18H3zM3 9h18M3 15h18M9 3v18M15 3v18',
    copy: 'M8 8h13v13H8zM4 16H3V3h13v1',
    bulb: 'M8 15C1 7 8 2 12 3s11 4 4 12v3H8v-3M9 21h6M9 15h6',
    chart: 'M3 3v18h18M6 16l5-7 4 3 6-8',
    home: 'm2 11 10-8 10 8M5 10v11h14V10M10 21v-7h4v7',
    music: 'M9 18V5l11-2v13M9 8l11-2M3 18a3 2 0 1 0 6 0 3 2 0 1 0-6 0M14 16a3 2 0 1 0 6 0 3 2 0 1 0-6 0'
  };
  const icon = key => `<svg viewBox="0 0 24 24" aria-hidden="true">${SPIKE_WIDGET_ICONS[key] || `<path d="${paths[key] || paths.grid}"></path>`}</svg>`;
  function element(tag, cls, html) { const node=document.createElement(tag);node.className=cls;if(html)node.innerHTML=html;return node; }
  function button(id, caption, fn, symbol) {
    const node=element('button','btn small spike-button',`${symbol ? icon(symbol) : ''}<span>${caption}</span>`);
    node.id=id;node.type='button';node.title=caption;node.onclick=fn;return node;
  }
  let inspectorMode='closed', lastSelection=null, pageId=null;
  let scaleMode='fit', zoom=1, grid=true, snapping=true, preview=false;
  const banner=document.getElementById('localEditorToolbar');
  if(banner)banner.classList.add('spike-banner');
  const reset=button('spikeReset','Demo zurücksetzen', async () => {
    if (!confirm('Lokale Demo-Änderungen zurücksetzen?')) return;
    await fetch('/api/spike/reset',{method:'POST'}); location.reload();
  });
  $('layoutPane').append(reset);
  shell.insertBefore(document.querySelector('.workspace-top'),sidebar);
  const pages=element('div','spike-pages');
  const pageTabs=element('nav','spike-page-tabs');
  pages.append(pageTabs);workspace.prepend(pages);
  $('layoutPane').prepend($('pagesSection'));
  setSectionCollapsed('pages',false);setSectionCollapsed('widgets',false);setSectionCollapsed('inspector',false);
  const tools=element('div','spike-tools');pages.append(tools);
  const undoButton=button('spikeUndo','Rückgängig',()=>restoreHistory(false),'undo');
  const redoButton=button('spikeRedo','Wiederholen',()=>restoreHistory(true),'undo');redoButton.classList.add('spike-redo');
  for(const node of [undoButton,redoButton]){
    node.classList.add('spike-icon-button');node.setAttribute('aria-label',node.title);
  }
  const gridButton=button('spikeGrid','Raster',()=>{grid=!grid;syncTools();},'grid');
  const snapButton=button('spikeSnap','Einrasten',()=>{snapping=!snapping;syncTools();});
  const leftButton=button('spikeLibraryToggle','Widgets',()=>{document.body.classList.toggle('spike-left-open');document.body.classList.remove('spike-right-open');});
  const propsButton=button('spikeProperties','Eigenschaften',()=>{preview=false;inspectorMode='widget';openRight();sync();});
  const pagePropsButton=button('spikePageProperties','Seiteneinstellungen',()=>{preview=false;editor.selectedWidgetId=null;renderAll();inspectorMode='page';openRight();sync();});
  const previewButton=button('spikePreview','Vorschau',()=>{preview=!preview;if(preview)inspectorMode='closed';sync();});
  tools.append(leftButton,undoButton,redoButton,pagePropsButton,propsButton);
  const library=element('section','spike-library');
  const libraryHead=element('div','spike-library-head');
  libraryHead.append(button('spikeContentTab','← Zurück',()=>{setPane('content');$('addTileBtn').focus();}),element('strong','','Widget hinzufügen'));
  library.append(libraryHead);
  const search=element('input','spike-library-search');search.type='search';search.placeholder='Widget suchen …';search.setAttribute('aria-label','Widget-Bibliothek durchsuchen');
  $('addTileBtn').onclick=()=>{setPane('library');search.focus();};
  library.append(search);
  const groups=[
    ['Werte & Verläufe',[['sensor','Sensor','chart'],['gauge','Gauge','chart'],['graph','Graph','chart'],['binary_tile','Status','grid'],['contact_tile','Kontakt','grid']]],
    ['Steuerung',[['light_tile','Licht','bulb'],['button','Button','grid'],['slider','Slider','grid'],['heating_tile','Heizung','grid'],['climate_tile','Klima','grid'],['cover_tile','Cover','grid'],['fan_tile','Lüfter','grid'],['lock_tile','Schloss','grid'],['select_tile','Auswahl','grid'],['datetime_tile','Datum/Zeit','grid'],['scene_tile','Szene','grid']]],
    ['Räume, Wetter & Energie',[['room_tile','Raum','home'],['weather_tile','Wetter','grid'],['weather_3day','Vorhersage','grid'],['solar_forecast','Solar','chart'],['energy_timeline','Energie','chart']]],
    ['Media & Geräte',[['media_player','Media','music'],['sonos_favorites','Sonos','music'],['bambu_printer','Bambu','grid'],['vacuum_tile','Saugroboter','grid'],['camera_tile','Kamera','grid'],['car_widget','Auto','grid'],['wallbox_evcc','Wallbox','grid'],['todo_list','Aufgaben','grid']]],
    ['Layout',[['empty_tile','Abstand','grid']]]
  ];
  for(const [name,types] of groups){
    const section=element('section','spike-library-group');section.append(element('h3','',name));const items=element('div','spike-library-items');
    for(const [type,name,key] of types){
      if(!templates[type])continue;
      const item=button('spikeAdd_'+type,name,()=>{
        if(isFixedPage(selectedPage())){setStatus('Auf dieser Seite werden die Kacheln automatisch angeordnet.');return;}
        capture();
        const sample=structuredClone(templates[type]);
        const wasSuspended=layoutHistory.suspended;layoutHistory.suspended=true;
        try {
          const widget=addWidget(type,{entityId:sample.entity_id,title:name});
          if(!widget)return;
          const {id,rect}=widget;delete sample.id;delete sample.rect;
          Object.assign(widget,sample,{id,rect,title:name});
        } finally {layoutHistory.suspended=wasSuspended;}
        renderAll();capture();
        setPane('content');inspectorMode='widget';openRight();document.body.classList.remove('spike-left-open');sync();
      },type);
      item.dataset.widgetIcon=type;
      item.dataset.search=(name+' '+type).toLowerCase();items.append(item);
    }
    section.append(items);library.append(section);
  }
  search.oninput=()=>{for(const section of library.querySelectorAll('.spike-library-group')){let visible=0;for(const item of section.querySelectorAll('button')){item.hidden=!item.dataset.search.includes(search.value.toLowerCase());if(!item.hidden)visible++;}section.hidden=!visible;}};
  const tip=element('p','spike-library-tip','Klicken fügt eine Kachel mit Demo-Entitäten hinzu. Name, Größe und Entitäten kannst du rechts bearbeiten.');library.append(tip);
  library.append(button('spikeEntityChooser','Über Entität hinzufügen …',()=>openAddTileDialog()));
  $('layoutPane').prepend(library);
  const viewport=element('div','spike-viewport');const wrap=document.querySelector('.canvas-wrap');wrap.before(viewport);viewport.append(wrap);
  const foot=element('div','spike-view-controls');const selectionInfo=element('span','spike-selection-info');
  const zoomLabel=element('span','spike-zoom-label');zoomLabel.id='spikeZoomLabel';
  const viewOptions=element('details','spike-view-options');
  viewOptions.id='spikeViewOptions';
  const viewSummary=element('summary','','Ansicht');viewOptions.append(viewSummary);
  const viewPanel=element('div','spike-view-panel');
  const zoomTools=element('div','spike-zoom-tools');
  zoomTools.append(button('spikeZoomOut','−',()=>changeZoom(.8)),zoomLabel,button('spikeZoomIn','+',()=>changeZoom(1.25)),button('spikeFit','Einpassen',()=>{scaleMode='fit';updateCanvasScale();}),button('spikeActual','100 %',()=>{scaleMode='manual';zoom=1;updateCanvasScale();}));
  viewPanel.append(zoomTools,gridButton,snapButton,previewButton);viewOptions.append(viewPanel);
  foot.append(selectionInfo,viewOptions);viewport.append(foot);
  const inspectorHead=element('div','spike-inspector-head');const inspectorTitle=element('strong','');
  const inspectorTitleGroup=element('div','inspector-title-with-help');inspectorTitleGroup.append(inspectorTitle,createInspectorOverviewButton());
  inspectorHead.append(inspectorTitleGroup);aside.prepend(inspectorHead);
  const inspectorEmpty=element('p','spike-inspector-empty');inspectorHead.after(inspectorEmpty);
  let inspectorTab='properties';
  const inspectorTabs=element('nav','spike-inspector-tabs');inspectorTabs.setAttribute('role','tablist');
  const propertyTab=button('spikePropertiesTab','Eigenschaften',()=>{inspectorTab='properties';sync();});
  const positionTab=button('spikePositionTab','Position & Größe',()=>{inspectorTab='position';sync();});
  inspectorTabs.append(propertyTab,positionTab);inspectorHead.after(inspectorTabs);
  inspectorTabs.addEventListener('keydown',event=>{
    if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
    event.preventDefault();
    inspectorTab=event.key==='Home'?'properties':event.key==='End'?'position':inspectorTab==='properties'?'position':'properties';
    sync();(inspectorTab==='properties'?propertyTab:positionTab).focus();
  });
  const inspectorActions=element('div','spike-inspector-actions');inspectorActions.append(button('spikeDuplicate','Duplizieren',()=>duplicateWidget(),'copy'),button('spikeDelete','Löschen',()=>deleteWidget()));aside.append(inspectorActions);
  const position=$('fX').closest('details');position.open=true;
  const propertyPanel=element('div','spike-property-panel');propertyPanel.id='spikePropertyPanel';
  const positionPanel=element('div','spike-position-panel');positionPanel.id='spikePositionPanel';
  const widgetForm=$('inspectorSection').querySelector('.form');
  $('inspectorGroupDataLabel').closest('details').classList.add('spike-data-group');
  for(const group of widgetForm.querySelectorAll(':scope > .inspector-group'))(group===position?positionPanel:propertyPanel).append(group);
  widgetForm.append(propertyPanel,positionPanel);
  for(const [tab,panel] of [[propertyTab,propertyPanel],[positionTab,positionPanel]]){
    tab.setAttribute('role','tab');tab.setAttribute('aria-controls',panel.id);
    panel.setAttribute('role','tabpanel');panel.setAttribute('aria-labelledby',tab.id);
  }
  const style=$('inspectorGroupStyle');style.open=false;
  $('energyTimelineColorOptions').querySelector('summary').addEventListener('click',()=>{stylePreference=true;});
  let stylePreference=null;
  style.querySelector('summary').addEventListener('click',event=>{
    event.preventDefault();stylePreference=!style.open;style.open=stylePreference;
  });
  // Reuse the bound fields; the compact view forwards editing events to them.
  const entityEditors=[];
  for(const input of aside.querySelectorAll('#inspectorSection .entity-input-row input[type="text"], #energyPageOptions input[type="text"]')){
    let row=input.closest('.entity-input-row');
    if(!row){
      row=element('div','entity-input-row');input.before(row);row.append(input);
      const picker=button('',t('entity_picker.search'),()=>spikeOpenSensorPicker(input));
      picker.classList.add('entity-picker-btn');picker.dataset.spikeSensorPicker='true';row.append(picker);
    }
    const field=element('textarea','spike-entity-editor');
    field.id=input.id+'Spike';field.rows=1;field.spellcheck=false;field.wrap='soft';
    field.placeholder=input.placeholder;
    if(input.maxLength>0)field.maxLength=input.maxLength;
    input.before(field);spikeBindEntityField(input,field,resizeEntityEditor);
    const searchButton=row.querySelector('[data-inspector-entity-target], [data-spike-sensor-picker]');
    if(searchButton){
      const caption=searchButton.textContent.trim();
      searchButton.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="10" cy="10" r="6"/><path d="m15 15 6 6"/></svg>';
      searchButton.title=caption;searchButton.setAttribute('aria-label',caption);
    }
    entityEditors.push({input,field});
  }
  const nameRow=element('div','spike-name-row');
  const nameField=$('fTitle').closest('label');nameField.before(nameRow);
  nameRow.append(nameField,$('fShowName').closest('label'));
  function optionalGroup(id,inputs){
    const group=element('details','spike-optional-group');group.id=id;
    const summary=element('summary','');const fields=element('div','spike-optional-fields');
    fields.append(...inputs.map(input=>input.closest('label')));group.append(summary,fields);
    return group;
  }
  const batteryOptions=optionalGroup('spikeEtlBattery',[el.fEtlCharge,el.fEtlDischarge,el.fEtlSoc]);
  const forecastOptions=optionalGroup('spikeEtlForecast',[el.fEtlForecast]);
  const etlHelp=createInspectorHelpRow($('energyTimelineHint'),'');
  $('energyTimelineFields').append(batteryOptions,forecastOptions,etlHelp);
  const etlStyle=element('div','slider-options hidden');etlStyle.append(el.fEtlStyle.closest('label'));
  style.querySelector('.inspector-group-body').prepend(etlStyle);
  // Page configuration gets its own compact layout, rather than a squeezed copy of the old form.
  const pageForm=$('pageInspectorSection').querySelector('.form');
  const navigation=optionalGroup('spikePageNavigation',[$('pageIconSelect'),el.pageTitleInput]);
  navigation.querySelector('.spike-optional-fields').append($('pageIconHint'),$('pageNavWarning'));
  pageForm.prepend(navigation);
  const energyOptions=$('energyPageOptions');
  const pageBattery=optionalGroup('spikePageBattery',[$('energyBatterySoc')]);
  const batteryManual=element('div','spike-optional-fields');
  batteryManual.append(...['energyBatteryPower','energyBatteryCharge','energyBatteryDischarge'].map(id=>$(id).closest('label')));
  pageBattery.querySelector('.spike-optional-fields').prepend(batteryManual);
  const meters=optionalGroup('spikePageMeters',[$('energyGas'),$('energyWater')]);
  energyOptions.insertBefore(pageBattery,$('applyEnergyPageBtn'));
  energyOptions.insertBefore(meters,$('applyEnergyPageBtn'));
  const pageHelpBody=element('div','spike-optional-fields');
  for(const id of ['energyPageHint','energySourceHint','energyDetailsHint','energyGasWaterHint','energySolarPowerHint','energyBatterySocHint'])pageHelpBody.append($(id));
  const pageHelp=createInspectorHelpRow(pageHelpBody,'');energyOptions.append(pageHelp);
  for(const pair of $('energyManualOptions').querySelectorAll('.grid-2'))pair.replaceWith(...pair.children);
  // Sensor changes already apply through the existing change/blur handlers.
  $('applyEnergyPageBtn').hidden=true;
  let inspectorPage=null;
  let inspectorWidget=null;
  function refreshInspectorHelp(){
    for(const hint of aside.querySelectorAll('.meta, .field-hint'))presentInspectorHint(hint);
  }
  // Shared bindInspectorHints observes this workspace, including fields moved by the adapter.
  function resizeEntityEditor(field){
    if(!field.getClientRects().length)return;
    field.style.height='auto';field.style.height=Math.max(32,field.scrollHeight+2)+'px';
    if(document.activeElement!==field){field.setSelectionRange(0,0);field.scrollTop=0;}
    field.scrollLeft=0;
  }
  function fitStyleGroup(){
    if(inspectorMode!=='widget'||inspectorTab!=='properties'||preview||!style.getClientRects().length)return;
    if(stylePreference!==null){style.open=stylePreference;return;}
    // Measure the expanded form before paint. Only keep it open when the whole Inspector fits.
    style.open=true;
    style.open=aside.scrollHeight<=aside.clientHeight+1;
  }
  function reflowInspector(){entityEditors.forEach(({field})=>resizeEntityEditor(field));fitStyleGroup();}
  aside.addEventListener('toggle',event=>requestAnimationFrame(()=>{
    entityEditors.forEach(({field})=>resizeEntityEditor(field));
    if(event.target!==style)fitStyleGroup();
  }),true);
  function syncInspector(){
    const widget=selectedWidget();const energy=widget?.type==='energy_timeline';
    const lang=(de,en)=>editor.i18nLanguage==='de'?de:en;
    // Local responses have no firmware build; keep the device information useful and honest.
    if(!el.boardLabelValue.textContent){
      const projects={panel4:'betta-ha-panel',panel10:'betta-ha-panel-10.1',paneljc:'betta-ha-panel-jc','paneljc-v2':'betta-ha-panel-jc-v2',tab5:'betta-ha-panel-tab5'};
      el.boardLabelValue.textContent=BOARD_SHORT_LABELS[projects[editor.appBoard]]||editor.appBoard;
      el.boardLabel.hidden=!el.boardLabelValue.textContent;
    }
    if(!editor.appBuildDate&&!editor.appBuildTime){
      el.buildTimeValue.textContent=lang('Lokale Quellen','Local sources');el.buildTimeLabel.hidden=false;
    }
    if(editor.selectedPageId!==inspectorPage){
      navigation.open=false;pageBattery.open=false;meters.open=false;inspectorPage=editor.selectedPageId;
    }
    navigation.querySelector('summary').textContent=lang('Titel und Navigation','Title and navigation');
    $('applyPageBtn').textContent=lang('Übernehmen','Apply');
    $('energyDetailsLabel').textContent=lang('7-Tage-Ansicht','Seven-day view');
    setInspectorHelpCaption(pageHelp,lang('Hinweise zur Datenquelle und den Sensoren','Data source and sensor help'));
    const manual=$('energySource').value===ENERGY_SOURCE_MANUAL;
    batteryManual.hidden=!manual;meters.hidden=!manual;
    for(const [group,de,en,ids] of [
      [pageBattery,'Batterie','Battery',['energyBatteryPower','energyBatteryCharge','energyBatteryDischarge','energyBatterySoc']],
      [meters,'Gas und Wasser','Gas and water',['energyGas','energyWater']],
    ]){
      const count=ids.filter(id=>$(id).value.trim()&&(manual||id==='energyBatterySoc')).length;
      group.querySelector('summary').textContent=lang(de,en)+' · '+(count?`${count} ${lang('Sensoren','sensors')}`:lang('optional','optional'));
    }
    if(widget?.id!==inspectorWidget){
      batteryOptions.open=false;forecastOptions.open=false;inspectorTab='properties';stylePreference=null;inspectorWidget=widget?.id;
      // An advanced disclosure from the previous widget must not inflate the next Inspector.
      for(const group of aside.querySelectorAll('details:not(.inspector-group)')){
        if(!group.closest('#pageInspectorSection'))group.open=false;
      }
      aside.scrollTop=0;
    }
    for(const [id,de,en] of [
      ['fSecondaryEntityLabel',widget?.type==='car_widget'?'Ladeleistung':widget?.type==='vacuum_tile'?'Kartenbild':widget?.type==='gauge'?'Zusatzinfo 1':null,
        widget?.type==='car_widget'?'Charging power':widget?.type==='vacuum_tile'?'Map image':widget?.type==='gauge'?'Extra readout 1':null],
      ['fTertiaryEntityLabel',widget?.type==='car_widget'?'Reichweite':widget?.type==='vacuum_tile'?'Reinigungsmodus':widget?.type==='gauge'?'Zusatzinfo 2':null,
        widget?.type==='car_widget'?'Range':widget?.type==='vacuum_tile'?'Cleaning mode':widget?.type==='gauge'?'Extra readout 2':null],
      ['fCarPrecondLabel','Vorklimatisieren','Preconditioning'],['fCarLockLabel','Verriegelung','Door lock'],['fCarTempLabel','Innenraum · °C','Interior · °C'],
    ])if(de)$(id).textContent=lang(de,en);
    if(widget?.type==='gauge'){
      const extras=['radial','horizontal'].includes(widget.style_variant);
      $('fSecondaryEntityWrap').classList.toggle('hidden',!extras);
      $('fTertiaryEntityWrap').classList.toggle('hidden',!extras);
    }
    if(widget?.type==='car_widget')$('fEntityLabel').textContent=lang('Akkustand · %','Battery level · %');
    for(const [field,de,en] of [
      ['Mode','Lademodus','Charge mode'],['Vehicle','Fahrzeug','Vehicle'],['Target','Ladeziel · %','Target · %'],
      ['Power','Ladeleistung','Charging power'],['Soc','Ladezustand · %','State of charge · %'],
      ['Connected','Verbunden','Connected'],['Charging','Lädt','Charging'],['Disabled','Deaktiviert','Disabled'],
      ['Effective','Effektives Ziel · %','Effective target · %'],['Energy','Geladen · kWh','Charged · kWh'],['Remaining','Restzeit','Remaining time'],
    ])$('fEvcc'+field+'Label').textContent=lang(de,en);
    $('evccDetailsSummary').textContent=lang('Sensoren und Aktionen · 11','Sensors and actions · 11');
    for(const label of $('bambuFields').querySelectorAll('label > span:first-child')){
      label.textContent=label.textContent.replace(/\s*\((optional|Optional)\)/g,'');
    }
    if(['sensor','binary_tile','contact_tile','lock_tile','vacuum_tile'].includes(widget?.type)){
      $('fHistoryPopupLabel').textContent=['lock_tile','vacuum_tile'].includes(widget.type)
        ?lang('Verlauf bei langem Druck','History on long press'):lang('Verlauf bei Tipp','History on tap');
    }
    const roomEntities=el.fRoomEntities;
    if(widget?.type==='room_tile'&&roomEntities)roomEntities.rows=Math.max(2,Math.min(7,roomEntities.value.split('\n').length));
    propertyTab.querySelector('span').textContent=lang('Eigenschaften','Properties');
    positionTab.querySelector('span').textContent=lang('Position & Größe','Position & size');
    for(const [input,de,en] of [[el.fX,'Links · px','Left · px'],[el.fY,'Oben · px','Top · px'],[el.fW,'Breite · px','Width · px'],[el.fH,'Höhe · px','Height · px']]){
      const label=input.closest('label');let caption=label.querySelector('span');
      if(!caption){caption=element('span','');label.firstChild.replaceWith(caption);}
      caption.textContent=lang(de,en);
    }
    inspectorTabs.setAttribute('aria-label',lang('Widget bearbeiten','Edit widget'));
    for(const [tab,active] of [[propertyTab,inspectorTab==='properties'],[positionTab,inspectorTab==='position']]){
      tab.classList.toggle('active',active);tab.setAttribute('aria-selected',active);tab.tabIndex=active?0:-1;
    }
    propertyPanel.hidden=inspectorTab!=='properties';positionPanel.hidden=inspectorTab!=='position';
    if(energy){
      for(const [id,de,en] of [
        ['fEntityLabel','Netzbezug · kWh','Grid import · kWh'],
        ['fSecondaryEntityLabel','Einspeisung · kWh','Grid export · kWh'],
        ['fTertiaryEntityLabel','Solarerzeugung · kWh','Solar production · kWh'],
        ['fEtlChargeLabel','Laden · kWh','Charge · kWh'],
        ['fEtlDischargeLabel','Entladen · kWh','Discharge · kWh'],
        ['fEtlSocLabel','Ladezustand · %','State of charge · %'],
        ['fEtlForecastLabel','Solcast-Sensor','Solcast sensor'],
      ])$(id).textContent=lang(de,en);
    }
    const batteryCount=[el.fEtlCharge,el.fEtlDischarge,el.fEtlSoc].filter(input=>input.value.trim()).length;
    batteryOptions.querySelector('summary').textContent=lang('Batterie','Battery')+' · '+(batteryCount?`${batteryCount} ${lang('Sensoren','sensors')}`:lang('optional','optional'));
    forecastOptions.querySelector('summary').textContent=lang('Solarprognose','Solar forecast')+' · '+(el.fEtlForecast.value.trim()?lang('konfiguriert','configured'):lang('optional','optional'));
    setInspectorHelpCaption(etlHelp,lang('Hinweise zu den Zählern','Counter requirements'));
    etlStyle.classList.toggle('hidden',!energy);updateStyleGroupVisibility();
    pageTabs.setAttribute('aria-label',t('layout.pages.heading'));
    const pageIds=new Set(editor.layout.pages.map(page=>page.id));
    for(const tab of pageTabs.children)if(!pageIds.has(tab.dataset.page))tab.remove();
    editor.layout.pages.forEach((page,index)=>{
      let tab=Array.from(pageTabs.children).find(node=>node.dataset.page===page.id);
      if(!tab){
        tab=button('','',()=>{editor.selectedPageId=page.id;editor.selectedWidgetId=null;renderAll();});tab.dataset.page=page.id;
      }
      tab.querySelector('span').textContent=[pageIconEmoji(page),page.title||page.id].filter(Boolean).join(' ');
      tab.title=page.title||page.id;tab.classList.toggle('active',page.id===editor.selectedPageId);
      tab.setAttribute('aria-current',page.id===editor.selectedPageId?'page':'false');
      if(pageTabs.children[index]!==tab)pageTabs.insertBefore(tab,pageTabs.children[index]||null);
    });
    for(const {input,field} of entityEditors){
      if(document.activeElement!==field)field.value=input.value;
      field.disabled=input.disabled;
      const label=input.closest('label').querySelector('span');
      if(label)field.setAttribute('aria-label',label.textContent);
    }
    refreshInspectorSearchButtons(aside);
    requestAnimationFrame(reflowInspector);
    refreshInspectorHelp();
  }
  function openRight(){document.body.classList.add('spike-right-open');document.body.classList.remove('spike-left-open');}
  function setPane(next){library.hidden=next!=='library';$('widgetsSection').hidden=next!=='content';}
  function decorateActions(){
    const save=$('saveBtn');save.innerHTML=icon('save')+'<span>Speichern</span>';save.classList.add('spike-caption-action');save.title='Layout lokal speichern';save.setAttribute('aria-label','Speichern');
    const exp=$('exportBtn');exp.innerHTML=icon('export')+'<span>Export</span>';exp.classList.add('spike-caption-action');exp.title='Aktuelles Layout als JSON herunterladen';exp.setAttribute('aria-label','Export');save.after(exp);
    $('layoutJsonMenuBtn').title='JSON importieren';$('layoutJsonMenuBtn').setAttribute('aria-label','JSON importieren');
  }
  const originalTranslate=applyWebTranslations;applyWebTranslations=function(...args){const value=originalTranslate(...args);decorateActions();syncInspector();return value;};
  decorateActions();
  function syncTools(){
    el.canvas.classList.toggle('spike-no-grid',!grid);gridButton.classList.toggle('active',grid);snapButton.classList.toggle('active',snapping);
    gridButton.setAttribute('aria-pressed',grid);snapButton.setAttribute('aria-pressed',snapping);
    undoButton.disabled=layoutHistory.cursor===0;redoButton.disabled=layoutHistory.cursor>=layoutHistory.states.length-1;
  }
  function sync(){
    const current=editor.selectedWidgetId;
    if(current!==lastSelection){inspectorMode=current?'widget':'closed';if(current)openRight();lastSelection=current;}
    if(editor.selectedPageId!==pageId){
      pageId=editor.selectedPageId;
      if(!current){inspectorMode=isFixedPage(selectedPage())?'page':'closed';if(inspectorMode==='page')openRight();}
    }
    body.classList.toggle('spike-inspector-open',inspectorMode!=='closed'&&!preview);
    body.classList.toggle('spike-page-inspector',inspectorMode==='page');
    body.classList.toggle('spike-preview',preview);
    $('pageInspectorSection').style.display=inspectorMode==='page'&&!preview?'':'none';
    $('inspectorSection').style.display=inspectorMode==='widget'&&!preview?'':'none';
    inspectorActions.style.display=inspectorMode==='widget'&&!preview?'':'none';
    inspectorTabs.hidden=inspectorMode!=='widget'||preview;
    inspectorEmpty.hidden=inspectorMode!=='closed'&&!preview;
    inspectorEmpty.textContent=isFixedPage(selectedPage())
      ?(editor.i18nLanguage==='de'?'Öffne die Seiteneinstellungen, um diese Seite anzupassen.':'Open page settings to configure this page.')
      :(editor.i18nLanguage==='de'?'Wähle ein Widget in der Vorschau oder links in der Liste.':'Select a widget in the preview or in the list on the left.');
    propsButton.hidden=!selectedWidget()||(!preview&&inspectorMode==='widget'&&document.body.classList.contains('spike-right-open'));
    pagePropsButton.hidden=isFixedPage(selectedPage())&&!preview&&inspectorMode==='page'&&document.body.classList.contains('spike-right-open');
    const widget=selectedWidget();inspectorTitle.textContent=!inspectorEmpty.hidden?(editor.i18nLanguage==='de'?'Eigenschaften':'Properties'):inspectorMode==='page'?selectedPage()?.title||'Seite':widget?.title||'Widget';
    selectionInfo.textContent=widget?`${widget.title||widget.type} · ${widget.rect.w} × ${widget.rect.h} px`:`${selectedPage()?.title||''} · ${selectedPage()?.widgets?.length||0} Widgets`;
    previewButton.querySelector('span').textContent=preview?'Bearbeiten':'Vorschau';previewButton.classList.toggle('active',preview);
    document.body.classList.toggle('spike-settings',editor.activePane==='settings');
    syncInspector();
    syncTools();requestAnimationFrame(()=>updateCanvasScale());
  }
  const originalInspector=renderInspector;renderInspector=function(...args){const value=originalInspector(...args);sync();return value;};
  let openRoom=null;
  const originalHouseOptions=renderHouseOptions;
  renderHouseOptions=function(page){
    const value=originalHouseOptions(page);
    for(const [index,row] of Array.from(el.houseRoomList.children).entries()){
      const room=page.house.rooms[index], key=page.id+':'+index;
      const [floor,name,up,down,remove,sensor,extraMode,extraEntity]=Array.from(row.children);
      const group=element('details','spike-room-editor');
      const summary=element('summary','');
      const updateSummary=()=>{summary.textContent=[name.value,floor.value].filter(Boolean).join(' · ');};
      updateSummary();name.addEventListener('change',updateSummary);floor.addEventListener('change',updateSummary);
      const fields=element('div','spike-room-fields');
      const label=(caption,input)=>{const node=element('label','');node.append(element('span','',caption),input);input.setAttribute('aria-label',caption);fields.append(node);};
      const lang=(de,en)=>editor.i18nLanguage==='de'?de:en;
      label(lang('Raum','Room'),name);label(lang('Stockwerk','Floor'),floor);
      const sensorRow=element('div','entity-input-row');sensor.replaceWith(sensorRow);sensorRow.append(sensor);
      const field=element('textarea','spike-entity-editor');field.rows=1;field.value=sensor.value;field.spellcheck=false;
      sensor.before(field);spikeBindEntityField(sensor,field,resizeEntityEditor);
      label(lang('Temperatur','Temperature'),sensorRow);field.setAttribute('aria-label',lang('Temperatursensor','Temperature sensor'));
      label(lang('Zusatzinfo','Extra readout'),extraMode);label(lang('Sensor','Sensor'),extraEntity);
      const extraLabel=extraEntity.parentElement;
      const updateExtra=()=>{extraLabel.hidden=extraMode.value==='default';extraEntity.title=extraEntity.value;};
      updateExtra();extraMode.addEventListener('change',updateExtra);extraEntity.addEventListener('change',updateExtra);
      const actions=element('div','spike-room-actions');
      for(const [control,caption] of [[up,lang('Nach oben','Move up')],[down,lang('Nach unten','Move down')],[remove,lang('Raum entfernen','Remove room')]]){
        control.title=caption;control.setAttribute('aria-label',caption);actions.append(control);
      }
      up.disabled=index===0;down.disabled=index===page.house.rooms.length-1;
      fields.append(actions);group.append(summary,fields);group.open=openRoom===key;
      row.replaceWith(group);
      group.addEventListener('toggle',()=>{
        if(group.open){openRoom=key;for(const other of el.houseRoomList.children)if(other!==group)other.open=false;resizeEntityEditor(field);}
        else if(openRoom===key)openRoom=null;
      });
      if(group.open)requestAnimationFrame(()=>resizeEntityEditor(field));
    }
    return value;
  };
  const originalWidgets=renderWidgets;
  renderWidgets=function(...args){
    const value=originalWidgets(...args);
    if(selectedPage()?.type===HOUSE_PAGE_TYPE){
      const message=el.widgetsList.querySelector('.muted');
      if(message)message.textContent=editor.i18nLanguage==='de'?'Die Räume werden automatisch angeordnet. Bearbeite sie rechts in den Seiteneinstellungen.':'Rooms are arranged automatically. Edit them in page settings on the right.';
    }
    return value;
  };
  const originalEnergyPreview=renderEnergyCanvasPreview;
  renderEnergyCanvasPreview=function(page){
    const value=originalEnergyPreview(page);
    if(page.energy?.source===ENERGY_SOURCE_MANUAL){
      for(const [name,key] of [['gas','gas_entity_id'],['water','water_entity_id']]){
        const label=el.canvas.querySelector(`.energy-preview-label.${name} small`);
        if(label)label.textContent=page.energy[key]||t('layout.energy.no_sensor');
      }
    }
    return value;
  };
  const originalInspectorChange=renderInspectorChange;renderInspectorChange=function(...args){const value=originalInspectorChange(...args);sync();return value;};
  const originalAll=renderAll;renderAll=function(...args){const value=originalAll(...args);sync();return value;};
  const originalActive=setActivePane;setActivePane=function(...args){const value=originalActive(...args);sync();return value;};
  const originalSnap=snap;snap=function(value){return snapping?originalSnap(value):Math.round(value);};
  updateCanvasScale=function(){
    if(!el.canvas||!wrap.clientWidth)return;
    // Leave a small rounding margin at fractional browser zoom levels.
    const availableW=Math.max(100,wrap.clientWidth-34),availableH=Math.max(120,wrap.clientHeight-34);
    const fit=Math.max(.1,Math.min(1,availableW/CANVAS_WIDTH,availableH/CANVAS_HEIGHT));
    const scale=scaleMode==='fit'?fit:zoom;
    editor.canvasScale=scale;
    $('canvasStage').style.width=`${Math.ceil(CANVAS_WIDTH*scale)}px`;$('canvasStage').style.height=`${Math.ceil(CANVAS_HEIGHT*scale)}px`;
    el.canvas.style.width=`${CANVAS_WIDTH}px`;el.canvas.style.height=`${CANVAS_HEIGHT}px`;el.canvas.style.setProperty('--canvas-scale',scale);
    zoomLabel.textContent=`${Math.round(scale*100)} %`;
    $('spikeFit').classList.toggle('active',scaleMode==='fit');
  };
  function changeZoom(factor){zoom=Math.min(2,Math.max(.25,(editor.canvasScale||1)*factor));scaleMode='manual';updateCanvasScale();}
  wrap.addEventListener('wheel',event=>{if(event.ctrlKey||event.metaKey){event.preventDefault();changeZoom(event.deltaY<0?1.1:1/1.1);}},{passive:false});
  // Both local views use the production history and shortcut handler.
  function capture(){layoutHistoryBoundary();syncTools();}
  function restoreHistory(forward){layoutHistoryStep(forward?1:-1);sync();}
  const originalHistoryControls=layoutHistoryControls;
  layoutHistoryControls=function(...args){originalHistoryControls(...args);syncTools();};
  const originalLoad=loadLayout;loadLayout=async function(...args){const result=await originalLoad(...args);sync();return result;};
  document.addEventListener('keydown',event=>{if(event.key==='Escape')viewOptions.open=false;});
  // Keep close/undo controls clickable without wrapper deselection or canvas pointer capture.
  tools.addEventListener('pointerdown',event=>event.stopPropagation());foot.addEventListener('pointerdown',event=>event.stopPropagation());
  document.addEventListener('pointerdown',event=>{if(!viewOptions.contains(event.target))viewOptions.open=false;});
  window.addEventListener('resize',()=>{updateCanvasScale();requestAnimationFrame(reflowInspector);});
  new ResizeObserver(()=>updateCanvasScale()).observe(wrap);

  setPane('content');sync();document.body.classList.add('spike-ready');window.spikeReady=true;
})().catch(error=>{console.error(error);document.body.insertAdjacentHTML('afterbegin','<p>Spike konnte nicht gestartet werden. Details stehen in der Browserkonsole.</p>');});
