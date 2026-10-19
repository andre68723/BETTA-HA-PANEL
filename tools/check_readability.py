#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
"""Semantic readability diagnostics for a simulator JSONL dump (panel4, 30 cm).

Roles and actual glyph boxes come from the renderer, never inferred from line
height. Unassigned widgets remain diagnostic. --strict promotes reviewed essential
text to failures; compact 20 px is reported explicitly, and must still fit.
"""
import argparse
import json
from pathlib import Path


def problems(records):
    failed = []
    for record in records:
        if record.get('kind') != 'label' or not record.get('essential') or not record.get('text', '').strip():
            continue
        context = f"{record['widget']}: {record['text']!r}"
        if record.get('font_px', 0) < 20 or record.get('H_arcmin_30cm', 0) < 16:
            failed.append(f"{context}: below 16 arcmin capital-height reference at 30 cm")
        if record.get('text_clipped'):
            failed.append(f"{context}: essential text/units clipped")
    return failed


def coverage_problems(records, page_id):
    """Check retained day horizons and actual column positions, beyond rectangles."""
    errors = []
    labels = [r for r in records if r.get('kind') == 'label' and r.get('essential')]
    expected = {'page_1': (6, 5), 'page_esp': (5, 5),
                'readable_oldschool': (3, 3), 'readable_kitchen': (2, 2)}
    if page_id not in expected:
        return errors
    weather = [r for r in labels if 'weather' in r['widget'] and r.get('text_role') == 'label']
    # The solar summary contains a value and its unit, not a day. Match the unit rather than
    # a translated prefix (formerly "Rest "), so changing the language cannot add a forecast slot.
    solar = [r for r in labels if 'solar' in r['widget'] and r.get('text_role') == 'label'
             and not r.get('text', '').strip().lower().endswith('kwh')]
    if len(weather) != expected[page_id][0] or len(solar) != expected[page_id][1]:
        errors.append(f'{page_id}: forecast day coverage changed: {len(weather)} weather / {len(solar)} solar slots')
    if page_id.startswith('readable_'):
        weather.sort(key=lambda r: r['x1']); solar.sort(key=lambda r: r['x1'])
        for w, s in zip(weather, solar):
            if abs((w['x1'] + w['x2']) - (s['x1'] + s['x2'])) > 2:
                errors.append(f'{page_id}: weather/solar date column centers differ')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dumps', nargs='+', type=Path)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    failures = []
    for path in args.dumps:
        records = [json.loads(line) for line in path.read_text().splitlines() if line]
        compact = [r for r in records if r.get('compact_fallback') and r.get('text', '').strip()]
        unassigned = sum(r.get('text_role') == 'unassigned' and r.get('font_px', 0) > 0 for r in records)
        issues = problems(records)
        print(f'{path.name}: {len(issues)} essential issues, {len(compact)} compact fallbacks, {unassigned} unassigned labels')
        for r in compact:
            print(f"  COMPACT {r['widget']}: {r['text']!r} at actual {r.get('font_px')} px")
        failures += [f'{path.name}: {issue}' for issue in issues]
    for issue in failures: print(('FAIL ' if args.strict else 'DIAGNOSTIC ') + issue)
    return 1 if args.strict and failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
