<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -->

# Hardware diagram sources

Raster illustrations updated on 2026-10-04 with the built-in Imagegen tool. These are documentation illustrations, not live telemetry. Text equivalents and measurement caveats are in the [display device guides](../../choose-display.md), [CPU/memory reference](../../hardware/resources.md) and [storage reference](../../hardware/storage.md).

## Consistent display and audio information (2026-10-06)

The four device illustrations were edited with the built-in Imagegen tool to
add matching bottom information cards: **Display** (size and resolution) and
**Speaker / Speaker output** (hardware connection and BETTA support).

| Image | Display card | Audio card |
| --- | --- | --- |
| `panel4.png` | 4-inch · 720 × 720 | External speaker connector · BETTA chimes |
| `tab5.png` | 5-inch · 1280 × 720 (landscape) | Built-in 1 W speaker · BETTA chimes |
| `panel10.png` | 10.1-inch · 1280 × 800 | External speaker connector · BETTA audio disabled |
| `paneljc.png` | 10.1-inch · 1280 × 800 (landscape) | External speaker connector · BETTA audio disabled |

The Tab5 and JC values describe the landscape orientation shown, rather than
the panels' native portrait scan order. Audio support does not establish
audible output: the 4B driver path was exercised, while only a quiet Tab5 chime
was heard; see [peripheral evidence](../../hardware/peripherals.md).

References: [Waveshare LCD-4B](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B)
(speaker header), [Waveshare LCD-X](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X)
(speaker header), [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5)
(built-in 1 W speaker), and the
[Guition datasheet](https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961095913/JC8012P4A1C_I_W%20Specifications-EN-v1.1%282%29.pdf)
(page 5, interface 11: Speaker Interface).

The previous JC illustration's circular speaker grille and its leader were
removed: the cited manufacturer interface diagram establishes an internal
speaker connection, not an integrated case loudspeaker. The JC native USB
callout no longer promises the UART serial console; that console is labelled
at USB-UART. The Tab5 camera callout now states that sensing is off by default.

Shared edit prompt specification (each existing image was its own target):

```text
Precise hardware-guide edit. Preserve white background, 1536 × 1024 aspect
ratio, shaded device geometry, blue rounded callouts and navy English text.
Add two matching bottom information cards with readable Display and
Speaker/Speaker output headings and the exact size/resolution/audio wording
from the table above. Cards have no physical leader lines. Modestly reflow
existing groups to make space without changing port/button order or leader
targets. No extra components, tiny text, overlap or cropping. Preserve the
correct Tab5 rear battery/legend geometry and Waveshare LCD-X blue PCB pair.
For JC remove the invented rear speaker grille and its old Speaker callout;
leave a smooth case surface, with external-speaker output only in the info
card. Label native USB Full Speed as power/flashing, and USB-UART as
power/flashing/UART serial log. For Tab5 use Camera body text:
Room-light sensing; off by default.
```

## Waveshare LCD-X 10.1-inch correction (2026-10-05)

`panel10.png` was corrected with the built-in Imagegen tool. It replaces the
earlier Nano-board illustration with the blue LCD-X main board and narrow I/O
board. The flashing callout identifies **USB TO UART**, the upper USB-C port.
The lower port is USB OTG. The display shape follows the 10.1-inch dimensions.
This is a schematic illustration, not a firmware compatibility or device-test
claim; the current `panel10` source still uses the inherited Nano BSP.

