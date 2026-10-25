<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk | Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Fonts of the panel

Every text, symbol, icon and emoji of the firmware is drawn by LVGL's `lv_tiny_ttf` from **four small TrueType subsets** that are
embedded in the image as const arrays (they stay in flash, `lv_tiny_ttf` reads them in place). No bitmap font is generated per size and
`lv_font_conv` is not used: a size is an integer given at run time, so the text size can follow the board, the role and the user's setting
(`docs/appearance.md`, "Text size") without regenerating anything.

| Subset | File | Source | Codepoints | License |
| --- | --- | --- | --- | --- |
| Text | `poppins_medium_subset.ttf` | Poppins Medium (Google Fonts) | the Latin range below, `kern` kept | SIL OFL 1.1, `POPPINS-OFL.txt` |
| Emphasis | `poppins_bold_subset.ttf` | Poppins Bold (Google Fonts) | same Latin range and kerning | SIL OFL 1.1, `POPPINS-OFL.txt` |
| Symbols and icons | `mdi_subset.ttf` | Material Design Icons webfont (`materialdesignicons-webfont.ttf` in this directory) | `mdi_codepoints.txt` | Apache-2.0, `MDI-APACHE-2.0.txt` |
| Emoji | `noto_emoji_subset.ttf` | Noto Emoji (monochrome outlines), static instance wght 400 of `NotoEmoji[wght].ttf` | `emoji_codepoints.txt` | SIL OFL 1.1, `NOTO_EMOJI-OFL.txt` |

Each subset also exists as a C array (`ttf_poppins_data.c`, `ttf_poppins_bold_data.c`, `ttf_mdi_data.c`, `ttf_emoji_data.c`, declared in `ttf_data.h`); `emoji_codepoints.inc` is the
sorted emoji list `ui/notification_text.c` includes. All of these files are **generated** by `tools/generate_widget_fonts.py`: edit a manifest, run the
tool, commit the result.

## How the firmware uses them

`ui/text_font.c` (the font manager) makes `lv_tiny_ttf` fonts on demand and keeps them for the session (at most `UI_FONT_MAX_INSTANCES`): the text font at a
pixel size (`ui_font_role()`, `ui_font_text_px()`), the symbol and icon fonts (`ui_font_icon()`, wrapped by `APP_FONT_SYMBOL_*` in `app_symbols.h` and by
`mdi_font_registry.c`) and the emoji font that follows a text size. Notification emphasis requests `ui_font_emphasis(base)` for Poppins Bold with the same size/metrics and fallback as its Medium base.
It shares the instance budget; failure returns the base font. Two things matter when a font is created:

- **Metrics.** `lv_tiny_ttf` takes line height and base line from the font itself, which makes Poppins lines about 20 % taller than the former bitmap fonts and
  shifts every tile. The manager therefore applies a metrics table (`ui/text_scale.c`: Poppins 16 ... 64 px, each icon font, the emoji sizes) after every create, and
  the tiny_ttf base line is the table value minus 1. A new size or icon font needs its row in the table; compare it with a screenshot before using it.
- **Fallbacks.** Each text font falls back to the emoji font of its size, each symbol font to the text font of its role, so a label can mix text, a symbol and an
  emoji. The chains are set at run time and never rewritten while labels use a font.

LVGL's own compile-time default font is the tiny built-in unscii 8 (`CONFIG_LV_USE_CUSTOM_FONT_DEFAULT`, root `CMakeLists.txt`); the UI sets the manager's `meta`
font as the inherited font of the screen, so an unstyled label stays readable.

