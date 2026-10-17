<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md -->
# Widget anatomy

Structural rules for a widget's own layout -- independent of which of the four looks in
[appearance.md](appearance.md) is active. That file is about *rendering* (flat fills vs. gradients vs.
translucency); this one is about *where things go inside a card* and *how big they need to be to work with a
finger*. Keeping the two separate means a change to one does not need touching the other.

## Three zones, top to bottom

Every widget card follows the same rough vertical structure:

1. **Header** (optional): icon and/or widget name, pinned to the top.
2. **Content**: the reading, the arc, the picture -- whatever the widget is actually for, taking up the space in
   between.
3. **Footer** (optional): buttons, a slider, chips.

A widget that needs less than the full height centers its content in the middle zone rather than letting the header
or footer drift.

## Shared name visibility

Every current widget type with its own name accepts `show_name` (boolean, default `true`, except the weather tiles
`weather_tile` and `weather_3day`, whose default is `false`: they show the weather, not the place's name).
The editor calls it **Show name** / **Name anzeigen**, beside the Name input. It covers
both a custom `title` and the entity name used as its fallback. Hiding it frees its
reserved space; values, units, states, update ages, forecast day labels, song titles,
list rows and controls stay visible. A popup may still use the name to identify its entity.
Minimum tile sizes remain unchanged because the remaining content and touch controls need them.

The JSON reader in `widget_types.c` gives an explicit `show_name` priority over the old
sensor-only `sensor_show_label`. Missing fields show the name, while an old sensor with
`sensor_show_label: false` stays name-free. The editor migrates this flag to `show_name`
when loading/importing a layout and writes only the common field, and only when it differs from the
type's default.

Programmatic widget definitions use `hide_name` so a zero-initialized definition keeps its
name. Create the LVGL label with `ui_widget_name_create()`: it retains the preference in
the label's user flag 0. Use `ui_widget_name_set_hidden()` if a layout or state update
also hides the label, and the height/gap helpers to reclaim its space. State updates must
not reveal a name the owner disabled. Keep normal style text formatting via `ui_style_title()`.

Getting this wrong is not cosmetic, it produces real, found-on-a-device bugs:

- A label without an explicit single-line height can wrap into the zone below it instead of truncating.
  `LV_LABEL_LONG_DOT` only truncates to one line if the label *also* has an explicit height matching one line;
  without it, LVGL wraps across as many lines as an auto-sized height allows before ever truncating -- and a
  one-word label (a switch or scene entity named e.g. "Schwenken") has no space to break at, so it wraps mid-word
  instead of at a word boundary. Found repeatedly this way: the generic slider's title (a tile-sized test title
  wrapped into two lines), the gauge widget's title on a narrow vertical tile (a real dashboard's "Ladeleistung"
  gauge wrapped into "Ladeleist"/"ung" instead of truncating -- not caught by the widget gallery earlier because its
  own test titles happened to be short enough not to trigger it), the fan tile's speed/preset chips
  ("Schwenken" -> "Schwe"/"nken"), and the generic button widget's title (same word, same split, no long-mode set
  there at all).
- A value centered purely by the tile's size class, without accounting for whether the header is actually shown and
  how tall it is, can land underneath it instead of below it. Found in the sensor tile: a short, wide tile
  (220x80) with `sensor_show_label` on centered its value close enough to the middle to overlap a long title
  ("Ladeleistung") pinned at the top.

## Minimum touch target

An element a finger operates directly (a slider, a small icon button) needs an *effective* touch area of at least
about 24-32 px in the dimension the finger drags or taps across, even when the element is drawn thinner for visual
reasons (a slider bar drawn thin to leave room for other content above it is a legitimate visual choice; a *touch
target* that thin is not).

