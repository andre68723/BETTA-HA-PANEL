/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
 * Wrapping entity fields for the local prototype; existing Inspector handlers own model changes.
 */
function spikeBindEntityField(input, field, resize) {
  input.hidden = true;
  function mirror() {
    if (field.value !== input.value) field.value = input.value;
    resize(field);
  }
  input.addEventListener('input', mirror);
  input.addEventListener('change', mirror);
  input.focus = (...args) => { mirror(); field.focus(...args); };
  input.select = () => field.select();
  function forward(type) {
    input.value = field.value.replace(/[\r\n]/g, '');
    input.dispatchEvent(new Event(type, { bubbles: true }));
  }
  field.addEventListener('input', () => { forward('input'); resize(field); });
  for (const type of ['change', 'blur', 'focus']) {
    field.addEventListener(type, () => forward(type));
  }
  field.addEventListener('keydown', event => {
    if (event.key === 'Enter') { event.preventDefault(); field.blur(); }
  });
}

function spikeOpenSensorPicker(input) {
  const titleKey = 'entity_picker.title_sensor';
  const titleFallback = 'Choose Sensor';
  openLightEntityPicker('spike_page_sensor', {
    inspectorTarget: { input, domain: 'sensor', titleKey, titleFallback },
    configOverride: entityPickerConfigForDomain('sensor', titleKey, titleFallback),
  });
}

function spikeFullHint(hint) {
  const full = hint.dataset.hintFull;
  if (full !== undefined) {
    hint.textContent = full;
    delete hint.dataset.hintFull;
    delete hint.dataset.hintShown;
    delete hint.dataset.hintOpen;
  }
  return hint.textContent.trim();
}