References: [LCD-X manufacturer guide](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X#esp32-p4-wifi6-touch-lcd-101),
[controller diagram](https://docs.waveshare.com/assets/images/ESP32-P4-WIFI6-Touch-LCD-7-details-intro-ef6edd5aef7620cef6e7e68953930c46.webp)
and [10.1-inch dimensions](https://docs.waveshare.com/assets/images/ESP32-P4-WIFI6-Touch-LCD-10.1-details-size-54557c018e34373f91d098a46c6763cc.webp).

Edit prompt (existing `panel10.png` was the target):

```text
Use case: precise-object-edit / scientific-educational. Image 1 is the EDIT TARGET: BETTA HA Panel's old panel10 illustration. Image 2 is the authoritative Waveshare ESP32-P4-WIFI6-Touch-LCD-X controller hardware diagram; Image 3 is the authoritative 10.1-inch panel shape/dimensions. Correct image 1 to depict the actual 10.1-inch LCD-X kit, while preserving the white background, clean softly shaded product illustration style, navy type, light blue rounded callouts and 1536x1024 composition. Replace the WRONG black Nano PCB (Ethernet and USB-A) completely. The actual controller in image 2 has a BLUE almost rectangular main board on the RIGHT and a narrow BLUE I/O daughterboard on its LEFT, linked by two short horizontal cables (upper FPC ribbon and lower power cable) across a small gap. Main PCB: central silver square P4 core/module, small shielded C6 module upper right, a vertical black expansion header down the LEFT of the main PCB, top camera FPC, right-side touch and DSI FPC connectors, microSD lower right. Daughterboard: left edge power switch near upper third, BOOT and RESET small buttons below switch, two stacked USB-C ports toward the lower half. The UPPER USB-C is USB TO UART, used for power/flashing/serial; LOWER USB-C is USB OTG and must NOT be the flashing leader target. No Ethernet, no USB-A, no black Nano controller. Preserve recognizable blue PCB geometry and relative part positions from image 2; do not invent pin numbers or component labels. On the LEFT show the black front of the 10.1-inch display matching image 3: slightly taller outer bezel, rounded corners, blank dark 1280x800 screen; outer proportions approximately 250.2 wide by 169 high. To its right show an enlarged top-down explanatory view of the correct blue controller assembly. A simple illustrative DSI ribbon connects the main board's RIGHT-side DSI connector to the display; route visibly behind/around the board, not into the camera connector. Keep four readable callouts with exact English text: '10.1-inch display' / '1280 × 800'; 'USB TO UART' / 'USB-C: power, flashing and serial log.'; 'BOOT' / 'Hold for download mode.'; 'RESET' / 'Restarts the panel.' Leaders must point precisely to the correct parts on the narrow I/O daughterboard. BOOT leader above RESET leader. Text should be large, sharp and legible with generous whitespace. No numbered circles from the source, no huge tiny wiring legend, no extra device or invented sensor. This is a schematic documentation illustration, not an assertion of firmware hardware compatibility.
```

Localized connector/leader correction prompt:

```text
Precise localized hardware-label correction. Image 1 is the EDIT TARGET just generated. Image 2 is the authoritative Waveshare daughterboard reference. Keep the display, main blue PCB, DSI cable, background, lighting and general composition completely unchanged. Correct ONLY the narrow blue daughterboard and the three callout leaders/box placement. The real daughterboard has, from top to bottom on its LEFT EDGE: power slide switch; exactly two small round push buttons (BOOT above RESET); exactly TWO stacked USB-C receptacles (USB TO UART above USB OTG). In image 1 the two correct BOOT/RESET buttons are approximately at (1028,535) and (1028,579). The upper existing USB-C receptacle is approximately at (1027,636). BELOW that port, remove the two wrongly invented round pushbuttons at roughly (1027,690) and (1027,733) and draw ONE second USB-C receptacle centered roughly (1027,705), same metal USB-C shape and orientation as the upper one. There must be only TWO pushbuttons total, both ABOVE the TWO USB-C ports. Reroute the BOOT leader to the actual upper pushbutton near y535, RESET leader to actual lower pushbutton near y579, and USB TO UART leader to the upper USB-C near y636. The current BOOT/RESET endpoints are wrong and must change. Reflow the three callout boxes in the whitespace immediately left of the daughterboard to avoid crossing other text, arranging them in physical top-to-bottom order BOOT, RESET, USB TO UART if needed. Exact text stays 'BOOT' / 'Hold for download mode.'; 'RESET' / 'Restarts the panel.'; 'USB TO UART' / 'USB-C: power, flashing and serial log.' All text remains large and readable; no extra USB OTG callout needed. Preserve the 10.1-inch display label and all other pixels/components as closely as possible. Match image 2's actual connector ordering, not invented parts.
```

## Tab5 rear geometry correction (2026-10-05)

`tab5.png` was edited with the built-in Imagegen tool. The reference was the
maintainer's photograph supplied on 2026-10-05 (rear with ST7121 legend);
the reference photo is not redistributed. The corrected illustration places
the gray battery holder on the left, the printed electronics legend on the
right, and the vertical expansion socket between them. The battery callout
points into the holder. The front and its connector callouts are retained.
Fine electrical labels are schematic; use the manufacturer's documentation
for wiring.

Edit prompt:

```text
Use case: precise-object-edit. Image 1 is the edit target: BETTA HA Panel Tab5 hardware illustration. Image 2 is the owner's photograph of the REAL REAR and is the authoritative geometry reference. Edit ONLY the lower rear-view device in image 1 and the endpoint of its Battery slot leader. Keep the upper front-view device, camera, USB-A/USB-C callouts, text, background, navy/blue colors, canvas proportions and clean illustration style unchanged. Correct the rear: in landscape, the tall dark-gray NP-F550 battery holder occupies the LEFT roughly 40% of the back, not the whole back. The RIGHT roughly 60% is a flat pale gray/white printed electronics/pin legend with colorful small blocks and recognizable placement from image 2. Between them is a vertical black two-column multipin expansion socket. The red side button is on the LEFT outer edge about one-third down. Keep the white frame and gold fasteners. Below the battery area, show horizontal 'TAB5' branding and plus/minus symbols as in the reference. Right edge has USB-A at upper-middle, USB-C below it and headset jack lower down, matching the reference. The printed rear legend can be simplified: do not invent detailed electrical pin numbers or fake readable wiring; keep the broad colored patch layout and major TAB5/M5STACK identifiers. Match the existing lower object's mild perspective, studio shading, size and position. Move the blue Battery slot leader's endpoint so it points INTO the LEFT dark-gray holder, never the electronics legend. Keep exact external callout text: 'Battery slot' / 'Optional NP-F550 battery.' All other existing labels and layout remain untouched. White opaque background; preserve original 1536 by 1024 aspect ratio.
```

## Current flash layout

[flash-partitions.png](flash-partitions.png) now shows only the adopted layout. The maintainer reports that the transition has been completed; the legacy before/after comparison has been removed from the image.

Sources: `config/partitions.csv`, `config/partitions.paneljc.csv`, `config/partitions.tab5.csv`.

- Both profiles: two 6.5 MiB OTA apps, 256 KiB coredump and 24 KiB NVS. LittleFS starts at 13.375 MiB, with 2.625 / 18.625 MiB capacity.
- Both maps share a 0–32 MiB scale. The 16 MiB map stops at the midpoint, with white space to its right. The common app and coredump boundaries align across profiles. Only file-storage capacity differs.
- Tiny segment widths are schematic at raster resolution; printed capacities and the source CSVs are authoritative.
- The 128 KiB prefix includes boot regions, NVS, OTA/PHY metadata and alignment gaps; it is not one partition. The C6 update image is inside each P4 app; the C6 executes from its own flash.
- The image describes storage roles and capacities. It does not establish that every board or failure case has been tested.

Use matching category colors in bar segments and explanation boxes:

| Category | Palette specification |
| --- | --- |
| Firmware / OTA apps | Blue `#79CFF2` |
| LittleFS files | Teal `#63C7CE` |
| Crash report / coredump | Amber `#FFC857` |
| Boot + metadata, including NVS | Purple `#B49DF4` |

## Memory baseline bar charts

[memory-baseline.png](memory-baseline.png) uses independent horizontal metric bars, not a stacked allocation budget.

- Physical capacity: the ESP32-P4's internal high-performance SRAM pool (HP L2MEM) is 768 KiB. This is distinct from the runtime heap, external PSRAM and the separate low-power/RTC memory. Source: [Espressif ESP32-P4 v1.3 datasheet](https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_datasheet_en.html), memory organization.
- Internal heap: 2026-10-01 notes in [development.md](../../development.md), with a common 0–400 KB scale. Panel4 / paneljc: free after boot 329 / 321 KB; largest free block 152 / 163.8 KB; lowest free heap in stress runs 165–226 / 184 KB. The panel4 range spans different runs. Largest free block is part of free heap, and stress readings are from another time.
- Free PSRAM: separately dated 2026-09-25 snapshots from [the CPU/memory reference](../../hardware/resources.md), 17.9 MB for panel4 and 12.5 MB for paneljc, on a separate 0–32 MB scale. White chart space is not a measured used-memory category. Different firmware versions/layouts were involved.
- KB and MB retain the source measurement units; do not silently relabel them KiB/MiB. The physical SRAM label uses KiB. Installed PSRAM capacity is 32 MB; reported internal heap is about 576–591 KB.
- Role annotations (HA cache about 8 MB, JC camera buffers about 3.75 MB when active) are examples, not complete accounting.
- The 32 KiB running text log is volatile PSRAM. The RTC restart breadcrumb survives warm reset but not power loss and is recorded in NVS at next boot. NVS, LittleFS and coredump are persistent flash partitions.
- No measured baseline is assumed for panel10 or Tab5.

When tables or measurements change, regenerate the affected image and update captions, text equivalents and prompts. Check capacities, dates, units, segment order, aligned OTA boundaries, common endpoints and readable explanations. Keep the current-layout image focused on the adopted design; retain historical migration details in text.

## Current flash generation prompt

```text
Create an accurate clean English hardware infographic for BETTA HA Panel, white background, navy text and large readable flat colored boxes. Title "Flash storage: current layout". Subtitle "BETTA HA Panel • 16 and 32 MiB boards".
Main visual: TWO horizontal flash partition bars on ONE shared linear0–32MiB ruler. Upper row "32 MiB / panel4, panel10". Lower row "16 MiB / paneljc, paneljc-v2, tab5". Use common left edge,32rightedge,midpoint16dashedline. Lower16rowstopsatmidpointandright halfiswhite.
DRAW BOTH ROWS USING THE SAME PREFIX AND APP RECTANGLES COPIED VERTICALLY. Only length of the final Files rectangle differs. Their boundaries must align perfectly vertically:
32row = Boot+metadata0.125 +OTAA6.5 +OTAB6.5 +Coredump0.25 +Files18.625.
16row = Boot+metadata0.125 +OTAA6.5 +OTAB6.5 +Coredump0.25 +Files2.625.
Thus Files starts at13.375MiB on BOTH rows, clearly to LEFT of the16midpoint. The32Files block crosses16midpoint andcontinues32. DO NOT start32Files at16. Both apps6.5areexactlysamephysicalwidth,bothrows. Each6.5 app occupies20.3125% of the full32MiB width.
Use these shared boundaries relative to the full32 ruler: left0%;purpleprefixends0.390625%;OTAAends20.703125%;OTABends41.015625%;ambercoredumpends41.796875%;16Filesends50%;32Filesends100%. Label little segments OUTSIDE the bars to avoid widening rectangles for text. Files16 label2.625MiB may go below with a leader.
Showexactprintedvalues: OTA A6.5MiB,OTA B6.5MiB;Coredump256KiB;Files18.625MiBupper,Files2.625MiBlower;Boot+metadata128KiB. No other maps. No before/after, legacy, factory,unusedgap or proposal.
Underbars: "Same 6.5 MiB OTA slots; 32 MiB boards add 16 MiB for files." / "Boot + metadata: 128 KiB, including 24 KiB NVS."
Below four equally readable explanationboxes sidebyside. Use exactly matching color fills for corresponding barsegments AND cards: blue#79CFF2firmware;amber#FFC857crash;teal#63C7CEfiles;purple#B49DF4boot/NVS.
Firmware slots: "Panel app, editor/assets and bundled C6 update. OTA A/B keep running and alternate versions. A rollback-capable bootloader can return to the previous app after a failed pending update."
Crash report(coredump): "Latest panic snapshot: which task failed and where. Retained after restart for diagnosis. Separate from restart history."
Files(LittleFS): "Dashboard layout, non-secret settings, custom themes/translations and graph history."
NVS inside boot+metadata: "Wi-Fi/MQTT passwords, HA token, admin password hash and last 8 restart entries. 24 KiB on both profiles."
Footer: "Running text log: 32 KiB in PSRAM; lost after restart." / "C6 runs from its own flash; its update image is bundled in the P4 app."
No hexaddresses,no gradients,no chips. Geometrymust obeysinglecommonscaleandidenticalOTA/dumppositionsinbothrows.
```

## Memory generation prompt

```text
Use case: infographic-diagram. Regenerate memory-baseline.png as an English documentation infographic for BETTA HA Panel, with HORIZONTAL BAR CHARTS as the main visual instead of memory-chip drawings/tables. Match a companion flash infographic: white background, navy typography, crisp flat artwork, generous whitespace, blue#79CFF2 / teal#63C7CE / amber#FFC857 / purple#B49DF4, square or portrait. Readable labels outside bars, no tiny crowded text.

Title "RAM and PSRAM: measured headroom"
Subtitle "BETTA HA Panel • dated baseline, not a live reading"

Top section "Internal RAM • measured 2026-10-01".
Small text "Reported internal heap: about576–591KB. Wi-Fi/SDIO, TLS and internal task stacks need it."
Draw SIX horizontal independent metric bars, arranged as3 paired groups. ALL six share one linear0–400KB ruler; larger values proportionally longer. Blue=panel4; teal=paneljc. Put exact numerical labels beside bar ends. The metric groups are NOT stacked allocation buckets:
"Free after boot": panel4 329KB, paneljc321KB.
"Largest free block": panel4 152KB, paneljc163.8KB.
"Lowest free heap in stress runs": panel4 165–226KB (solid bar to165, hatched/translucent extension to226 clearly marked as range); paneljc184KB.
Two small blue/teal legend cards use the EXACT same solid fills as their respective board bars.
Note "Largest block is part of free heap. Stress readings were taken at another time."
Do not subtract these metrics or add them together. Bars begin at0. Do not depict the reported heap range as a precise total capacity or invent used-memory amounts.

Middle section "Free PSRAM • older snapshot, 2026-09-25".
TWO horizontal bars on a SEPARATE common0–32MB ruler, bluepanel4 free17.9MB and tealpaneljc free12.5MB. Only free amounts are colored; unused chart background is white, not a claimed 'used' category.
Text "32MB PSRAM installed • 200MHz Hex PSRAM."
Note "Different firmware versions/layouts; these are not the October baseline. Runtime heap can be smaller than installed capacity."
Small role annotation, not a stacked budget: "PSRAM holds LVGL/display/images, HA state cache (~8MB), and JC camera buffers (~3.75MB when sensing is active)."
Retain source units KB/MB for measurements; do not relabel as KiB/MiB. No invented complete allocation accounting.

Bottom section "What survives a restart?"
THREE concise matched-color explanation cards:
Blue/teal "PSRAM • running text log": "32KiB ring buffer; lost after restart."
Purple "RTC RAM • restart breadcrumb": "Cause, uptime and heap readings. Survives warm reset; lost on power loss. Saved to NVS at next boot."
A small persistent-flash strip with EXACT same category colors as the companion flash infographic: purple"NVS", teal"LittleFS", amber"Coredump", each followed by matching-color description boxes or labels:
NVS "Credentials and last8 restarts"
LittleFS "Layout, non-secret settings, custom files and graph history"
Coredump "Latest panic snapshot for crash diagnosis"
Small "Persistent stores are partitions in flash, separate from RAM."

Footer "Approximate readings; workload and features change headroom. No baseline assumed for panel10 or tab5."
No dates or numbers beyond those specified, no chip illustrations as the dominant visual, no hex addresses, no pie charts, no fabricated per-task RAM budget. Main presentation MUST be bar charts with clear independent scales and paired board comparison.
```

## ESP32-P4 SRAM label edit prompt

```text
Edit this existing English BETTA HA Panel memory infographic. Make the physical ownership and capacity of internal SRAM explicit while preserving all measured values, dates, paired bar charts, colors, scales, PSRAM section, persistence explanations and footer.
Replace the top section header "Internal RAM • measured 2026-10-01" with the large header "ESP32-P4 internal SRAM".
Immediately below it add a clearly readable line: "768 KiB on-chip HP SRAM • measured heap baseline: 2026-10-01".
Retain the next line: "Reported internal heap: about 576–591 KB. Wi-Fi/SDIO, TLS and internal task stacks need it."
Add or integrate a short note near that line: "Physical SRAM capacity is larger than the runtime heap."
Reflow or slightly extend the top panel vertically if needed; do not shrink text into tiny type.
The768KiB is the P4's physical high-performance on-chip SRAM pool, NOT free RAM, NOT heap capacity, NOT external PSRAM, NOT the C6's RAM, NOT the separate RTC RAM.
Keep six independent bars on a common0–400KB axis:
freeboot panel4 329KB,paneljc321KB;largestfreeblock panel4 152KB,paneljc163.8KB;stress panel4solid165KB hatchedto226KB,paneljc184KB.
Keep PSRAM older2026-09-25 with32MBinstalled200MHzHex andseparate0–32MB axis free17.9MBpanel4,12.5MBpaneljc. Keep all existing allocation-example caveats. Measurement units KB/MB stay unchanged; only the new physical SRAM capacity is labelled768KiB.
Keep lower explanation cards: PSRAM textlog32KiBringbufferlostafterrestart;RTCbreadcrumbcauseuptimeheapwarmresetnotpower,NVSnextboot;persistentNVScredentials8restarts,LFSconfigurationcustomfilesgraphs,coredumpmostrecentpanic.
Do not invent used-memory percentages or new measurements. Do not change any dates or existing numbers. Preserve clean navy/blue/teal style and legibility.
```

## Memory explanation cleanup prompt

```text
Edit this image with ONE change only: remove the entire two-line paragraph starting "The 768KiB is the P4's physical high-performance on-chip SRAM pool, NOT free RAM, NOT heap capacity," and ending "NOT external PSRAM, NOT the C6's RAM, NOT the separate RTC RAM." Delete this paragraph completely. Leave white space or move the chart up slightly to close the gap. It is unwanted duplicated explanation.
Keep the header "ESP32-P4 internal SRAM", the line "768 KiB on-chip HP SRAM • measured heap baseline: 2026-10-01", reported heap576–591KB and the short line "Physical SRAM capacity is larger than the runtime heap." Keep all remaining text, every measurement, all bars, scales, dates and sections unchanged. Do not add any new sentences.
```

## Flash endpoint correction prompt

```text
Make ONLY one localized geometry correction in this image. Keep the title, both bar prefixes, identical OTA A/B geometry, coredump blocks, every explanation card, all values and footer unchanged.
In the LOWER16MiB row, the teal Files block wrongly extends past the dashed16MiB line. Trim its RIGHT EDGE to end EXACTLY on the dashed16MiB line (x769 on this1536pximage). Its LEFT EDGE stays aligned to the upper teal Files left edge (x722). The small lower teal Files rectangle is now x722–769. Remove the navy outline/box from the right half of this lower row; x769–1498 must be completely white empty page with no horizontal border. Do not move the midpoint, upper bar or any other colored rectangle.
Remove the text INSIDE this tiny lower teal rectangle. Place "Files 2.625 MiB" above or below it, with a short leader line to x745. The label may occupy the blank area to the right. Do NOT widen the teal block to fit text.
Upper Files18.625MiB stays unchanged and extends to32. Both16and32profilemetadata/apps/coredumpboundariesremainidentical. This is ONLY a lowertealendpoint correction andexternal label placement.
```

## Memory heading typo correction

Corrected `What surviyes a restart?` to `What survives a restart?` with the built-in Imagegen tool. Only the generated replacement letter was composited into the original at x=243–264, y=1132–1164. Pixel comparison confirmed zero changes outside that 22 × 33 pixel rectangle; all other source pixels were retained.

```text
Edit this image with exactly ONE TYPO FIX. In the bottom section heading, replace "What surviyes a restart?" with "What survives a restart?". Only the erroneous lowercase "y" in "surviyes" becomes a lowercase "v". Match the existing heading's exact font, size, weight, navy color, baseline and kerning. Do not rewrite or redraw the other letters. Do not reflow anything.
Every pixel outside this one letter must remain identical to the supplied original. Keep all bars,values329/321KB,152/163.8KB,165–226/184KB,17.9MB,12.5MB,768KiB,dates,boxes,footers,borders,colors,fontsandlayout unchanged. This is a single-glyph typo correction, not a regeneration.
```
