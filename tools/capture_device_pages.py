#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
"""Capture every page of a panel's saved layout as PNG plus a manifest, never overwriting a set.

  tools/capture_device_pages.py <panel-ip> [--out-dir DIR] [--reference-dir DIR] [--pages id ...]

Navigates with POST /api/navigate, waits for the page to render, then saves the framebuffer with
scripts/screenshot.sh. manifest.json records the firmware commit, the layout hash and, with
--reference-dir, the SHA-256 of the images it is compared with, so an immutable "before" set can be
verified later. The panel is returned to its first page. Publish the set with
tools/import_device_captures.py --from <set>.
Uses curl (a Python socket can fail with "No route to host" on macOS local-network rules).
Set BETTA_PASSWORD when the panel requires the editor login.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def curl(ip, path, data=None, timeout=20):
    cmd = ['curl', '-fsS', '-m', str(timeout)]
    if os.environ.get('BETTA_PASSWORD'):
        cmd += ['-u', f"admin:{os.environ['BETTA_PASSWORD']}"]
    if data is not None:
        cmd += ['-X', 'POST', '-H', 'Content-Type: application/json', '-d', json.dumps(data)]
    return subprocess.run(cmd + [f'http://{ip}{path}'], check=True, capture_output=True).stdout


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('ip')
    parser.add_argument('--out-dir', type=Path, help='default: docs/img/device-<timestamp>')
    parser.add_argument('--reference-dir', type=Path, help='existing set whose PNG hashes go into the manifest')
    parser.add_argument('--pages', nargs='+', help='page ids to capture (default: all pages of the layout)')
    parser.add_argument('--settle', type=float, default=3.0, help='seconds to wait after each navigation')
    args = parser.parse_args()

    version = json.loads(curl(args.ip, '/api/version'))
    layout_raw = curl(args.ip, '/api/layout')
    pages = json.loads(layout_raw)['pages']
    wanted = args.pages or [p['id'] for p in pages]
    unknown = sorted(set(wanted) - {p['id'] for p in pages})
    if unknown:
        sys.exit(f'unknown page id(s): {", ".join(unknown)}')

    out = args.out_dir or REPO / 'docs/img' / f'device-{datetime.now():%Y%m%d-%H%M%S}'
    out.mkdir(parents=True, exist_ok=False)  # earlier sets stay untouched

    captured = []
    try:
        for page in pages:
            if page['id'] not in wanted:
                continue
            curl(args.ip, '/api/navigate', {'page': page['id']})
            time.sleep(args.settle)
            image = out / f"{page['id']}.png"
            subprocess.run([str(REPO / 'scripts/screenshot.sh'), args.ip, str(image)], check=True, capture_output=True)
            if image.suffix != '.png' or not image.exists():
                sys.exit(f'no PNG converter available, {image} missing')
            captured.append({'id': page['id'], 'title': page.get('title', page['id']), 'image': image.name,
                             'captured': datetime.now().isoformat()})
            print(image)
    finally:
        curl(args.ip, '/api/navigate', {'page': pages[0]['id']})  # leave the panel on its first page

    manifest = {
        'captured': datetime.now().isoformat(),
        'source': 'device framebuffer',
        'board': version.get('board'),
        'firmware': version.get('git_commit'),
        'resolution': [version.get('screen_w'), version.get('screen_h')],
        'layout_sha256': hashlib.sha256(layout_raw).hexdigest(),
        'pages': captured,
    }
    if args.reference_dir:
        manifest['reference_sha256'] = {p.name: sha256(p) for p in sorted(args.reference_dir.glob('*.png'))}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
    print(f'{len(captured)} page(s) in {out}')


if __name__ == '__main__':
    main()
