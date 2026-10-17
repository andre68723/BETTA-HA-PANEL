#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-FNCL-1.1
# Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
"""Regenerate the colored weather icons (wxicon_<id>_<size>.png) in this folder.

The artwork is Meteocons by Bas Milius (MIT license, see LICENSE-meteocons.txt),
fill style, taken from the published npm package @meteocons/svg-static. Each SVG
is rendered with resvg at the two sizes the weather tile uses and quantized to
96 colors with alpha (Pillow) to keep the flash cost tiny.

Needs: node/npm (network access for npm) and Pillow.
    python3 main/ui/assets/weather/generate_icons.py
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

PACKAGE = "@meteocons/svg-static@0.1.0"
SIZES = (64, 112)  # forecast columns, current condition
COLORS = 96
# Meteocons file name -> icon id used by weather_icons.h
ICONS = {
    "clear-day": "clear_day",
    "clear-night": "clear_night",
    "partly-cloudy-day": "partly_cloudy_day",
    "partly-cloudy-night": "partly_cloudy_night",
    "overcast": "overcast",
    "fog": "fog",
    "hail": "hail",
    "thunderstorms": "thunderstorms",
    "thunderstorms-rain": "thunderstorms_rain",
    "rain": "rain",
    "extreme-rain": "extreme_rain",
    "snow": "snow",
    "sleet": "sleet",
    "wind": "wind",
    "extreme": "extreme",
}
RENDER_JS = """
import { Resvg } from "@resvg/resvg-js";
import fs from "fs";
const icons = JSON.parse(process.argv[2]);
const sizes = JSON.parse(process.argv[3]);
fs.mkdirSync("raw", { recursive: true });
for (const name of icons) {
  const svg = fs.readFileSync(`package/fill/${name}.svg`);
  for (const size of sizes) {
    const png = new Resvg(svg, { fitTo: { mode: "width", value: size } }).render().asPng();
    fs.writeFileSync(`raw/${name}_${size}.png`, png);
  }
}
"""


def run(cmd, cwd):
    subprocess.run(cmd, cwd=cwd, check=True, stdout=subprocess.DEVNULL)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    with tempfile.TemporaryDirectory() as tmp:
        run(["npm", "init", "-y"], tmp)
        run(["npm", "install", "@resvg/resvg-js"], tmp)
        run(["npm", "pack", PACKAGE], tmp)
        tgz = glob.glob(os.path.join(tmp, "meteocons-svg-static-*.tgz"))[0]
        run(["tar", "xzf", tgz], tmp)
        with open(os.path.join(tmp, "render.mjs"), "w") as f:
            f.write(RENDER_JS)
        import json
        run(["node", "render.mjs", json.dumps(list(ICONS)), json.dumps(list(SIZES))], tmp)
        license_src = os.path.join(tmp, "package", "LICENSE")
        shutil.copy(license_src, os.path.join(here, "LICENSE-meteocons.txt"))
        for name, ident in ICONS.items():
            for size in SIZES:
                raw = Image.open(os.path.join(tmp, "raw", f"{name}_{size}.png")).convert("RGBA")
                quant = raw.quantize(colors=COLORS, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
                quant.save(os.path.join(here, f"wxicon_{ident}_{size}.png"), optimize=True)
    total = sum(os.path.getsize(p) for p in glob.glob(os.path.join(here, "wxicon_*.png")))
    print(f"wrote {len(ICONS) * len(SIZES)} icons, {total} bytes")


if __name__ == "__main__":
    sys.exit(main())
