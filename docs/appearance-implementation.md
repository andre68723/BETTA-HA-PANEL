<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Appearance implementation

For choosing a style and theme, see [Appearance](appearance.md). This page records
the shared rendering rules, compatibility and measurements for contributors.

## Style IDs and compatibility

Settings store the style as a stable string ID. `main/ui/theme/ui_style_registry.c` defines the supported IDs and
these rules, shared by settings load, the HTTP API, the UI runtime and the simulator:

| Stored or requested ID | Panel and editor show | `PUT /api/settings` |
| --- | --- | --- |
| none (new device) | Glow, the default | -- |
| supported: `flat`, `glow`, `glass`, `matte` | that style | stored |
| anything else, including a removed style | Flat, the fallback | rejected as invalid |

There is no migration: when a style is removed, a panel that stored it shows Flat until a style is chosen again in
the editor.

Adding a style touches: the registry (`ui_style_registry.{h,c}`, `UI_STYLES`, `tools/ui_styles.py`, the settings
`<option>`), the `switch` cases in `ui_style.c`/`ui_style.h` (`-Wswitch` lists every one that is missing), its
`ui_style_traits()` entry, a `css/style-<id>.css` in `WEBUI_CSS_SOURCES` and `index.html`, the translations
`settings.appearance.<id>` and `<id>_info`, the gallery's `STYLE_TITLE`/`STYLE_SUMMARY`, and any palette
tokens it needs. Removing one deletes the same entries and its rendering code; `tools/check_web.py` and the
compiler point out what was missed. Old custom theme JSON may keep fields a removed style used: the theme reader
only reads the fields it knows (`theme_palette_from_json()`), so they are ignored. The editor (`UI_STYLES` in `components/webui/www/js/config.js`) and the tools (`tools/ui_styles.py`)
repeat the lists; `tools/check_web.py` compares them with the firmware, the settings options and the translations.
Adding or removing a style changes all four places.

## Cross-style exception: value-coded fill gradients

Bars, sliders and gauge arcs that show a fill level or a value range (Solar Forecast, the Gauge widget, the car
tile's rings, ...) carry a colour gradient by value (for example gold at a low fill to green at a high one, via
`ui_style_fill_level_color()`) **in all four styles, including Flat and Matte**. This is a deliberate exception: the gradient
encodes information ("how full", "how critical"), not surface texture, so it stays regardless of which look is
active. Only the surrounding surface -- the track background, its border, whether the track itself has a shadow or
inset look -- follows the per-style rules below. A widget that puts a decorative (non-value) gradient on a plain
button or card background is not covered by this exception and should follow Flat's "no gradients" rule instead.

## Type and icons: the same in every style

