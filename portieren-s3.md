<!-- SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 Cpt_Kirk -->

# Portierung `betta-ha-panel` → Guition CYD ESP32-S3 4848S040 (4″ / 480×480)

Stand: Mai 2026. Dieses Dokument ist der Port-Plan analog zu
[portieren.md](portieren.md) (panel4 → panel10), aber für eine **andere
SoC-Familie**: ESP32-S3 statt ESP32-P4.

> **Strategie:** Dritte Variante `panels3` parallel zu `panel4` und `panel10`
> einführen. Variant-Switch ist im Repo bereits sauber abgebildet
> (Kconfig + sdkconfig-Overlay + getrennte Treiber-Quelldateien) — wir
> erweitern ihn um einen dritten Zweig statt einen Fork zu machen.

---

## 0. TL;DR

- **Machbar**, aber deutlich aufwändiger als panel4 → panel10, weil sich der
  SoC ändert (ESP32-P4 → ESP32-S3) und damit Display-Interface, WiFi-Stack
  und HW-Beschleunigung wegfallen.
- **Kein** ESP-Hosted/C6 mehr — S3 hat natives WiFi.
- **Kein** MIPI-DSI mehr — Guition 4848S040 ist RGB parallel mit ST7701S.
- **Kein** PPA — LVGL-Tearing-Mitigation rein in Software (Bounce-Buffer +
  Direct-Mode).
- Drei klare Phasen: Skelett-Build → Treiber → Polish.

---

## 1. Hardware-Unterschiede

| Aspekt              | panel4 (P4 4″)              | panel10 (P4 10.1″)            | **panels3 (CYD 4848S040)**                  |
| ------------------- | --------------------------- | ----------------------------- | ------------------------------------------- |
| SoC                 | ESP32-P4                    | ESP32-P4                      | **ESP32-S3**                                |
| Display-Interface   | MIPI-DSI                    | MIPI-DSI                      | **RGB parallel 16-bit (ST7701S 3-wire SPI init)** |
| Auflösung           | 720 × 720                   | 1280 × 800                    | **480 × 480**                               |
| Touch               | GT911 (I²C)                 | GT911 (I²C)                   | **GT911 (I²C)** — am Gerät verifizieren, manche Chargen FT6336 |
| WiFi                | ESP-Hosted + C6             | ESP-Hosted + C6               | **nativ (`esp_wifi`)**                      |
| BT/BLE              | über C6                     | über C6                       | nativ (für jetzt aus)                       |
| PSRAM               | 32 MB Hex 200 MHz           | 32 MB Hex 200 MHz             | **8 MB Octal/Quad 80 MHz**                  |
| Flash               | 32 MB                       | 32 MB                         | **i. d. R. 16 MB** (manche Varianten 8 MB) |
| HW-Accel            | PPA                         | PPA                           | **keine**                                   |
| Speaker/Audio       | I²S                         | I²S                           | I²S (nicht relevant für MVP)                |
| SD-Slot             | ja                          | ja                            | ja (nicht relevant für MVP)                 |

