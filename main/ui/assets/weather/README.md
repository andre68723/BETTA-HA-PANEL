# Weather icons

Colored weather icons for the forecast tile, shown at 64 px and 112 px.

- **Source:** [Meteocons](https://github.com/basmilius/weather-icons) by Bas
  Milius, MIT License (`LICENSE-meteocons.txt`), taken from the npm package
  `@meteocons/svg-static` (the "fill" set).
- **Conversion:** `generate_icons.py` renders the SVGs with resvg and quantizes
  them to 96 colors (Pillow) to keep the PNGs small (about 24 KB in total).
- **Embedding:** `main/CMakeLists.txt` embeds the `wxicon_<id>_<size>.png`
  files and defines `APP_HAVE_WEATHER_PNG_ICONS` if all are present and LVGL's
  lodepng decoder is enabled on the supported P4 variants.
- **API:** `weather_icons.h` maps a normalized Home Assistant condition to an
  icon id (`weather_icon_from_condition`) and returns the image descriptor
  (`weather_icon_image`).

To regenerate, follow the comments at the top of `generate_icons.py`.
