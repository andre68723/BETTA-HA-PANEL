#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Serve the current web editor with synthetic API responses and local saves. No firmware build, panel or HA connection."""
import argparse
import copy
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler
from pathlib import Path
import threading
import time
import math
import sys
import webbrowser
from urllib.parse import parse_qs, urlsplit

from generate_docs_examples import DemoHTTPServer, WWW, demo_api, fixtures
from generate_simulator_gallery import DATA, GEOMETRY, demo_page, prepare_fixture

ROOT = Path(__file__).resolve().parent.parent
SUPPORT = Path(__file__).resolve().parent / 'editor_local'
CSP = "default-src 'self'; connect-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; script-src 'self'; font-src 'self' data:; object-src 'none'; base-uri 'self'"


def demo_fixture(variant):
    wide, compact, _devices = fixtures()
    _, gallery = prepare_fixture(json.loads(DATA.read_text(encoding='utf-8')), variant)
    templates = {}
    for page in gallery['pages']:
        for widget in page.get('widgets', []):
            templates.setdefault(widget['type'], copy.deepcopy(widget))
    fixture = compact if variant == 'panel4' else wide
    if variant == 'tab5':
        fixture['pages'][0] = demo_page(gallery, variant)
    elif variant != 'panel4':
        choices = [('weather_3day', 'Wetter', (20, 20, 600, 280)),
                   ('light_tile', 'Stehlampe', (650, 20, 280, 320)),
                   ('sensor', 'Luftfeuchte', (960, 20, 300, 160)),
                   ('media_player', 'Wohnzimmer', (20, 330, 600, 310)),
                   ('room_tile', 'Raumklima', (650, 380, 610, 260))]
        widgets = []
        for kind, title, rect in choices:
            item = copy.deepcopy(templates[kind])
            item.update(id='demo_' + kind, title=title, rect=dict(zip(('x', 'y', 'w', 'h'), rect)))
            if kind == 'weather_3day': item['forecast_days'] = 4
            if kind == 'sensor': item['entity_id'] = 'sensor.demo_humidity'
            widgets.append(item)
        fixture['pages'][0].update(title='Wohnzimmer', widgets=widgets)
        office = copy.deepcopy(compact['pages'][0])
        office.update(id='office', title='Arbeitszimmer')
        for widget in office['widgets']: widget['id'] = 'office_' + widget['id']
        fixture['pages'].insert(1, office)
    for page in fixture['pages']:
        if page['id'] == 'house': page['title'] = 'Raumübersicht'
        if page['id'] == 'energy': page['title'] = 'Energie'
    return fixture, templates


def demo_responses(variant, fixture_path=None, all_widgets=False):
    fixture, templates = demo_fixture(variant)
    templates['empty_tile'] = {'type': 'empty_tile', 'entity_id': '', 'rect': {'x': 0, 'y': 0, 'w': 200, 'h': 120}}
    if all_widgets:
        from editor_local_demo import all_widgets_fixture
        if fixture_path is not None:
            raise ValueError('Choose either --demo all or --fixture')
        fixture = all_widgets_fixture(variant)
    if fixture_path is not None:
        supplied = json.loads(Path(fixture_path).read_text(encoding='utf-8'))
        if not isinstance(supplied, dict) or not isinstance(supplied.get('pages'), list):
            raise ValueError('Fixture must be a layout object with a pages array')
        fixture.update({key: supplied[key] for key in ('version', 'pages', 'alerts', 'states') if key in supplied})
    for state in fixture['states']:
        # Synthetic picture URLs are unnecessary here; no external resource is fetched.
        if 'entity_picture' in state.get('attributes', {}): state['attributes']['entity_picture'] = ''
    responses = demo_api(fixture)
    if all_widgets:
        widgets = [w for page in fixture['pages'] for w in page.get('widgets', [])]
        def bindings(kind):
            widget = next(w for w in widgets if w['type'] == kind)
            return {key: value for key, value in widget.items() if key.endswith('entity_id')}
        responses['/api/ha/printers'] = {'status': 'ready', 'printers': [
            {'id': 'demo_bambu_a1', 'name': 'Demo Bambu A1', 'entities': bindings('bambu_printer')}]}
        responses['/api/ha/evcc'] = {'status': 'ready', 'loadpoints': [
            {'id': 'demo_wallbox', 'name': 'Garage', 'instance': 'Demo evcc', 'entities': bindings('wallbox_evcc')}]}
    width, height, canvas_height = GEOMETRY[variant]
    version = responses['/api/version']
    version.update(board=variant, screen_w=width, screen_h=height, canvas_w=width, canvas_h=canvas_height,
                   max_pages=8 if width < 1000 else 12,
                   full_nav_pages=5 if width < 1000 else 10,
                   version='LOCAL-EDITOR', safe_mode=False)
    sizes = (16, 20, 24, 28, 32, 36, 42, 48, 56, 64)
    offset = 1 if variant == 'tab5' else 0
    version['text'].update(board=variant, floor=20 if variant in ('panel4', 'tab5') else 16,
                           sizes=[[sizes[index + offset + step] for index in (1, 2, 3, 5, 6)] for step in range(3)])
    responses['/api/settings']['ui'].update(language='de', text_step=0)
    responses['/api/settings']['device'] = {'id': 'local-editor', 'room': 'Lokaler Editor'}
    return responses, templates