Quellen: [Guition 4848S040 Wiki](https://github.com/rzeldent/esp32-smartdisplay)
sowie LVGL-Forum-Threads zu CYD ST7701S. Init-Sequenz **board-spezifisch**,
nicht generisch übernehmbar.

---

## 2. Strategie-Entscheidung: Conditional vs. Fork

### Variante A — Variant in-tree (empfohlen)
- Dritter Zweig `panels3` parallel zu `panel4`/`panel10`.
- ESP-Hosted/C6 wird per `CONFIG_APP_HAS_HOSTED_C6` (neues Kconfig) bedingt
  kompiliert: `panel4`/`panel10`=y, `panels3`=n.
- Treiber-Pärchen `display_init_panels3.c` / `touch_init_panels3.c` wie
  bisher.
- **Vorteil:** alle drei Targets bleiben dauerhaft synchron, ein Bugfix fließt
  in alle drei Builds.
- **Nachteil:** mehrere `#ifdef`-Stellen im WiFi-/Boot-Pfad.

### Variante B — Fork in eigenes Repo
- `betta-ha-panel-s3` als separate Codebase.
- **Vorteil:** keine Conditionals, schnellerer initialer Port.
- **Nachteil:** Drift garantiert, jeder Bugfix doppelt.

→ **Wir gehen Variante A.** Begründung: das Projekt hat den Variant-Switch
bereits sauber gebaut, und der Aufwand für ein zweites `idf_component.yml` +
ein paar `#ifdef`s ist kleiner als langfristige Synchronisation per Cherry-Pick.

---

## 3. Phasen-Plan

### Phase 1 — Skelett (Build kommt durch, Display dunkel)

Ziel: `idf.py -B build-panels3 ... build` läuft fehlerfrei, Firmware bootet
auf S3, Statusbar/Boot-Splash erscheint nicht (Display-Init noch Stub),
LVGL ist initialisiert mit Dummy-Display, WiFi-AP-Setup-Mode startet.

**Dateien:**

1. [CMakePresets.json](CMakePresets.json) — Preset `panels3`:
   ```json
   {
     "name": "panels3",
     "displayName": "Panel S3 4\" 480x480 (Guition CYD)",
     "generator": "Ninja",
     "binaryDir": "${sourceDir}/build-panels3",
     "cacheVariables": {
       "SDKCONFIG_DEFAULTS": "sdkconfig.defaults.s3;sdkconfig.defaults.panels3",
       "SDKCONFIG": "${sourceDir}/sdkconfig.panels3"
     }
   }
   ```
   **Wichtig:** Eigenes `sdkconfig.defaults.s3` als Basis, weil das gemeinsame
   `sdkconfig.defaults` ESP-Hosted/PSRAM-Hex/PPA enthält — alles
   P4-spezifisch.

2. [CMakeLists.txt](CMakeLists.txt) — dritter Zweig:
   ```cmake
   foreach(_sdk ${SDKCONFIG_DEFAULTS})
       if(_sdk MATCHES "panel10")
           set(_PV_SUFFIX "-10.1")
           set(_PROJECT_NAME "betta-ha-panel-10.1")
           set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.panel10.yml")
       elseif(_sdk MATCHES "panels3")
           set(_PV_SUFFIX "-s3")
           set(_PROJECT_NAME "betta-ha-panel-s3")
           set(_PANEL_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/main/idf_component.panels3.yml")
       endif()
   endforeach()
   ```

3. [main/Kconfig.projbuild](main/Kconfig.projbuild) — neuer Choice-Wert +
   abgeleitetes `CONFIG_APP_HAS_HOSTED_C6`:
   ```
   config APP_PANEL_VARIANT_S3_480
       bool "4\" 480x480 (Guition CYD ESP32-S3 4848S040)"

   config APP_HAS_HOSTED_C6
       bool
       default y if APP_PANEL_VARIANT_4INCH_720
       default y if APP_PANEL_VARIANT_10INCH_1280
       default n if APP_PANEL_VARIANT_S3_480
   ```

4. **Neu:** `sdkconfig.defaults.s3` — S3-Basis (PSRAM Octal, kein Hex/PPA,
   kein ESP-Hosted, nativer WiFi-Stack, kleinere Flash-Größe):
   ```
   CONFIG_IDF_TARGET="esp32s3"
   CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
   CONFIG_SPIRAM=y
   CONFIG_SPIRAM_MODE_OCT=y
   CONFIG_SPIRAM_SPEED_80M=y
   # ESP-Hosted komplett aus
   # CONFIG_ESP_WIFI_REMOTE_ENABLED is not set
   # CONFIG_LVGL_PORT_ENABLE_PPA is not set
   # CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR is not set
   CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y
   ```
   Plus alle gemeinsamen Defaults aus `sdkconfig.defaults` per
   *Copy-übernehmen-und-anpassen* (kein Include-Mechanismus in
   `sdkconfig.defaults`-Listen außer per CLI-Reihenfolge).

   **Alternative (sauberer):** `sdkconfig.defaults` so umorganisieren, dass
   die ESP-Hosted/PSRAM-Hex-Blöcke in `sdkconfig.defaults.p4` wandern, und
   die *wirklich* gemeinsamen Teile in `sdkconfig.defaults` bleiben.

5. **Neu:** `sdkconfig.defaults.panels3` — Panel-spezifisch:
   ```
   CONFIG_APP_PANEL_VARIANT_S3_480=y
   # CONFIG_APP_PANEL_VARIANT_4INCH_720 is not set
   # CONFIG_APP_PANEL_VARIANT_10INCH_1280 is not set
   ```

6. **Neu:** [main/idf_component.panels3.yml](main/idf_component.panel10.yml)
   — ohne `esp_wifi_remote` / `esp_hosted` / `waveshare/...`. Stattdessen:
   ```yaml
   dependencies:
     idf: ">=5.5"
     lvgl/lvgl: { version: "^9" }
     espressif/esp_lcd_touch: { version: "^1.2.0" }
     espressif/esp_lcd_touch_gt911: { version: "1.2.0~2" }
     espressif/esp_lcd_st7701: { version: "*" }   # falls verfügbar
     joltwallet/littlefs: { version: "^1.20.2" }
     espressif/esp_websocket_client: "*"
   ```
   Falls kein passender ST7701-Treiber im Registry: Init-Sequenz inline in
   `display_init_panels3.c` (siehe Phase 2).

7. [main/app_config.h](main/app_config.h) — Geometrie-Block ergänzen:
   ```c
   #elif defined(CONFIG_APP_PANEL_VARIANT_S3_480)
   #  define APP_NAME               "betta-ha-panel-s3"
   #  define APP_SCREEN_WIDTH       480
   #  define APP_SCREEN_HEIGHT      480
   #  define APP_CONTENT_BOX_WIDTH  480
   #  define APP_CONTENT_BOX_HEIGHT 360
   ```
   `APP_CONTENT_BOX_HEIGHT = 480 - 60(statusbar) - 60(navbar)`.

8. [main/CMakeLists.txt](main/CMakeLists.txt) — dritter Zweig in
   `if/elseif`-Kette für `PANEL_DRIVER_SRCS`:
   ```cmake
   if(CONFIG_APP_PANEL_VARIANT_10INCH_1280)
       set(PANEL_DRIVER_SRCS drivers/display_init_panel10.c drivers/touch_init_panel10.c)
   elseif(CONFIG_APP_PANEL_VARIANT_S3_480)
       set(PANEL_DRIVER_SRCS drivers/display_init_panels3.c drivers/touch_init_panels3.c)
   else()
       set(PANEL_DRIVER_SRCS drivers/display_init_panel4.c drivers/touch_init_panel4.c)
   endif()
   ```
   Plus REQUIRES bedingt auf `esp_hosted` / `esp_wifi_remote` machen:
   ```cmake
   set(_REQS nvs_flash bootloader_support esp_netif esp_event esp_wifi ...)
   if(CONFIG_APP_HAS_HOSTED_C6)
       list(APPEND _REQS esp_wifi_remote esp_hosted)
   endif()
   idf_component_register(... REQUIRES ${_REQS})
   ```
   Ebenso: C6-Firmware-Embed-Block (`HOSTED_C6_FW_BIN` etc.) in
   `if(CONFIG_APP_HAS_HOSTED_C6)` einklammern.

9. [main/app_main.c](main/app_main.c) — `esp_hosted_init()` & friends in
   `#if CONFIG_APP_HAS_HOSTED_C6` einrahmen. Auf S3 reicht reguläres
   `esp_netif_init()` + `esp_wifi_init()` (das macht `wifi_mgr` ggf. schon —
   prüfen ob es Hosted-Codepfade enthält).

10. **Neu:** Stub `main/drivers/display_init_panels3.c` —
    `display_init()`-Funktion meldet `ESP_OK` ohne realen Panel-Init,
    aber initialisiert `lvgl_port` mit Dummy-Display. Ziel: Boot-Loop
    vermeiden, Logs prüfen.

11. **Neu:** Stub `main/drivers/touch_init_panels3.c` — `touch_init()`
    meldet `ESP_OK` ohne realen GT911-Init.

12. [partitions.csv](partitions.csv) — neue `partitions.s3.csv` mit kleineren
    OTA-Slots (z. B. 4 MB statt 9 MB, wegen 16 MB Flash). In
    `sdkconfig.defaults.s3` referenzieren via
    `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.s3.csv"`.

**Akzeptanzkriterium Phase 1:**
- Build grün.
- Boot-Logs zeigen `WiFi STA started` ohne Hosted-Hänger.
- `idf.py monitor` zeigt Boot-Splash-Logs (auch wenn Display schwarz bleibt).

---

### Phase 2 — Treiber (Display + Touch + WiFi nativ)

#### 2.1 Display ST7701S RGB

`display_init_panels3.c` muss:

1. Per **3-wire SPI** (Bit-Bang über GPIO oder dedizierter SPI-Bus) die
   ST7701S-Init-Sequenz schicken. Sequenz ist **vendor-spezifisch** —
   die Guition-Variante ist anders als das Sunton CYD! Quelle:
   `lvgl_st7701s_4848S040` Beispiele auf GitHub, sowie LVGL-Forum-Thread
   "ESP32-S3-4848S040 RGB ST7701".
2. RGB-Panel über `esp_lcd_new_rgb_panel()` mit:
   - 16-bit parallel
   - Pixel-Clock 14–16 MHz (höher → Streifen)
   - HSYNC/VSYNC/DE-Pulses laut Datenblatt
   - **Bounce-Buffer**, Größe `H_RES * 20` Zeilen — kritisch für saubere
     Darstellung ohne PSRAM-Burst-Drops
3. LVGL anbinden via `lvgl_port_add_disp()` (nicht `_dsi`!) mit
   `flags.direct_mode = true` (mandatorisch ohne PPA).

Pin-Mapping (Guition 4848S040, **am Gerät verifizieren**):
- DE=GPIO40, VSYNC=GPIO41, HSYNC=GPIO39, PCLK=GPIO42
- R0–R4: 45,48,47,21,14
- G0–G5: 5,6,7,15,16,4
- B0–B4: 8,3,46,9,1
- ST7701-CS=GPIO39, SCL=GPIO48, SDA=GPIO47 (Sideband-SPI für Init —
  konfliktiert evtl. mit RGB-Pins, daher Init **vor** RGB-Panel-Aktivierung)
- Backlight=GPIO38 (PWM via LEDC)

#### 2.2 Touch GT911

`touch_init_panels3.c`: weitgehend Copy-Paste von
`touch_init_panel4.c`, aber:
- I²C-Pins manuell setzen (kein BSP-Header):
  - SDA=GPIO19, SCL=GPIO20 (typisch CYD; verifizieren)
  - INT=GPIO_NUM_NC, RST=GPIO38 (geteilt mit Backlight bei manchen Boards —
    achten!)
- `bsp_i2c_init()` durch eigene `i2c_master_bus_init()` ersetzen.

#### 2.3 WiFi nativ

[main/net/wifi_mgr.c](main/net/wifi_mgr.c) prüfen:
- Falls dort `esp_hosted`-Aufrufe vorkommen → in `#if CONFIG_APP_HAS_HOSTED_C6`
  klammern.
- Sonst ist `esp_wifi_init()` + `esp_netif_create_default_wifi_sta()` SoC-
  unabhängig.

`sdkconfig.defaults.s3` muss `CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y` setzen
(Setup-AP). In den P4-Defaults steht das bewusst auf `n`, weil dort der C6
keinen AP-Mode kann.

**Akzeptanzkriterium Phase 2:**
- Boot-Splash sichtbar.
- BETTA-Setup-AP startet, Browser kann sich verbinden.
- Touch in Setup-Wizard funktioniert (kein Achsen-Tausch).
- Nach WiFi-Login: Statusbar zeigt Uhrzeit, leere Page lädt.

---

### Phase 3 — Polish & Feature-Reduktion

#### 3.1 Speicher-Budget prüfen

S3 + 8 MB PSRAM ist deutlich enger als P4 + 32 MB Hex. Wahrscheinlich zu
deaktivieren in `sdkconfig.defaults.s3`:

- `CONFIG_LV_USE_LOTTIE=n` (ThorVG + Lottie zieht ~600 kB Binary + Heap)
  **Hinweis (verifiziert):** Lottie/ThorVG läuft problemlos auf S3 — nur langsamer.
  ThorVG rastert vollständig in Software. Erwartete Renderzeit pro Frame: ~40–80 ms
  (vs. ~16–20 ms auf P4 mit PPA), also ~12–25 fps statt 30+. Für Wetter-Animationen
  akzeptabel. Empfehlung: in Phase 2 mit `CONFIG_LV_USE_LOTTIE=y` testen;
  bei spürbaren Rucklern Lottie-Refresh auf 15 fps drosseln statt komplett deaktivieren.
- `CONFIG_LV_USE_THORVG=n` falls Lottie raus → Vector-Graphic auf
  reine LVGL-Vektoren beschränken
- ggf. `CONFIG_LV_USE_VECTOR_GRAPHIC=n`
- Font-Auswahl reduzieren: nicht alle Montserrat-Größen, nur 16/22/28
- Weather-Icon-Lottie-JSONs: prüfen ob sie auch ohne Lottie-Player als
  PNG dargestellt werden können

#### 3.2 LVGL Performance ohne PPA

- `CONFIG_LV_DEF_REFR_PERIOD=33` (30 Hz statt 60 Hz)
- `CONFIG_LVGL_PORT_ENABLE_PPA=n` (gibt's auf S3 sowieso nicht)
- Direct-Mode + Bounce-Buffer (siehe 2.1) ist Pflicht.
- LVGL-Task-Stack ggf. auf 16 kB statt 24 kB (RAM-Druck).

#### 3.3 Web-UI

Aktuell sind Canvas-Größen in [components/webui/www/app.js](components/webui/www/app.js)
und [styles.css](components/webui/www/styles.css) hart kodiert (1280×680
oder 720×600). Drei Optionen:

- **Runtime-Injection (sauber):** `/api/version` liefert bereits
  `screen_width/height` — App.js liest das beim Start und setzt
  `CANVAS_WIDTH/HEIGHT` daraus. Funktioniert dann für alle drei Varianten.
- **Build-Time-Replacement:** Vor dem Embed `app.js`-Konstanten via `sed`
  ersetzen (mehr Build-System-Magie).
- **Drei separate Bundles:** schlecht, blowt das Repo auf.

→ Variante 1 implementieren (kommt auch panel10 zugute).

#### 3.4 Layouts

Bestehende Layouts vom 4″/10.1″ Panel passen nicht — Widgets sind relativ
zum jeweiligen Canvas. Optionen:
- Quick-Setup-Default-Layout für 480×360 in Code generieren.
- Optional: Skalierungs-Tool in [tools/](tools/) (lineare Skalierung
  720×600 → 480×360 mit Faktor 0.667).

#### 3.5 OTA

- Eigene OTA-Binary `betta-ha-panel-vX.Y.Z-panels3.ota.bin`
- Eigene Factory-Image `*-panels3.factory.bin`
- [tools/make_factory_bin.ps1](tools/make_factory_bin.ps1) um Variant
  `panels3` erweitern.
- Auf 16 MB Flash sind die OTA-Slots kleiner → Image-Size beim ersten
  Release aufmerksam beobachten, ggf. Lottie/ThorVG ganz raus.

**Akzeptanzkriterium Phase 3:**
- Voll funktionale Dashboards, alle Widget-Typen außer ggf. Lottie-
  basierten Wetter-Animationen.
- Web-Editor zeigt korrekt 480×480-Canvas.
- OTA durch Editor funktioniert.

---

## 4. Stolperfallen (vorab dokumentiert)

- **ST7701S-Init-Sequenz ist board-spezifisch.** Eine generische Sequenz
  wird funktionieren, aber Farben/Gamma sind dann falsch. Auf die exakte
  Guition-Sequenz (Hersteller-PDF oder LVGL-Forum) bestehen.
- **Pin-Konflikt RGB ↔ ST7701-SPI-Init.** Manche Pins werden für die
  Init-Sequenz als SPI verwendet und danach als RGB-Datenleitungen
  umgemappt. Reihenfolge in `display_init_panels3.c` strikt einhalten:
  GPIO als SPI → Sequenz schicken → SPI freigeben → RGB-Panel anlegen.
- **Touch-RST geteilt mit Backlight** (boardabhängig). Wenn Backlight aus
  geht, geht Touch mit aus. → Workaround: PWM nie auf 0% ziehen, Minimum
  bei 1%.
- **PSRAM Octal vs. Quad** unterscheidet sich je Charge des CYD.
  `CONFIG_SPIRAM_MODE_OCT` mit Quad-PSRAM = bricked Boot. Im Zweifel auf
  `CONFIG_SPIRAM_MODE_QUAD` zurückfallen.
- **`dependencies.lock`** vom P4-Build darf nicht ins S3-Build überschwappen
  (siehe Lessons in [portieren.md](portieren.md#71-build-system)). Vor
  erstem Build löschen + `idf.py set-target esp32s3`.
- **`$env:IDF_TARGET`** vor Build setzen.
- **GT911 vs. FT6336**: einige CYD-Chargen haben FT6336 mit anderer
  I²C-Adresse (0x38 statt 0x5D). Im Zweifel I²C-Scan erst.
- **Tearing ohne PPA**: ohne Direct-Mode + Bounce-Buffer ist das Bild
  unbrauchbar. Nicht einfach DSI-Codepfade kopieren.
- **`esp_lcd_st7701`-Komponente**: existiert im Registry, aber für
  unterschiedliche Boards. Falls Init-Sequenz nicht passt → eigene
  Init-Routine schreiben (paar Dutzend Register-Writes).
- **WiFi-Performance**: nativer S3-WiFi ist langsamer als Hosted-C6
  (Single-Antenne, Single-Stream). WebSocket zu HA bleibt stabil, aber
  initiale `get_states` für 256 Entities dauert spürbar länger. WS-Buffer
  evtl. anheben.

---

## 5. Offene Entscheidungen vor Start

- [ ] Variante A (in-tree) bestätigt? → ja, Empfehlung.
- [ ] Hardware physisch da? Welche **exakte** CYD-Charge (8 MB / 16 MB Flash,
      Octal-/Quad-PSRAM, GT911/FT6336)?
- [ ] Welche Features dürfen wegfallen? Lottie-Wetteranimationen (läuft auf S3,
      aber mit ~15–25 fps; kein zwingender Ausschluss), Roborock-Live-Map
      (RAM-intensiv prüfen), ThorVG?
- [ ] OTA-Server-Manifest erweitert um `panels3`-Channel?
- [ ] Release-Versionierung: weiter `v0.8.x` synchron oder eigener
      `v0.8.x-s3` Tag?

---

## 6. Empfohlene Port-Reihenfolge (kompakt)

1. Branch `variant-s3-port` von `main`.
2. **Phase 1:** Build-Skelett (Abschnitt 3.1–3.12). Akzeptanzkriterium:
   `idf.py -B build-panels3 ... build` grün, bootet ohne Crash.
3. **Phase 2.1:** Display-Treiber. Akzeptanzkriterium: Boot-Splash sichtbar.
4. **Phase 2.2:** Touch. Akzeptanzkriterium: Tap auf Boot-Splash dimmt
   Backlight nicht weg / Setup-Buttons reagieren.
5. **Phase 2.3:** WiFi-AP-Mode + Setup-Wizard. Akzeptanzkriterium:
   Provisioning kommt durch.
6. **Phase 3.1–3.2:** Speicher/LVGL-Tuning bis stabile 30 fps + kein
   Heap-Crash bei vollem Dashboard.
7. **Phase 3.3:** Web-UI Runtime-Canvas-Size (kommt panel10 auch zugute).
8. **Phase 3.4:** Default-Layout 480×360.
9. **Phase 3.5:** Factory/OTA-Build, erstes Release.

---

## 7. Minimaler Kickoff (PowerShell)

```powershell
cd C:\Users\chris\Documents\Smart86Box\betta-ha-panel
git checkout -b variant-s3-port

# Phase 1: leere Stub-Dateien anlegen (Inhalt aus Abschnitt 3)
New-Item main\drivers\display_init_panels3.c -Type File
New-Item main\drivers\touch_init_panels3.c -Type File
New-Item main\idf_component.panels3.yml -Type File
New-Item sdkconfig.defaults.s3 -Type File
New-Item sdkconfig.defaults.panels3 -Type File
New-Item partitions.s3.csv -Type File

# … dann Edits laut Abschnitt 3 …

# Set-target + erster Build (im ESP-IDF Terminal):
# idf.py kennt kein --preset; CMake-Preset-Parameter werden als -D-Flags übergeben.
$env:IDF_TARGET = "esp32s3"
Remove-Item dependencies.lock -ErrorAction SilentlyContinue
idf.py -B build-panels3 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.s3;sdkconfig.defaults.panels3" -D SDKCONFIG="sdkconfig.panels3" set-target esp32s3
idf.py -B build-panels3 -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.s3;sdkconfig.defaults.panels3" -D SDKCONFIG="sdkconfig.panels3" build
```

---

## 8. Referenzen

- [portieren.md](portieren.md) — panel4 → panel10 Port (mit Lessons Learned).
- [CMakePresets.json](CMakePresets.json) — Variant-Switch-Pattern.
- [main/Kconfig.projbuild](main/Kconfig.projbuild) — Variant-Choice-Pattern.
- [main/drivers/display_init_panel10.c](main/drivers/display_init_panel10.c)
  — Vorbild für variant-spezifischen Display-Treiber.
- ESP-IDF [esp_lcd RGB](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html)
- LVGL Forum Threads zu CYD ESP32-S3-4848S040 (Suche im LVGL-Forum nach
  "4848S040 ST7701").
