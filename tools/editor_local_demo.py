# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Comprehensive, synthetic Inspector exercise for the wide local editor."""
import copy
import json

from generate_simulator_gallery import DATA, GEOMETRY, prepare_fixture


def add_quote_samples(states):
    """Ten synthetic quote choices, including unbound sensors for the Add quote picker."""
    template = next(s for s in states if s.get('platform') == 'easy_stock')
    for slug, name, symbol, price, change in [
        ('europe_etf', 'Demo Europe ETF', 'DEUR.DE', 54.2, 0.8),
        ('solar', 'Demo Solar', 'DSOL.DE', 82.75, -1.2),
        ('health', 'Demo Health', 'DHLT.DE', 147.6, 0.4),
        ('bonds_etf', 'Demo Bonds ETF', 'DBND.DE', 99.1, 0.0),
        ('pacific_etf', 'Demo Pacific ETF', 'DPAC.DE', 36.8, -0.6),
    ]:
        entity_id = 'sensor.demo_' + slug
        if any(s['entity_id'] == entity_id for s in states):
            continue
        sample = copy.deepcopy(template)
        sample.update(entity_id=entity_id, state=str(price))
        sample['attributes'].update(friendly_name=name, long_name=name, symbol=symbol,
                                    change_pct=change, change=round(price * change / 100, 4),
                                    previous_close=round(price * (1 - change / 100), 4))
        states.append(sample)