`lv_obj_set_ext_click_area()` extends the hit area past what is drawn without changing the look. Already used on
the color-temperature button and the media player's volume slider; the light tile's brightness bar (drawn 16-22 px
tall depending on tile size) got the same treatment after being found without it -- a control that is both visually
thin *and* has no extended click area is a real usability bug on a wall-mounted panel, not just a style nitpick.
In Matte the light tile draws that interactive bar 32 px in compact tiles and 36 px in wide
tiles; the generic horizontal slider uses a 48 px track in wide tiles. The extended click
area still applies. Thin informational progress bars do not grow with it.

## Aspect ratio for a fixed-direction slider

The slider widget's `slider_direction` field can be `auto` (the widget reads the tile's own width/height ratio at
render time and picks a look that fits) or a fixed direction (`left_to_right`/`right_to_left` for a horizontal bar,
`bottom_to_top`/`top_to_bottom` for a vertical fill). A fixed direction does not itself adapt to the tile's shape --
picking `bottom_to_top` on a tile that is only a little taller than it is wide renders a "vertical" slider that
reads as a plain rectangle, not a vertical bar (found via the widget gallery's own test layout: a 130x240 tile,
ratio 1.85, looked closer to square than to a fill bar). Recommended minimum, matching the horizontal case that
already looked right at 260x105 (ratio 2.48): **about 2.5:1** in the direction that matters -- width:height for a
horizontal slider, height:width for a vertical one. Not currently enforced by `layout_validate.c` (a hard rejection
there risks breaking an already-saved layout that predates this guideline); a soft clamp in the web editor's resize
handling, so a fixed-direction slider cannot be dragged past this ratio in the first place, would be the safer place
for it if this becomes a recurring problem in practice.

## Leave the title its row

A control laid out "above the title" must shrink rather than grow over the title when the tile is short. The
slider reserved 50 px of track height in both directions; a horizontal slider in a 105 px high tile then covered
its own title (invisible behind the opaque track in Flat and Glow, showing through in Glass). A horizontal track
only needs its thickness (16 px minimum now); a vertical one keeps the 50 px length.

When text next to controls does not fit, drop the words and keep the number: the cover tile at 240 px shows only
"34 %" instead of "Open ..." -- the bar and the lit up/down button still carry the state.

## Value and unit formatting

Covered by the shared formatting helpers in `main/ui/theme/ui_style.h` (or their absence, where a widget still
formats a number by hand) rather than repeated here; see the helper's own doc comment for the exact rules
(decimal places, unit spacing, the degree sign). A widget that formats a temperature, percentage or other
HA-sourced unit without going through the shared helper is the kind of thing that drifts out of sync one widget at
a time -- this is exactly how the app ended up with "21.0°C", "21.0 °C" and "37,0°C" (a comma, not even the same
decimal separator) for the same kind of value in three different widgets, found via the widget gallery's
side-by-side comparison.

The number itself follows Home Assistant's display precision for the entity (`options.sensor.display_precision`, else
`suggested_display_precision` from its entity registry entry), which the state does not carry. `main/ha/ha_display_precision.c`
reads it for the layout's sensor/number entities; `sensor` and `gauge` format through `ha_display_precision_format()`.


## Current weather

On P4, current weather and forecast tiles use the same Meteocons mapping, including the night
variant. The current tile shows today's forecast low and precipitation in a separate row when
at least 22 px remain below the condition. If precipitation amount is absent, it shows the
probability instead. A short tile places the icon beside the current temperature; taller tiles
stack them. Humidity appears in a further row when it fits. Missing forecast values are omitted.

### Panel4 forecast and value fitting

`ui_typography.h` supplies panel4's 20/24/28/36/42 roles (meta, label, value,
key, hero) with a 20 px compact floor. `ui_day_grid.h` supplies common padding/gap, width and local
calendar dates. Explicit weather day slots retain missing dates as `--`; they
do not shift later dates left. Legacy layouts keep their day counts and rain
choice. Solar alignment separates remaining yield from dated forecast values,
with all configured readings still available in its detail popup.

Energy fits complete quantities and their units in the nodes, including both
import/export or charge/discharge arrows. Its smallest reviewed complete paired
readings use 20 px. History details use 24 px summaries and readable date/value
axes; discrete history keeps a scrolling event list.