Subsetting flags and why (`tools/generate_widget_fonts.py`): hinting is off (`lv_tiny_ttf` never runs TrueType bytecode; the sources carry no hinting tables but
Noto's tiny `gasp`); **glyph bounds are recalculated**, because stb_truetype sizes each cached A8 glyph from the box in the glyph header and a webfont exporter can leave it
loose (the MDI icons start at x 0 although their ink starts further right, which doubled the bitmap area of the symbols and differed from the old tight boxes);
`--check` fails when a header box is not tight. The scale factor depends only on unitsPerEm, so the head box does not matter for it. `desubroutinize` is not used: it
concerns CFF outlines and all four sources are `glyf` fonts.

## Regenerate

Needs fontTools (`python3 -m pip install fonttools brotli`).

```bash
# Text: Poppins Medium from Google Fonts
curl -LO https://raw.githubusercontent.com/google/fonts/main/ofl/poppins/Poppins-Medium.ttf
python3 tools/generate_widget_fonts.py --poppins Poppins-Medium.ttf
# Notification emphasis: the same range and derived subscript digits
curl -LO https://raw.githubusercontent.com/google/fonts/main/ofl/poppins/Poppins-Bold.ttf
python3 tools/generate_widget_fonts.py --bold Poppins-Bold.ttf
# Symbols and icons: from the committed full MDI font and mdi_codepoints.txt
python3 tools/generate_widget_fonts.py --mdi
# Emoji: the variable Noto Emoji font is set to wght 400 and subset to emoji_codepoints.txt
curl -L -o 'NotoEmoji[wght].ttf' 'https://raw.githubusercontent.com/google/fonts/main/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf'
python3 tools/generate_widget_fonts.py --emoji 'NotoEmoji[wght].ttf'
# The editor's preview font: the Poppins subset as a WOFF2 data URI in components/webui/www/styles.css
python3 tools/generate_widget_fonts.py --web
# Check that subsets, arrays and manifests agree (scripts/check.sh runs it when fontTools is installed)
python3 tools/generate_widget_fonts.py --check
```

Text range (Latin-1, Euro and common punctuation, including Dutch/German accents, `°`, `µ`, `²`, `³` and subscript digits):
`0x20-0x7E, 0xA0-0xFF, 0x2013-0x2014, 0x2018-0x2019, 0x201C-0x201D, 0x2022, 0x2026, 0x2080-0x2089, 0x20AC, 0x2212`. Add a character there and regenerate rather than
fixing one size. The text font is Poppins **Medium**, not Regular: LVGL's former built-in Montserrat was the Medium weight and Regular's thinner strokes read
noticeably weaker on the 4" panel.

Both Poppins weights have no subscript digits. The generator derives `₁₂₃` by lowering its native superior glyphs by half an em;
the remaining digits use half-size regular outlines with a baseline 0.15 em below the regular baseline.
Existing glyphs and font metrics stay unchanged. The simulator checks `CO₂`, `m²`, `m³` and all subscript digits at every text size.

### Symbols and icons

`mdi_codepoints.txt` lists every MDI codepoint the firmware can draw (UI symbols, tile icons, status, energy, arrow and weather glyphs). To add an icon: look up its
codepoint (Material Design Icons cheat sheet), add a `U+F....` line, add a `APP_SYMBOL_*` string to `app_symbols.h` if it is a UI symbol, regenerate with `--mdi`
and use it with `APP_FONT_SYMBOL_SMALL/MEDIUM/LARGE` (18, 26, 48 px; the media transport glyphs are small in their em square, so use them one step up) or the
tile icon fonts of `mdi_font_registry.h`. Never use `LV_SYMBOL_*`: no font is left to render it. The icon artwork is Apache-2.0 (notice in
`MDI-APACHE-2.0.txt`); do not embed brand logos.

### Emoji

`emoji_codepoints.txt` is the set of standalone, monochrome emoji the panel can show in notifications (faces and reactions, weather, alerts, household and device
states). The rendering is the text colour; sequence shaping is not supported. A small set of them is also drawn in color in the notification popup (below). `ui/notification_text.c` keeps a supported emoji, shows a base
glyph for an emoji with a skin tone, and replaces everything it cannot shape (joined sequences, flags, keycaps, tag sequences, unsupported emoji) by `[Emoji]`
once per sequence. It also replaces symbols Poppins cannot draw (box-drawing lines, `✓`, `Δ`) by ones it can; see the header of `notification_text.h`. Add a codepoint (Noto Emoji must contain it; the tool reports a missing one), run `--emoji`, and add a case to `tests/host/test_notification_text.c`.

### Color emoji of the notification popup

`notification_emoji.txt` lists the few motifs (about 40) that the notification popup draws in color; every other emoji stays monochrome. Each codepoint must
also be in `emoji_codepoints.txt`, otherwise the sanitizer replaces it before it reaches the label. `tools/generate_notification_emoji.py` takes the images
from a pinned revision of `googlefonts/noto-emoji` (`2D/png/128`, Apache-2.0, `NOTO_EMOJI_IMAGES-APACHE-2.0.txt`), scales them premultiplied to the popup's text
sizes (20, 24, 28 and 32 px: the META role on every board and user step) and writes `notification_emoji_data.c` as raw RGB565A8, so the panel decodes
nothing and reads the images in place from flash (about 8.4 KB per motif, all four sizes). `ui/notification_emoji.c` wraps the popup's text font in an
`lv_imgfont` (`LV_USE_IMGFONT`): a codepoint of the table draws the image, any other character falls back to the wrapped font.

```bash
python3 -m pip install pillow
python3 tools/generate_notification_emoji.py --cache /private/tmp/betta-noto128   # after editing notification_emoji.txt
python3 tools/generate_notification_emoji.py --check
```