def all_widgets_fixture(variant):
    width, _, height = GEOMETRY[variant]
    if width < 1280:
        raise ValueError('The all-widgets demo needs a wide board geometry (paneljc, panel10 or tab5)')
    _, gallery = prepare_fixture(json.loads(DATA.read_text(encoding='utf-8')), variant)
    samples = {w['id']: w for p in gallery['pages'] for w in p.get('widgets', [])}
    states = copy.deepcopy(gallery['states'])
    add_quote_samples(states)

    def state(entity, value, **attributes):
        existing = next((item for item in states if item['entity_id'] == entity), None)
        if existing:
            existing.update(state=str(value), attributes={**existing.get('attributes', {}), **attributes})
        else:
            states.append(dict(entity_id=entity, state=str(value), attributes=attributes))

    state('sensor.demo_car_charging_power', 7.4, unit_of_measurement='kW', device_class='power')
    state('sensor.demo_car_remaining_range', 312, unit_of_measurement='km', device_class='distance')
    state('lock.demo_car_doors', 'locked', friendly_name='Fahrzeug verriegeln')
    state('image.demo_robot_map', '2026-10-11T14:30:00+00:00', friendly_name='Reinigungsplan')
    state('script.demo_evening', 'off', friendly_name='Abendprogramm')
    state('event.demo_doorbell', '2026-10-11T14:30:00+00:00', event_type='pressed', event_types=['pressed'], device_class='doorbell')
    state('sensor.demo_grid_power_signed', 840, unit_of_measurement='W', device_class='power')
    state('sensor.demo_battery_power_signed', -250, unit_of_measurement='W', device_class='power')
    for entity, value in [('grid_import', 1638.2), ('grid_export', 842.1), ('solar_production', 3284.5),
                          ('battery_charge', 728.4), ('battery_discharge', 632.8)]:
        state('sensor.demo_' + entity, value, unit_of_measurement='kWh', device_class='energy',
              state_class='total_increasing')

    pages = []

    def page(page_title, entries):
        widgets = []
        for sample, title, rect, options in entries:
            w = copy.deepcopy(samples[sample]) if sample else {'type': 'empty_tile', 'entity_id': ''}
            w.pop('gallery_title', None)
            w.update(id=f'test_{len(pages)}_{len(widgets)}', title=title, show_name=True,
                     rect=dict(zip(('x', 'y', 'w', 'h'), rect)))
            w.update(options)
            widgets.append(w)
        icons = ['floorplan', 'climate', 'room', 'weather', 'music', 'garage', 'dashboard', 'sofa']
        pages.append(dict(id=f'test_{len(pages)}', title=page_title, icon=icons[len(pages)], widgets=widgets))

    # Keep the layout inside every wide board's 600 px minimum content height.
    # Separate variants exercise mutually exclusive options without invalid combinations.
    page('Werte & Diagramme', [
        ('sensor', 'Temperatur', (20, 20, 200, 140), dict(history_popup=True, sensor_show_age=True)),
        ('smooth', 'Füllstand', (240, 20, 150, 240), dict(gauge_color_mode='gradient', gauge_color_low='#3b82f6', gauge_color_high='#ef4444')),
        ('segmented', 'LED-Skala', (410, 20, 150, 240), dict(gauge_segment_count=20)),
        ('horizontal_gauge', 'Luftfeuchte', (580, 20, 300, 120), dict(secondary_entity_id='sensor.demo_room_temperature', tertiary_entity_id='sensor.demo_humidity')),
        ('radial', 'Radial mit Extras', (900, 20, 240, 280), dict(secondary_entity_id='sensor.demo_room_temperature', tertiary_entity_id='sensor.demo_humidity', gauge_color_mode='range', gauge_target_low=40, gauge_target_high=75, gauge_target_color='#4fcb7e')),
        ('graph_line', 'Kurve', (20, 340, 280, 220), dict(graph_ha_history=True, graph_point_count=48, graph_time_window_min=1440)),
        ('graph_bars', 'Balken', (320, 340, 280, 220), dict(graph_point_count=48, graph_bar_bucket_min=30)),
        ('graph_line', 'Geglättet', (620, 340, 280, 220), dict(graph_display_mode='line_smooth', graph_point_count=48)),
        ('graph_line', 'Kurve und Punkte', (920, 340, 280, 220), dict(graph_display_mode='line_smooth_points', graph_point_count=48)),
    ])
    page('Licht & Klima', [
        ('light', 'Wohnzimmerlicht', (20, 20, 220, 260), {}),
        ('heating_default', 'Heizung', (260, 20, 280, 280), dict(secondary_entity_id='sensor.demo_room_temperature')),
        ('climate', 'Klimaanlage', (560, 20, 320, 280), {}),
        ('fan', 'Lüfter', (900, 20, 320, 240), {}),
        ('button', 'Schalter', (20, 340, 160, 140), dict(button_mode='auto', button_accent_color='#42c9b8')),
        ('vertical', 'Rollo vertikal', (200, 340, 100, 220), dict(slider_accent_color='#42c9b8', slider_entity_domain='cover')),
        ('horizontal', 'Rollo horizontal', (320, 340, 300, 120), dict(slider_accent_color='#ffba49', slider_entity_domain='cover')),
        ('cover', 'Jalousie', (650, 340, 340, 140), {}),
    ])
    room = dict(room_controls=True)
    for key in ('room_humidity', 'room_cover', 'room_media_player'):
        samples[key] = copy.deepcopy(samples[key])
        samples[key]['room_entities'].append('fan.demo_fan')
    page('Räume & Aktionen', [
        ('room_humidity', 'Raum mit Feuchte', (20, 20, 380, 240), room),
        ('room_cover', 'Raum mit Cover', (440, 20, 380, 240), room),
        ('room_media_player', 'Raum mit Medien', (860, 20, 380, 240), room),
        ('contact', 'Türkontakt', (20, 300, 220, 100), dict(history_popup=True)),
        ('binary_sensor', 'Bewegung', (260, 300, 220, 100), dict(display_mode='since', history_popup=True)),
        ('scene', 'Szene', (500, 300, 220, 100), {}),
        ('front_door', 'Schloss', (740, 300, 280, 110), dict(lock_confirm=True, history_popup=True)),
        ('charge_mode', 'Lademodus', (20, 440, 300, 120), {}),
        ('morning_routine', 'Weckzeit', (350, 440, 300, 120), {}),
        ('alarm', 'Alarmanlage', (680, 440, 300, 120), {}),
        (None, 'Freifläche', (1020, 440, 220, 100), {}),
    ])
    page('Wetter & Energie', [
        ('weather_now', 'Aktuelles Wetter', (20, 20, 220, 220), {}),
        ('weather_forecast', 'Wetterbericht', (270, 20, 900, 220), dict(forecast_days=6, show_precipitation=True)),
        ('solar_forecast', 'Solar horizontal', (20, 280, 380, 280), dict(solar_forecast_bar_max_kwh=60, forecast_days=5)),
        ('solar_forecast_vertical', 'Solar vertikal', (420, 280, 260, 280), dict(solar_forecast_bar_max_kwh=60, forecast_days=5)),
        ('energy_last_24_hours', 'Energie mit Batterie', (700, 280, 540, 280), {}),
    ])
    page('Medien & Finanzen', [
        ('media_player', 'Musik', (20, 20, 340, 360), {}),
        ('sonos_favorites', 'Sonos-Favoriten', (380, 20, 300, 360), {}),
        ('portfolio', 'Portfolio', (700, 20, 260, 290), dict(parqet_selector=True)),
        ('camera', 'Einfahrt', (20, 400, 340, 180), {}),
        ('todo_list', 'Einkaufsliste', (380, 380, 500, 200), {}),
        ('energy_price_forecast', 'Energiepreise', (900, 340, 340, 220), {}),
    ])
    page('Geräte & Mobilität', [
        ('bambu_a1', 'Bambu mit allen Sensoren', (20, 20, 320, 520), {}),
        ('vacuum', 'Saugroboter mit Extras', (360, 20, 280, 360), dict(secondary_entity_id='image.demo_robot_map', tertiary_entity_id='select.demo_cleaning_mode', default_mode='Auto')),
        ('car', 'Auto mit allen Sensoren', (660, 20, 260, 540), dict(secondary_entity_id='sensor.demo_car_charging_power', tertiary_entity_id='sensor.demo_car_remaining_range', lock_entity_id='lock.demo_car_doors')),
        ('wallbox', 'Wallbox mit allen Sensoren', (940, 20, 320, 540), {}),
    ])
    quote_ids = ['sensor.demo_insurer', 'sensor.demo_tech']
    page('Aktien & ETFs', [
        ('stocks_charts', title, (20 + index * 420, 20, 400, 560),
         dict(stock_view=view, stock_entities=quote_ids, stock_period='ytd', stock_delta='both', stock_ticker=True))
        for index, (view, title) in enumerate([('list', 'Liste'), ('chart', 'Charts'), ('grid', 'Kacheln')])
    ])
    page('Sensorlisten', [
        ('sensor', 'Einzelwert', (20, 20, 240, 180), {}),
        ('sensor', 'Messwerte', (280, 20, 460, 540), dict(sensor_view='list', sensor_entities=[
            'sensor.demo_humidity', 'sensor.demo_power', 'sensor.demo_car_battery', 'sensor.demo_portfolio_value'])),
        ('sensor', 'Zustände', (760, 20, 500, 540), dict(entity_id='binary_sensor.demo_front_door', sensor_view='list',
            sensor_entities=['binary_sensor.demo_motion', 'sensor.demo_robot_battery'])),
    ])
    for kind, title in [('house_dashboard', 'Raumübersicht'), ('energy_dashboard', 'Energieseite')]:
        special = copy.deepcopy(next(p for p in gallery['pages'] if p.get('type') == kind))
        special.update(id=f'test_{len(pages)}', title=title, icon='house' if kind == 'house_dashboard' else 'energy')
        if kind == 'energy_dashboard':
            special['energy'].update(grid_power_entity_id='sensor.demo_grid_power_signed',
                                     battery_power_entity_id='sensor.demo_battery_power_signed', details='side')
        else:
            for index, mode, entity in [(0, 'humidity', 'sensor.demo_humidity'),
                                         (1, 'cover', 'cover.demo_blind'),
                                         (4, 'media_player', 'media_player.demo_music')]:
                room = special['house']['rooms'][index]
                room['room_entities'].append(entity)
                room.update(room_extra_mode=mode, room_extra_entity_id=entity)
        pages.append(special)

    # Deliberately long, valid IDs expose clipping in compact Inspector fields.
    replacements = {'sensor.demo_grid_import': 'sensor.demo_arbeitszimmer_netzbezug_energie_gesamt_kwh',
                    'sensor.demo_printer_remaining': 'sensor.demo_bambu_a1_verbleibende_druckzeit_in_minuten'}

    def replace(value):
        if isinstance(value, dict): return {k: replace(v) for k, v in value.items()}
        if isinstance(value, list): return [replace(v) for v in value]
        return replacements.get(value, value) if isinstance(value, str) else value

    fixture = replace(dict(version=1, pages=pages, states=states))
    for s in fixture['states']:
        attrs = s.setdefault('attributes', {})
        attrs.setdefault('friendly_name', s['entity_id'].split('.', 1)[1].replace('demo_', '').replace('_', ' '))
        if attrs.get('unit_of_measurement') in ('kWh', 'm³'):
            attrs.setdefault('state_class', 'total_increasing')
    return fixture