The four looks differ in surfaces, shapes and effects, never in typeface or icon family: all text is Poppins and all
icons and symbols are Material Design Icons in every style. That is an architecture decision, recorded with its
reasons in [development.md](development/conventions.md#text-and-fonts).

## Shapes in code: one helper per element

Every corner radius comes from `main/ui/theme/ui_style.h`, never from a number or `LV_RADIUS_CIRCLE` written into
a widget. A literal applies the same shape in all four styles, so Flat silently keeps Glow's pills; this is how the
slider, the linear gauge, the media player's progress and volume bars, the light tile's brightness slider and the room
card chips all stayed round in Flat until they were moved to the helpers below.

| Element | Set the radius with | Flat | Glow and Glass | Matte |
| --- | --- | --- | --- | --- |
| Widget card | factory and simulator call `ui_style_apply_card()` | 8 px, 1 px outline | theme radius (20-22 px) | 12 px, no outline |
| Surface inside a card: button, chip, list row, forecast day | `ui_style_radius(r)` | at most 6 px | `r` | at most 8 px |
| Track and fill: slider, gauge, progress, brightness | `ui_style_track_radius()` for both parts | 4 px | pill | 8 px |
| Toggle switch: track, fill and knob | switch-specific radius | 4 px | pill | pill |
| Round control: slider knob, icon button, colour swatch | `LV_RADIUS_CIRCLE` directly | round | round | round |

Rules that follow from it:

- `LV_RADIUS_CIRCLE`, or a computed `height / 2`, is only for the last row. Anything else that is round in Glow uses
  a helper.
- Code that sets a radius again later (a layout pass after a resize, a state update restyling the widget) calls the
  helper again. It runs after the style was applied, so a literal there overrides it.
- Code that creates widgets without `ui_widget_factory_create()` (the desktop simulator) repeats the card treatment,
  or every style renders the same card.
- Check a shape change in all four styles with `python3 tools/generate_simulator_gallery.py` before committing.

The system screens (boot splash, OTA progress) are not styled and keep their square progress bar in every style.

## Flat (Classic)

Focused on maximum sharpness, minimal render cost and plain readability, with no depth cues.

- **Surfaces and edges**: strictly two-dimensional. `shadow_width` is hard 0 on every card (no soft or ambient
  shadow, not even a 1 px one accidentally inherited from a shared style), no light edges, no gradients on cards or
  buttons. A box is either a flat fill with a defined 1 px outline in a neutral slate grey (the theme's
  `card_border` colour, at `LV_OPA_90` -- deliberately more opaque than the same border reads in Glow, so Flat does
  not just look like "Glow with the effects switched off"), or set apart from the background by colour alone.
  Corners are crisp: cards 8 px (`UI_STYLE_FLAT_CARD_RADIUS`, where Glow and Glass use 20-22 px), surfaces inside a
  card (forecast days, list rows, buttons) and the top bar's chips and navigation buttons at most 6 px
  (`ui_style_radius()`). Round controls that are circles by design stay round.
- **Fill levels and tracks**: every track and its fill -- bars, sliders, linear gauges, the media player's progress
  and volume bars, the light tile's brightness slider and its popup colour bands, the weather forecast's temperature
  bars -- has nearly square ends (`UI_STYLE_FLAT_TRACK_RADIUS`, 4 px, via `ui_style_track_radius()`), where Glow and
  Glass draw pills. Only the knob on a track stays round. Tracks use a flat background (no inset look, no shadow); the
  solar forecast and linear gauge track is a dark grey (`0x3A3F47`). See the cross-style exception above for the fill itself. Gauge arcs keep flat, square
  caps (`arc_rounded` off).
- **Interaction**: a pressed button just swaps its fill/border colour (inverts); no raised or pressed-in motion.
- **Icons and type**: flat MDI glyphs; colour follows entity/state semantics (on/off/accent) like in every other
  style, not a fixed monochrome. Type as in every style (see "Type and icons" above).

Implementation: the Flat case of `ui_style_apply_card()` for the card border/shadow/radius, `ui_style_radius()` for
inner surfaces, `ui_style_track_radius()` for tracks (see "Shapes in code" above), and the `case UI_STYLE_FLAT` of
the exhaustive `switch (ui_style_get())` inside `ui_style_button()` and `ui_style_inset_track()` (which in Flat
only sets the 4 px radius, leaving the caller's own flat colours) in `main/ui/theme/ui_style.{h,c}`.

## Glow

A tactile, raised look on a dark background that makes buttons and indicators read as physical controls.

- **Raised structure**: containers and buttons are dark with heavily rounded corners. A vertical gradient (light on
  top, dark at the bottom) plus a soft drop shadow makes them look slightly raised, almost pressed out of the screen.
- **Glowing fill levels**: fill-level indicators sit in a dark, pill-shaped inset track (see the cross-style exception above for
  the gradient itself); the contrast between the dark track and the glowing gradient reads as if the element is lit
  from within.
- **Interaction**: pressing a button inverts/relaxes the shadow, as if it were mechanically pushed in.
- **Icons and type**: flat MDI glyphs sitting on the raised button bodies, not realistic or textured artwork -- this
  keeps the flash footprint small and avoids embedding brand logos (see [development.md](development/conventions.md#text-and-fonts)). Type
  as in every style (see "Type and icons" above).

Implementation: `ui_style_button()`, `ui_style_inset_track()`, `ui_style_gauge_arc()` in `main/ui/theme/ui_style.h`.

## Matte

Matte uses soft, opaque surfaces without simulated depth. Each colour theme supplies its own Matte
page, inactive card, active card, track and light-accent colours. Dark and Pure Black use clear
slate blocks against a near-black page; Light uses pale blue-grey surfaces and a darker amber fill.
Custom themes can set the five `matte_*` colours in the editor. Older custom themes receive a
light or dark fallback when loaded.

- **Cards:** 12 px corners, full opacity, no visible border, outline, shadow or decorative gradient.
  Inactive and unavailable entities use `matte_card_off`; active entities use `matte_card_on`.
  Their colour is reapplied after Home Assistant state changes without accumulating tint.
- **Inner controls:** at most 8 px corners. Tracks and fills use 8 px corners and a dark, flat
  background. Interactive light brightness uses a visible 32-36 px track; the wide generic dimmer
  uses 48 px. Light fills use `matte_accent`; cover controls use the muted active-card colour.
  Informational bars keep their own size. Toggle switches keep pill ends and a contrasting round knob.
- **Interaction and type:** a pressed button changes its fill without a shadow or raised motion.
  Wide Light, Climate and Slider tiles divide information and controls horizontally. Cover retains
  its open, stop and close controls; Climate retains setpoint, mode, fan and swing controls. Short
  titles are uppercased with a UTF-8-aware helper, including German `ß` → `SS`; long titles use
  an ellipsis. Widgets use Poppins Medium and MDI; notification emphasis uses Poppins Bold.

Implementation: the Matte case of `ui_style_apply_card()` after widget creation and state changes,
`ui_style_radius()` and `ui_style_track_radius()` for shapes, and the Matte cases in
`ui_style_button()` and `ui_style_inset_track()`. Layout differences are traits, not style checks:
`ui_style_traits()` gives Matte `split_wide_tiles` (the two-column Light, Slider and Climate layouts in
`light_layout_split()`, `slider_layout_split_info()` and `climate_layout_split()`), `touch_tracks` (thick slider and progress tracks), `value_beside_slider` (the value in
a fixed column next to the track), `caps_titles` (`ui_style_title()`) and `single_line_titles`, so another style can
adopt one without a new branch in the widgets. Colours work the same way: widgets ask for a role (`ui_style_page_bg()`,
`ui_style_control_bg()`, `ui_style_control_on()`, `ui_style_accent()`, `ui_style_switch_knob()`,
`ui_style_value_on_fill()`, `ui_style_slider_fill()`), passing the colour they use when the style defines none, and no widget names a style;
Matte's `matte_*` palette values are read in one place, `ui_style.c`. The other traits describe one visual property each, so any style can adopt them: `round_switches`
(switches stay pills), `frameless_page`, `plain_tracks` (the style's track radius, no outline or shadow),
`no_slider_knob` (the fill is the handle) and `adaptive_control_icons` (icon colour derived from the control's fill). The simulator applies the same card helper. A widget
marks an active card with `ui_style_card_set_active()` (light or fan on, cover open, climate not off, media
playing, vacuum cleaning, paused or returning); Matte tints it from that state, not from the card's colour.

## Glass

Translucent, floating panels with clear light reflections, as if cut from glass.

- **Surfaces and edges**: translucent containers with a light **frosting tint** -- white mixed into the card's own
  background at about 12 % (`lv_color_mix(lv_color_white(), base, 32)`, i.e. roughly 10-15 %), not just a lower
  opacity of the same plain dark colour: a uniformly darkened colour reads as "dim", a white-tinted one reads as
  "frosted glass". The light edge is **asymmetric on purpose** -- only the top and left side of the 1 px border are
  drawn (`LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT`), at `LV_OPA_40`, as if light falls across the panel from one
  direction, instead of a uniform all-around outline (which reads as a plain border, not a glass edge). Against a
  page background with no contrast of its own, translucency has nothing to show through and Glass ends up
  indistinguishable from Flat; `ui_style_glass_background()` (below) exists specifically so the content area is
  never a flat single colour in Glass. The frosting is applied again after every state update (widgets restyle their
  background) but only mixes when the widget has changed its colour since: tiles that set their background once
  (room card, car) used to lighten further with every update until they looked grey.
- **Fill levels and tracks**: tracks stay faint and near-transparent (see the cross-style exception above for the
  fill itself).
- **Interaction**: touching a button brightens the whole glass surface (a highlight/frost effect), not a mechanical
  press-in.
- **Icons and type**: MDI glyphs and text at full opacity and the brightest available colour, so they stay legible
  against the translucent surface underneath.

Implementation: the Glass case of `ui_style_apply_card()` for the card frosting/edge, `ui_style_glass_background()` for the page-level
gradient behind the cards, and the `ui_style_is_glass()` branches of `ui_style_button()` and
`ui_style_inset_track()`, all in `main/ui/theme/ui_style.{h,c}`.

## Editor layout preview

The tiles on the editor's canvas take the active colour theme, the appearance style's card and inner
shapes above and Poppins Medium, so the layout reads like the panel; the rest of the editor stays neutral.
A change to a style's shapes belongs in the firmware helpers above and in that style's
`components/webui/www/css/style-<style>.css` (Glow and Glass share `style-depth.css`). See
[editor-source.md](editor-source.md#layout-preview).

## Other references

[Text size and readability reference](text-size-readability.md): text roles, size steps and the 30 cm design basis.

A widget's internal layout (header/content/footer zones, minimum touch targets, value/unit formatting) is a
separate concern from the rendering styles above and is kept in its own document:
[widget-anatomy.md](widget-anatomy.md).

## Style differences

`tools/style_diff.py` renders the gallery, readability and Matte fixtures in every style and reports, per style
pair, the percentage of widget pixels (content area, top bar and navigation excluded) whose colour differs by
more than 8 levels: median, mean and the least different pages. Use it before adding a style and before deciding
to retire one; a pair whose median stays near zero looks alike on almost every tile. It skips the special pages
(Energy, House). On 2026-09-29: Flat/Glow median 0.7 % (0 % on several tiles), Flat/Glass 53 %, Flat/Matte 98 %,
Glass/Matte 92 %.
