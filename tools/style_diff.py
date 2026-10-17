#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
"""How different do the appearance styles look? A number per style pair, from the SDL simulator.

  tools/style_diff.py [--fixture gallery|readability|matte_reference ...] [--threshold 8] [--min PCT]

Renders every page of the chosen fixtures (default: all three) in each style of tools/ui_styles.py,
then compares each pair of styles on the content area, ignoring the top bar and navigation. A pixel
counts when it belongs to a widget in either image (differs from that image's own page background)
and its colour differs by more than --threshold levels. The result is the percentage of such pixels
per page; a separate column tells how many pages also change the page background.

This is a review aid for the "does this style earn its place" question (docs/appearance.md), not a
gate: --min PCT makes the tool exit 1 when a pair's *median* page difference is below PCT, for a
run before retiring or adding a style. A low number means the styles look alike on those pages; it
does not say why (colours, shapes, layout). Look at the lowest pages it lists.
"""
import argparse
import itertools
import json
import os
import statistics
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_simulator_gallery import DATA, build_simulator, prepare_fixture  # noqa: E402
from ui_styles import STYLES  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
TOP, BOTTOM = 44, 600  # content area of the 720x720 simulator frame (top bar above, navigation below)


def load_fixtures(names):
    available = {
        'gallery': lambda: prepare_fixture(json.loads(DATA.read_text(encoding='utf-8')))[1],
        'readability': lambda: json.loads((REPO / 'tests/simulator/readability.json').read_text(encoding='utf-8')),
        'matte_reference': lambda: json.loads((REPO / 'tests/simulator/matte_reference.json').read_text(encoding='utf-8')),
    }
    unknown = [n for n in names if n not in available]
    if unknown:
        sys.exit(f'unknown fixture(s): {", ".join(unknown)} (choose from {", ".join(available)})')
    return {n: available[n]() for n in names}


def render_all(binary, fixtures, out):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy')
    jobs = []
    for name, fixture in fixtures.items():
        path = out / f'{name}.json'
        path.write_text(json.dumps(fixture, ensure_ascii=False), encoding='utf-8')
        for page in fixture['pages']:
            if page.get('type'):
                continue  # special pages (energy, house, ...) need their own renderer
            for style in STYLES:
                jobs.append((name, path, page['id'], style, out / f"{name}__{page['id']}__{style}.bmp"))

    def run(job):
        _, path, page, style, image = job
        subprocess.run([str(binary), '--fixture', str(path), '--page', page, '--style', style, '--theme', 'dark_v2',
                        '--language', 'en', '--exit-after-ms', '300', '--screenshot', str(image)],
                       env=env, check=True, capture_output=True)

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        list(pool.map(run, jobs))
    return jobs


def widget_mask(image, threshold):
    """Content pixels that differ from the page background (the colour just inside the content area)."""
    crop = image.crop((0, TOP, image.width, BOTTOM)).convert('RGB')
    background = crop.getpixel((2, 2))
    diff = ImageChops.difference(crop, Image.new('RGB', crop.size, background)).convert('L')
    return diff.point(lambda v: 255 if v > threshold else 0), crop, background


def compare(a, b, threshold):
    """(percent of widget pixels that differ, whether the page background differs)."""
    mask_a, crop_a, bg_a = widget_mask(a, threshold)
    mask_b, crop_b, bg_b = widget_mask(b, threshold)
    union = ImageChops.lighter(mask_a, mask_b)
    total = union.histogram()[255]
    changed = ImageChops.difference(crop_a, crop_b).convert('L').point(lambda v: 255 if v > threshold else 0)
    hit = ImageChops.multiply(changed, union).histogram()[255]
    bg_differs = max(abs(x - y) for x, y in zip(bg_a, bg_b)) > threshold
    return (100.0 * hit / total if total else 0.0), bg_differs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--fixture', nargs='+', default=['gallery', 'readability', 'matte_reference'])
    parser.add_argument('--threshold', type=int, default=8, help='colour difference (0-255) that counts, default 8')
    parser.add_argument('--min', type=float, help='exit 1 if any pair has a median page difference below this percent')
    parser.add_argument('--lowest', type=int, default=3, help='list this many least different pages per pair')
    args = parser.parse_args()

    fixtures = load_fixtures(args.fixture)
    binary = build_simulator()
    with tempfile.TemporaryDirectory(prefix='betta_style_diff_') as temp:
        out = Path(temp)
        jobs = render_all(binary, fixtures, out)
        pages = sorted({(j[0], j[2]) for j in jobs})
        results = {}
        for pair in itertools.combinations(STYLES, 2):
            rows = []
            for name, page in pages:
                a = Image.open(out / f'{name}__{page}__{pair[0]}.bmp')
                b = Image.open(out / f'{name}__{page}__{pair[1]}.bmp')
                percent, bg = compare(a, b, args.threshold)
                rows.append((percent, bg, f'{name}/{page}'))
            results[pair] = rows

    print(f'{len(pages)} pages, content area only, threshold {args.threshold}\n')
    print(f'{"pair":<14}{"median %":>10}{"mean %":>9}{"min %":>8}{"bg differs":>12}')
    failed = []
    for pair, rows in results.items():
        values = [r[0] for r in rows]
        median = statistics.median(values)
        bg = sum(r[1] for r in rows)
        print(f'{"/".join(pair):<14}{median:>10.1f}{statistics.mean(values):>9.1f}{min(values):>8.1f}{bg:>8}/{len(rows)}')
        if args.min is not None and median < args.min:
            failed.append(pair)
    print()
    for pair, rows in results.items():
        low = sorted(rows)[:args.lowest]
        print(f'least different, {"/".join(pair)}: ' + ', '.join(f'{name} {p:.0f}%' for p, _, name in low))
    if failed:
        print('\nBELOW --min: ' + ', '.join('/'.join(p) for p in failed))
        sys.exit(1)


if __name__ == '__main__':
    main()