class Store:
    def __init__(self, responses, path, reset=False):
        self.responses, self.seed = responses, copy.deepcopy(responses)
        self.path, self.lock = Path(path), threading.RLock()
        self.display_test = {"kind": "active", "percent": 0, "until": 0.0}
        if self.path.exists() and not reset:
            saved = json.loads(self.path.read_text(encoding='utf-8'))
            for key in ('/api/layout', '/api/settings'):
                if key in saved: responses[key] = saved[key]
        if reset: self.persist()

    def display_test_status(self):
        remaining = max(0, math.ceil(self.display_test['until'] - time.monotonic()))
        return {'active': remaining > 0, 'remaining_s': remaining, 'kind': self.display_test['kind'],
                'brightness_percent': self.display_test['percent'], 'local_only': True}

    def persist(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        saved = {key: self.responses[key] for key in ('/api/layout', '/api/settings')}
        temporary = self.path.with_suffix('.tmp')
        temporary.write_text(json.dumps(saved, ensure_ascii=False, indent=2), encoding='utf-8')
        temporary.replace(self.path)


def build_server(port=8766, variant='paneljc', fixture_path=None, state_path=None, spike_dir=None,
                 default_ui='classic', reset=False, all_widgets=False):
    responses, templates = demo_responses(variant, fixture_path, all_widgets)
    suffix = 'all-widgets-state' if all_widgets else 'state'
    store = Store(responses, state_path or ROOT / '.local/editor-local' / f'{variant}-{suffix}.json', reset)
    extra = Path(spike_dir).resolve() if spike_dir else None
    extra_files = ('spike.css', 'widget-icons.js', 'spike.js')
    if extra and any(not (extra / name).is_file() for name in extra_files):
        raise ValueError(f'Spike directory must contain {", ".join(extra_files)}')
    if default_ui == 'spike' and extra is None:
        extra = SUPPORT / 'spike'
        if any(not (extra / name).is_file() for name in extra_files):
            raise ValueError('Bundled spike adapter is unavailable; supply --spike DIR')
    # Additional prototype helpers are optional for externally supplied adapters.
    if extra and (extra / 'entity-fields.js').is_file():
        extra_files += ('entity-fields.js',)

    class Handler(SimpleHTTPRequestHandler):
        def log_message(self, fmt, *args):
            if self.command != 'GET' or (len(args) > 1 and str(args[1]).startswith(('4', '5'))):
                print(fmt % args, flush=True)

        def end_headers(self):
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Security-Policy', CSP)
            super().end_headers()

        def send(self, data, content_type, status=200):
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            if self.command != 'HEAD': self.wfile.write(data)

        def reply(self, value, status=200):
            self.send(json.dumps(value, ensure_ascii=False).encode(), 'application/json; charset=utf-8', status)

        def index(self, ui):
            if ui not in ('classic', 'spike') or (ui == 'spike' and extra is None):
                return self.reply({'error': 'Requested editor view is unavailable'}, 404)
            text = (WWW / 'index.html').read_text(encoding='utf-8')
            text = text.replace('<body>', f'<body data-local-ui="{ui}">')
            css = '<link rel="stylesheet" href="/__local__/local-editor.css">'
            scripts = ''
            if ui == 'spike':
                css += '\n<link rel="stylesheet" href="/__local__/spike.css">'
                scripts += '\n<script src="/__local__/widget-icons.js"></script>'
                if 'entity-fields.js' in extra_files:
                    scripts += '\n<script src="/__local__/entity-fields.js"></script>'
                scripts += '\n<script src="/__local__/spike.js"></script>'
            main = '<script src="/js/main.js"></script>'
            text = text.replace(main, '<script src="/__local__/local-editor.js"></script>\n' + main)
            text = text.replace('</head>', css + '\n</head>').replace('</body>', scripts + '\n</body>')
            self.send(text.encode(), 'text/html; charset=utf-8')

        def do_GET(self):
            url = urlsplit(self.path)
            query = parse_qs(url.query)
            if url.path == '/api/display/test':
                with store.lock: return self.reply(store.display_test_status())
            if url.path in ('/', '/index.html', '/classic.html'):
                return self.index('classic' if url.path == '/classic.html' else query.get('ui', [default_ui])[0])
            if url.path.startswith('/__local__/') and url.path not in (
                    '/__local__/config.json', '/__local__/templates.json'):
                name = url.path[len('/__local__/'):]
                allowed = {'local-editor.js': SUPPORT / 'local-editor.js', 'local-editor.css': SUPPORT / 'local-editor.css'}
                if extra: allowed.update({name: extra / name for name in extra_files})
                if name not in allowed: return self.reply({'error': 'Unknown local asset'}, 404)
                return self.send(allowed[name].read_bytes(), 'text/css' if name.endswith('.css') else 'text/javascript')
            if url.path in ('/spike-templates.json', '/__local__/templates.json'): return self.reply(templates)
            if url.path == '/__local__/config.json':
                return self.reply({'spike': bool(extra), 'ui': default_ui, 'variant': variant,
                                   'canvas_w': GEOMETRY[variant][0], 'canvas_h': GEOMETRY[variant][2]})
            if not url.path.startswith('/api/'): return super().do_GET()
            with store.lock:
                if url.path == '/api/ha/light_entities':
                    domains = query.get('domain', ['sensor'])[0].split(',')
                    search = query.get('search', [''])[0].lower()
                    items = [dict(item, entity_id=item['id'], room=item.get('area_name', 'Demo'))
                             for item in responses['/api/entities']['items']
                             if (item['domain'] in domains or 'room' in domains or 'any' in domains)
                             and search in (item['name'] + ' ' + item['id']).lower()]
                    return self.reply({'items': items, 'status': 'ready', 'pending': False,
                                       'total': len(items), 'loaded': len(items)})
                value = responses.get(url.path, {'items': []})
                if url.path == '/api/state' and 'entity_id' in query:
                    value = {'items': [s for s in value['items'] if s['entity_id'] == query['entity_id'][0]]}
                return self.reply(value)

        def mutation(self):
            origin = self.headers.get('Origin')
            if origin not in (None, f'http://127.0.0.1:{self.server.server_port}', f'http://localhost:{self.server.server_port}'):
                return self.reply({'error': 'Only local browser requests are accepted'}, 403)
            try: length = int(self.headers.get('Content-Length', '0'))
            except ValueError: return self.reply({'error': 'Invalid Content-Length'}, 400)
            if not 0 <= length <= 131072: return self.reply({'error': 'Request too large'}, 413)
            try: body = json.loads(self.rfile.read(length) or b'{}')
            except (ValueError, UnicodeError): return self.reply({'error': 'Invalid JSON'}, 400)
            if not isinstance(body, dict): return self.reply({'error': 'Expected a JSON object'}, 400)
            path = urlsplit(self.path).path
            with store.lock:
                if path == '/api/display/test' and self.command == 'POST':
                    action, percent, kind = body.get('action'), body.get('percent'), body.get('kind')
                    valid_percent = type(percent) is int and 0 <= percent <= 100
                    if action not in ('start', 'update', 'stop') or (action != 'stop' and not valid_percent) or (
                            action == 'start' and kind not in ('active', 'dim', 'night')):
                        return self.reply({'error': 'Invalid brightness test'}, 400)
                    if action == 'start':
                        store.display_test.update(kind=kind, percent=percent, until=time.monotonic() + 30)
                    elif action == 'update' and store.display_test_status()['active']:
                        store.display_test['percent'] = percent
                    elif action == 'stop': store.display_test['until'] = 0
                    return self.reply(store.display_test_status())
                if path == '/api/layout' and self.command == 'PUT':
                    pages = body.get('pages')
                    if not isinstance(pages, list) or not pages or len(pages) > responses['/api/version']['max_pages']:
                        return self.reply({'errors': ['Invalid page count']}, 400)
                    if len(json.dumps(body, ensure_ascii=False).encode()) > 32768:
                        return self.reply({'errors': ['Layout exceeds 32 KiB']}, 400)
                    if any(not isinstance(page, dict) or not isinstance(page.get('widgets', []), list)
                           or len(page.get('widgets', [])) > 32 for page in pages):
                        return self.reply({'errors': ['Invalid widget list or too many widgets']}, 400)
                    responses[path] = body
                    store.persist()
                    return self.reply({'ok': True, 'local_only': True})
                if path == '/api/settings' and self.command == 'PUT':
                    for group in ('ui', 'hardware', 'topbar', 'time'):
                        if isinstance(body.get(group), dict): responses[path].setdefault(group, {}).update(body[group])
                    store.persist()
                    return self.reply({'ok': True, 'local_only': True})
                if path in ('/api/local/reset', '/api/spike/reset') and self.command == 'POST':
                    responses.clear()
                    responses.update(copy.deepcopy(store.seed))
                    store.persist()
                    return self.reply({'ok': True})
                if path == '/api/ha/light_entities' and self.command == 'DELETE': return self.reply({'ok': True})
                return self.reply({'error': 'Unavailable in local editor mode'}, 405)

        do_PUT = mutation
        do_POST = mutation
        do_DELETE = mutation

    server = DemoHTTPServer(('127.0.0.1', port), partial(Handler, directory=str(WWW)))
    server.local_store = store
    return server


def open_browser_on_enter(url):
    try:
        input('Press Enter to open the editor in your default browser. Ctrl+C stops the server.\n')
    except EOFError:
        return
    try:
        opened = webbrowser.open(url)
    except (webbrowser.Error, OSError):
        opened = False
    if not opened:
        print(f'Could not open the browser. Open {url} manually.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8766, help='localhost port (default 8766; 0 chooses a free port)')
    parser.add_argument('--variant', choices=sorted(GEOMETRY), default='paneljc')
    parser.add_argument('--fixture', type=Path, help='layout JSON; optional synthetic states array')
    parser.add_argument('--demo', choices=('all', 'compact'),
                        help='demo layout (default: all widgets on wide boards, compact on panel4); all requires a wide board')
    parser.add_argument('--state-file', type=Path, help='local persistence file (default: separate per variant and demo)')
    parser.add_argument('--spike', type=Path, help='optional local spike adapter directory')
    parser.add_argument('--ui', choices=('classic', 'spike'), default='classic',
                        help='initial view; spike loads the bundled local prototype unless --spike overrides it')
    parser.add_argument('--reset', action='store_true', help='replace local saved state with the supplied/default fixture')
    args = parser.parse_args()
    all_widgets = args.demo == 'all' or (args.demo is None and args.fixture is None and args.variant != 'panel4')
    try:
        server = build_server(args.port, args.variant, args.fixture, args.state_file, args.spike, args.ui, args.reset,
                              all_widgets=all_widgets)
    except (OSError, ValueError) as error:
        parser.exit(2, f'Cannot start local editor: {error}\n')
    url = f'http://127.0.0.1:{server.server_port}/'
    print(f'Local editor: {url}', flush=True)
    if all_widgets:
        print('Demo: all widget types with optional bindings. Use --reset to reload it instead of a local save.', flush=True)
    if args.spike or args.ui == 'spike': print(f'View switch: {url}?ui=classic / {url}?ui=spike', flush=True)
    print(f'Local saves: {server.local_store.path}\nSources served live; refresh the browser after edits. Ctrl+C stops the server.', flush=True)
    if sys.stdin.isatty():
        threading.Thread(target=open_browser_on_enter, args=(url,), daemon=True).start()
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()


if __name__ == '__main__': main()
