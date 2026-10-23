/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/i18n_store.h"
#include "util/atomic_file.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *I18N_BUILTIN_DE =
    "{\"lvgl\":{"
    "\"common\":{\"on\":\"AN\",\"off\":\"AUS\",\"unavailable\":\"nicht verfügbar\",\"paused\":\"pausiert\",\"playing\":\"spielt\",\"start\":\"Start\",\"pause\":\"Pause\",\"unknown\":\"Unbekannt\"},"
    "\"cover\":{\"open\":\"Offen\",\"closed\":\"Geschlossen\",\"opening\":\"Öffnet\",\"closing\":\"Schließt\",\"moving\":\"Fährt\"},"
    "\"datetime\":{\"date\":\"Datum\",\"time\":\"Uhrzeit\",\"save\":\"Speichern\",\"cancel\":\"Abbrechen\",\"invalid\":\"Ungültiges Datum oder Uhrzeit\",\"failed\":\"Speichern fehlgeschlagen\"},"
    "\"history\":{\"loading\":\"Verlauf wird geladen ...\",\"empty\":\"Kein Verlauf aufgezeichnet\",\"shortened\":\"Verlauf gekürzt\",\"error\":\"Verlauf konnte nicht geladen werden\",\"min\":\"Min\",\"max\":\"Max\",\"range_day\":\"24 Std\",\"range_week\":\"7 Tage\",\"range_30d\":\"30 T\",\"range_12m\":\"12 M\",\"daily_mean\":\"Tagesmittel\",\"weekly_mean\":\"Wochenmittel\",\"range_empty_30d\":\"Noch keine Statistik für 30 Tage\",\"range_empty_12m\":\"Noch keine Statistik für 12 Monate\",\"hourly_mean\":\"Stundenmittel\",\"week_empty\":\"Keine Statistik für die letzten 7 Tage\",\"week_error\":\"Statistik konnte nicht geladen werden\",\"days_total\":\"Letzte 7 Tage\",\"days_title\":\"Tagesverbrauch\",\"days_partial\":\"Hellere Balken: Tag unvollständig\",\"days_sources\":\"Einige Quellen fehlen\",\"days_unsupported\":\"Keine Statistik für diese Quelle\"},"
    "\"energytl\":{\"home\":\"Verbrauch\",\"solar\":\"Aus Solar\",\"grid\":\"Aus Netz\",\"export\":\"Einspeisung\",\"battery\":\"Aus Batterie\",\"home_s\":\"Haus\",\"export_s\":\"Einspeisung\",\"solar_s\":\"Solar\",\"battery_s\":\"Batterie\",\"c_solar\":\"Solar\",\"c_battery\":\"Batterie\",\"c_grid\":\"Netz\",\"c_export\":\"Einspeisung\",\"c_charge\":\"Laden\",\"c_forecast\":\"Prognose\",\"loading\":\"Lädt ...\",\"stale\":\"Veraltet\",\"nostats\":\"Keine Statistik\",\"nostats_hint\":\"Home Assistant hat keine Langzeitstatistik für den Netzbezug-Zähler. Er braucht eine state_class total oder total_increasing.\",\"updated\":\"Stand %s\",\"partial\":\"%d von %d Zeitfenstern unvollständig\",\"measured\":\"Gemessen, letzte 24 Stunden\",\"import\":\"Netzbezug\",\"export_meter\":\"Netzeinspeisung\",\"solar_meter\":\"Solarerzeugung\",\"charge\":\"Batterie laden\",\"discharge\":\"Batterie entladen\",\"soc\":\"Batteriestand\",\"forecast\":\"Solarprognose\",\"mix_estimated\":\"Aufteilung (geschätzt)\",\"mix\":\"Aufteilung\",\"home_from_solar\":\"  aus Solar\",\"home_from_battery\":\"  aus Batterie\",\"home_from_grid\":\"  aus Netz\",\"export_from_solar\":\"  aus Solar\",\"export_from_battery\":\"  aus Batterie\",\"charge_from_solar\":\"  aus Solar\",\"charge_from_grid\":\"  aus Netz\",\"estimate_note\":\"Geschätzt: Summenzähler zeigen nicht, ob Netzstrom die Batterie geladen oder das Haus versorgt hat. Solar versorgt zuerst das Haus, dann die Batterie, dann das Netz; die Batterie deckt, was das Haus noch braucht. Die gemessenen Werte oben sind exakt.\"},"
    "\"room\":{\"switches_off\":\"Schalter aus\",\"switch_on\":\"Schalter an\",\"switches_on_n\":\"%d Schalter an\",\"idle\":\"Bereit\",\"closed\":\"Zu\",\"open_n\":\"%d offen\",\"lights_off\":\"Licht aus\",\"light_on\":\"Licht an\",\"lights_on_n\":\"%d Lichter an\",\"present\":\"Anwesend\",\"empty\":\"Niemand\",\"battery_n\":\"Schwache Batterien: %d\"},"
    "\"binary\":{\"just_now\":\"gerade eben\",\"ringing\":\"Ausgelöst\",\"last\":\"Zuletzt\",\"no_event\":\"Kein Ereignis\",\"home\":\"Zuhause\",\"away\":\"Unterwegs\",\"motion\":\"Bewegung\",\"clear\":\"Ruhig\",\"unlocked\":\"Entriegelt\",\"locked\":\"Verriegelt\",\"wet\":\"Nass\",\"dry\":\"Trocken\",\"alarm\":\"Alarm\",\"ok\":\"OK\",\"low\":\"Schwach\",\"connected\":\"Verbunden\",\"disconnected\":\"Getrennt\",\"on\":\"An\",\"off\":\"Aus\"},"
    "\"scene\":{\"scene\":\"Szene\",\"script\":\"Skript\",\"button\":\"Taste\",\"lock\":\"Entriegeln\"},"
    "\"lock\":{\"state\":{\"locked\":\"Verriegelt\",\"unlocked\":\"Entriegelt\",\"locking\":\"Verriegelt ...\",\"unlocking\":\"Entriegelt ...\",\"open\":\"Offen\",\"opening\":\"Öffnet ...\",\"jammed\":\"Blockiert\"},\"lock\":\"Verriegeln\",\"unlock\":\"Entriegeln\",\"open\":\"Öffnen\",\"confirm_unlock\":\"%s entriegeln?\",\"confirm_open\":\"%s öffnen?\"},"
    "\"alarm\":{\"state\":{\"disarmed\":\"Unscharf\",\"armed_home\":\"Scharf (Zuhause)\",\"armed_away\":\"Scharf (Abwesend)\",\"armed_night\":\"Scharf (Nacht)\",\"armed_vacation\":\"Scharf (Urlaub)\",\"armed_custom_bypass\":\"Scharf (Benutzerdef.)\",\"arming\":\"Schaltet scharf ...\",\"pending\":\"Verzögert\",\"disarming\":\"Schaltet unscharf ...\",\"triggered\":\"Ausgelöst\"},\"action\":{\"disarm\":\"Unscharf schalten\",\"arm_home\":\"Scharf (Zuhause)\",\"arm_away\":\"Scharf (Abwesend)\",\"arm_night\":\"Scharf (Nacht)\",\"arm_vacation\":\"Scharf (Urlaub)\",\"arm_custom_bypass\":\"Scharf (Benutzerdef.)\"},\"ok\":\"OK\"},"
    "\"price\":{\"today\":\"Heute\",\"tomorrow\":\"Morgen\",\"min\":\"min\",\"max\":\"max\",\"average\":\"Durchschnitt\",\"cheap\":\"Günstig\",\"no_cheap\":\"Kein günstiges Fenster mehr\",\"no_data\":\"Keine Preise\",\"no_today\":\"Keine Preise für heute\",\"no_tomorrow\":\"Morgen noch nicht veröffentlicht\",\"no_clock\":\"Uhrzeit fehlt\"},"
    "\"parqet\":{\"xirr\":\"XIRR\",\"ttwror\":\"TTWROR\",\"dividends\":\"Dividenden\",\"realized\":\"Realisiert\",\"performance\":\"Performance\",\"holdings\":\"Positionen\",\"loading\":\"Lädt\",\"no_answer\":\"Keine Antwort (die Antwort ist evtl. zu groß)\",\"failed\":\"Nicht verfügbar\",\"no_data\":\"Keine Daten\",\"largest\":\"Größte\",\"top\":\"Top 5\",\"flop\":\"Flop 5\",\"pick_interval\":\"Zeitraum wählen\"},"
    "\"stock\":{\"title\":\"Aktien & ETFs\",\"lookup_failed\":\"Quelle unbekannt\",\"unsupported\":\"Kein Kurs\",\"period\":{\"1d\":\"1T\",\"1w\":\"1W\",\"30d\":\"30T\",\"ytd\":\"YTD\",\"1y\":\"1J\"},\"source\":{\"closes\":\"Schlusskurse\",\"recorded\":\"Aufgezeichnet\",\"averages\":\"HA-Mittelwerte\"},\"since\":\"seit\",\"from\":\"ab\",\"prev_close\":\"Vortagesschluss\",\"trend\":\"Trend\",\"history_failed\":\"Verlauf nicht verfügbar\",\"history_empty\":\"Kein Verlauf für diesen Zeitraum\",\"market\":{\"open\":\"Börse offen\",\"pre\":\"Vorbörslich\",\"post\":\"Nachbörslich\",\"closed\":\"Börse geschlossen\"},\"quote_time\":\"Kurs\",\"updated\":\"aktualisiert\",\"last_price\":\"letzter Kurs\"},"
    "\"vacmode\":{\"vacuum\":\"Saugen\",\"mop\":\"Wischen\",\"vac_and_mop\":\"Saugen & Wischen\",\"vacuum_and_mop\":\"Saugen & Wischen\",\"mop_after_vac\":\"Erst saugen, dann wischen\",\"mop_after_vacuum\":\"Erst saugen, dann wischen\",\"quick_vac\":\"Schnell saugen\",\"auto_vac\":\"Auto saugen\",\"deep_clean_vac\":\"Intensiv saugen\",\"quiet_vac\":\"Leise saugen\",\"quick_mop\":\"Schnell wischen\",\"auto_mop\":\"Auto wischen\",\"deep_clean_mop\":\"Intensiv wischen\",\"quick_vac_and_mop\":\"Schnell saugen & wischen\",\"auto_vac_and_mop\":\"Auto saugen & wischen\",\"deep_clean_vac_and_mop\":\"Intensiv saugen & wischen\",\"quiet_vac_and_mop\":\"Leise saugen & wischen\",\"custom\":\"Individuell\"},"
    "\"car\":{\"precond\":\"Vorklimatisieren\"},"
    "\"fan\":{\"oscillate\":\"Schwenken\"},"
    "\"contact\":{\"open\":\"Offen\",\"opened\":\"Geöffnet\",\"closed\":\"Geschlossen\"},"
    "\"climate\":{\"current\":\"Jetzt\",\"fan_short\":\"Lüfter\",\"fan_title\":\"Lüfter-Modus\",\"mode_title\":\"Betriebsmodus\",\"swing_short\":\"Schwenk\",\"swing_title\":\"Schwenk-Modus\",\"mode\":{\"off\":\"Aus\",\"heat\":\"Heizen\",\"cool\":\"Kühlen\",\"auto\":\"Auto\",\"dry\":\"Trocknen\",\"fan_only\":\"Lüfter\",\"heat_cool\":\"Heizen/Kühlen\",\"cooling\":\"Kühlt\",\"drying\":\"Trocknet\",\"idle\":\"Bereit\",\"fan\":\"Lüftet\",\"defrosting\":\"Enteist\",\"preheating\":\"Vorheizen\",\"vertical\":\"Vertikal\",\"horizontal\":\"Horizontal\",\"both\":\"Beide\"}},"
    "\"roborock\":{\"fan_title\":\"Saugstufe\",\"ready\":\"Bereit\",\"cleaning\":\"Reinigt\",\"returning\":\"Kehrt zurück\",\"dock\":\"Station\",\"fan\":\"Saugstufe\",\"clean_selected_short\":\"Reinigen\",\"map_ready\":\"Karte verbunden\",\"map_loading\":\"Karte wird geladen\",\"map_waiting\":\"Warte auf Karte\",\"map_failed\":\"Kartenvorschau nicht verfügbar\",\"ready_to_charge\":\"In der Station\",\"mode\":\"Modus\",\"mode_title\":\"Reinigungsmodus\",\"state\":{\"cleaning\":\"Reinigt\",\"docked\":\"In der Station\",\"returning\":\"Kehrt zurück\",\"idle\":\"Bereit\",\"paused\":\"Pausiert\",\"error\":\"Fehler\",\"unavailable\":\"Nicht verfügbar\",\"unknown\":\"Unbekannt\",\"segment_cleaning\":\"Raumreinigung\",\"room_cleaning\":\"Raumreinigung\",\"zoned_cleaning\":\"Zonenreinigung\",\"spot_cleaning\":\"Punktreinigung\",\"going_to_target\":\"Fährt zum Ziel\",\"target_found\":\"Ziel erreicht\"}},"
    "\"topbar\":{\"ha\":\"HA\",\"ap\":\"AP\"},"
    "\"sensor\":{\"age\":{\"just_now\":\"gerade eben\",\"min_one\":\"vor 1 Min\",\"min_many\":\"vor %d Min\",\"hour_one\":\"vor 1 Std\",\"hour_many\":\"vor %d Std\",\"day_one\":\"vor 1 Tag\",\"day_many\":\"vor %d Tagen\"},\"in\":{\"now\":\"jetzt\",\"min_one\":\"in 1 Min\",\"min_many\":\"in %d Min\",\"hour_one\":\"in 1 Std\",\"hour_many\":\"in %d Std\",\"day_one\":\"in 1 Tag\",\"day_many\":\"in %d Tagen\"},\"day\":{\"today\":\"heute\",\"tomorrow\":\"morgen\",\"yesterday\":\"gestern\"},\"dur\":{\"d\":\"Tg\",\"h\":\"Std\",\"min\":\"Min\",\"s\":\"s\"}},"
    "\"heating\":{\"target_format\":\"Soll %.1f°C\",\"active\":\"Heizen aktiv\"},"
    "\"light\":{\"effect\":\"Effekt\",\"color_title\":\"Lichtfarbe\",\"white\":\"Weiß\",\"color_temperature\":\"Farbtemperatur\",\"warm\":\"Warm\",\"cool\":\"Kalt\",\"rgb_color\":\"RGB-Farbe\",\"presets\":\"Vorgaben\",\"hue\":\"Farbton\",\"saturation\":\"Sättigung\"},"
    "\"weather\":{\"unavailable\":\"Nicht verfügbar\",\"humidity_format\":\"Luftfeuchte %d%%\",\"low_short\":\"Tief\",\"rain_short\":\"Regen\",\"now\":\"Jetzt\",\"today\":\"Heute\",\"report_title\":\"Wetterbericht\",\"day_full_0\":\"Sonntag\",\"day_full_1\":\"Montag\",\"day_full_2\":\"Dienstag\",\"day_full_3\":\"Mittwoch\",\"day_full_4\":\"Donnerstag\",\"day_full_5\":\"Freitag\",\"day_full_6\":\"Samstag\",\"pressure\":\"Luftdruck\",\"wind_gust\":\"Windböen\",\"uv_index\":\"UV-Index\",\"cloud_cover\":\"Bewölkung\",\"sunshine\":\"Sonnenstunden\",\"rain_duration\":\"Regendauer\",\"dew_point\":\"Taupunkt\",\"humidity\":\"Luftfeuchtigkeit\",\"wind\":\"Wind\",\"visibility\":\"Sichtweite\",\"rain_chance\":\"Regenwahrscheinlichkeit\",\"precipitation\":\"Niederschlag\",\"compass\":\"N,NO,O,SO,S,SW,W,NW\",\"weekday_0\":\"So\",\"weekday_1\":\"Mo\",\"weekday_2\":\"Di\",\"weekday_3\":\"Mi\",\"weekday_4\":\"Do\",\"weekday_5\":\"Fr\",\"weekday_6\":\"Sa\",\"condition\":{\"clear_night\":\"Klare Nacht\",\"cloudy\":\"Bewölkt\",\"exceptional\":\"Ungewöhnliches Wetter\",\"fog\":\"Nebel\",\"hail\":\"Hagel\",\"lightning\":\"Gewitter\",\"lightning_rainy\":\"Gewitter mit Regen\",\"partlycloudy\":\"Teilweise bewölkt\",\"pouring\":\"Starkregen\",\"rainy\":\"Regnerisch\",\"snowy\":\"Schnee\",\"snowy_rainy\":\"Schneeregen\",\"sunny\":\"Sonnig\",\"windy\":\"Windig\",\"windy_variant\":\"Windig, bewölkt\"}},"
    "\"graph\":{\"no_history\":\"kein Verlauf\",\"no_data\":\"keine Daten\",\"min\":\"min\",\"max\":\"max\"},"
    "\"notification\":{\"dismiss\":\"Schließen\",\"action\":\"Öffnen\",\"sent\":\"Gesendet\"},"
    "\"conn\":{\"wifi\":\"WLAN\",\"ha\":\"Home Assistant\",\"mqtt\":\"MQTT\",\"status\":\"Status\",\"host\":\"Host\",\"broker\":\"Broker\",\"device_id\":\"Geräte-ID\",\"connected\":\"Verbunden\",\"disconnected\":\"Nicht verbunden\",\"syncing\":\"Verbunden, wird synchronisiert\",\"disabled\":\"Deaktiviert\",\"diag_reason\":\"Letzter Trennungsgrund\",\"diag_reconnects\":\"Reconnect-Versuche\",\"diag_hard_recover\":\"C6-Hardware-Resets\",\"firmware\":\"Firmware\",\"version\":\"Version\",\"built\":\"Erstellt\",\"update_available\":\"Update verfügbar\"},"
    "\"wifi\":{\"info\":{\"ip\":\"IP-Adresse\",\"signal\":\"Signalstärke\",\"setup_ap\":\"Setup-AP aktiv\",\"signal_excellent\":\"Sehr gut\",\"signal_good\":\"Gut\",\"signal_fair\":\"Mäßig\",\"signal_weak\":\"Schwach\"}},"
    "\"solar\":{\"rest\":\"Rest\",\"today\":\"Heute\",\"tomorrow\":\"Morgen\",\"day_3\":\"Tag 3\",\"day_4\":\"Tag 4\",\"day_5\":\"Tag 5\",\"day_generic\":\"Tag\",\"weekday_0\":\"So\",\"weekday_1\":\"Mo\",\"weekday_2\":\"Di\",\"weekday_3\":\"Mi\",\"weekday_4\":\"Do\",\"weekday_5\":\"Fr\",\"weekday_6\":\"Sa\"},"
    "\"energy\":{\"title\":\"Energie\",\"waiting\":\"Warte auf Energiedaten\",\"ha_waiting\":\"Home-Assistant-Energiedaten\",\"today\":\"Heute\",\"grid\":\"Netz\",\"solar\":\"Solar\",\"home\":\"Haus\",\"battery\":\"Batterie\",\"grid_in\":\"Bezug\",\"grid_out\":\"Einspeisung\",\"battery_charge\":\"Laden\",\"battery_out\":\"Entladen\",\"home_now\":\"Haus jetzt\",\"water\":\"Wasser\",\"gas\":\"Gas\",\"autarky\":\"Autarkie\",\"details_prompt\":\"Gas, Wasser oder Solar wählen\"},"
    "\"sonos_favorites\":{\"sensor_hint\":\"Benötigt die Sonos-Integration in Home Assistant und deren Favoriten-Sensor.\",\"setup_hint\":\"Sonos-Integration und sensor.sonos_favorites in Home Assistant aktivieren.\",\"empty\":\"Keine Favoriten gefunden.\"},"
    "\"evcc\":{\"wallbox\":\"Wallbox\",\"add\":\"+ Wallbox (evcc)\",\"loadpoint\":\"Ladepunkt\",\"choose\":\"Ladepunkt wählen\",\"find\":\"Ladepunkte suchen\",\"finding\":\"Ladepunkte werden gesucht…\",\"failed\":\"Nicht bestätigt\",\"none_found\":\"Keine aktivierten evcc-Ladepunkte gefunden\",\"replace\":\"Vorhandene Entity-Zuordnung durch diesen Ladepunkt ersetzen? Manuelle Zuordnungen werden ersetzt.\",\"missing\":\"Fehlende oder deaktivierte Entities\",\"bound\":\"Ladepunkt zugeordnet\",\"entities\":\"Erweiterte Entities\",\"hint\":\"Fehlende Entities in Home Assistant aktivieren. Erneute Suche erhält manuelle Zuordnungen.\",\"mode\":\"Lademodus\",\"vehicle\":\"Fahrzeug\",\"target\":\"Ziel-SOC\",\"power\":\"Ladeleistung (sensor)\",\"soc\":\"Fahrzeug-SOC (sensor)\",\"connected\":\"Verbunden\",\"charging\":\"Lädt\",\"disabled\":\"In Konfiguration deaktiviert (binary_sensor)\",\"effective\":\"Effektiv\",\"off\":\"Aus\",\"smart\":\"Smart\",\"fast\":\"Schnell\",\"none\":\"Kein Fahrzeug\",\"ready\":\"Nicht verbunden\",\"unavailable\":\"Nicht verfügbar\",\"pending\":\"Warten…\",\"apply\":\"Übernehmen\",\"energy\":\"Geladene Energie (sensor)\",\"remaining\":\"Restzeit\",\"sessions\":\"Ladungen\",\"charged\":\"Geladen\",\"change\":\"Ändern\",\"last\":\"Letzte Ladung\",\"power_label\":\"Leistung\",\"soc_label\":\"Ladestand\",\"target_label\":\"Ladeziel\",\"car\":\"Auto\",\"start\":\"Start\",\"sessions_loading\":\"Ladungen werden geladen…\",\"sessions_error\":\"Ladungen konnten nicht geladen werden\",\"sessions_empty\":\"Keine abgeschlossenen Ladungen\",\"sessions_unsupported\":\"Ladungen nicht verfügbar\",\"sessions_preview\":\"Ladungen werden auf dem Panel geladen\"},"
    "\"camera\":{\"live\":\"Live-Kamera\",\"connecting\":\"Verbinde…\",\"paused\":\"Pausiert\",\"waiting\":\"Warte auf Kamera\",\"unsupported\":\"MJPEG nicht verfügbar\",\"failed\":\"Kameraverbindung unterbrochen\"},"
    "\"bambu\":{\"state\":{\"missing\":\"Keine Daten\",\"unavailable\":\"Nicht verfügbar\",\"other\":\"Unbekannt\",\"offline\":\"Offline\",\"idle\":\"Bereit\",\"init\":\"Startet\",\"prepare\":\"Vorbereitung\",\"slicing\":\"Slicing\",\"running\":\"Druckt\",\"pause\":\"Pausiert\",\"finish\":\"Fertig\",\"failed\":\"Fehlgeschlagen\"},\"pause\":\"Pause\",\"resume\":\"Weiter\",\"stop\":\"Stopp\",\"status\":\"Status\",\"nozzle\":\"Düse\",\"bed\":\"Bett\",\"progress\":\"Fortschritt\",\"remaining\":\"Restzeit\",\"min\":\"Min\",\"stage\":\"Schritt\",\"job\":\"Auftrag\",\"stop_title\":\"Druck stoppen?\",\"cancel\":\"Abbrechen\",\"not_confirmed\":\"Befehl nicht bestätigt\",\"send_failed\":\"Befehl nicht gesendet\",\"sending\":\"Sende...\",\"last_job\":\"Letzter Auftrag\",\"details\":\"Druckerstatus\",\"not_connected\":\"Home Assistant nicht verbunden\"},"
    "\"autoupdate\":{\"rollback_title\":\"Update zurückgesetzt\",\"rollback_message\":\"Das automatische Update auf\",\"rollback_message2\":\"ist fehlgeschlagen. Das Panel ist zur vorherigen Firmware zurückgekehrt und versucht diese Version nicht noch einmal.\"},"
    "\"todo\":{\"empty\":\"keine offenen Einträge\",\"loading\":\"lädt ...\"},"
    "\"settings\":{\"wifi\":{\"ssid\":\"SSID\"}},"
    "\"boot\":{\"initializing_system\":\"System wird initialisiert\",\"initializing_wifi\":\"WLAN wird initialisiert\",\"initializing_touch\":\"Touch wird initialisiert\",\"wifi_setup_title\":\"WLAN-Einrichtung\",\"wifi_connect_failed\":\"WLAN-Verbindung fehlgeschlagen\",\"wifi_credentials_missing\":\"WLAN-Zugangsdaten fehlen\",\"open_editor\":\"BETTA Editor öffnen:\",\"ha_setup_title\":\"Home-Assistant-Einrichtung\",\"wifi_connected\":\"WLAN verbunden\",\"ha_credentials_missing\":\"Home-Assistant-Zugangsdaten fehlen\",\"set_ha_url_token\":\"Home-Assistant-URL und Token eingeben\",\"loading_dashboard\":\"Dashboard wird geladen\",\"setup_ap_prefix\":\"Setup AP\",\"setup_ap_password\":\"Passwort\",\"offline_mode\":\"Offline-Modus\"},"
    "\"ota\":{\"title\":\"Firmware-Update\",\"preparing\":\"Update wird vorbereitet\",\"downloading\":\"Firmware wird heruntergeladen ...\",\"receiving\":\"Firmware wird empfangen ...\",\"writing\":\"Firmware wird geschrieben\",\"writing_version\":\"Firmware %s wird geschrieben\",\"success\":\"Update geschrieben. Neustart ...\",\"failed\":\"Update fehlgeschlagen\",\"unknown_size\":\"%s / unbekannt\"}"
    "}}";

static const char *I18N_BUILTIN_EN =
    "{\"lvgl\":{"
    "\"common\":{\"on\":\"ON\",\"off\":\"OFF\",\"unavailable\":\"unavailable\",\"paused\":\"paused\",\"playing\":\"playing\",\"start\":\"Start\",\"pause\":\"Pause\",\"unknown\":\"Unknown\"},"
    "\"cover\":{\"open\":\"Open\",\"closed\":\"Closed\",\"opening\":\"Opening\",\"closing\":\"Closing\",\"moving\":\"Moving\"},"
    "\"datetime\":{\"date\":\"Date\",\"time\":\"Time\",\"save\":\"Save\",\"cancel\":\"Cancel\",\"invalid\":\"Invalid date or time\",\"failed\":\"Could not save\"},"
    "\"history\":{\"loading\":\"Loading history...\",\"empty\":\"No history recorded\",\"shortened\":\"History shortened to fit\",\"error\":\"Could not load history\",\"min\":\"Min\",\"max\":\"Max\",\"range_day\":\"24 h\",\"range_week\":\"7 days\",\"range_30d\":\"30 d\",\"range_12m\":\"12 m\",\"daily_mean\":\"Daily means\",\"weekly_mean\":\"Weekly means\",\"range_empty_30d\":\"No statistics for 30 days yet\",\"range_empty_12m\":\"No statistics for 12 months yet\",\"hourly_mean\":\"Hourly means\",\"week_empty\":\"No statistics for the last 7 days\",\"week_error\":\"Could not load statistics\",\"days_total\":\"Last 7 days\",\"days_title\":\"Daily consumption\",\"days_partial\":\"Lighter bars: incomplete day\",\"days_sources\":\"Some sources are missing\",\"days_unsupported\":\"No statistics for this source\"},"
    "\"energytl\":{\"home\":\"Home use\",\"solar\":\"From solar\",\"grid\":\"From grid\",\"export\":\"Feed-in\",\"battery\":\"From battery\",\"home_s\":\"Home\",\"export_s\":\"Feed-in\",\"solar_s\":\"Solar\",\"battery_s\":\"Battery\",\"c_solar\":\"Solar\",\"c_battery\":\"Battery\",\"c_grid\":\"Grid\",\"c_export\":\"Feed-in\",\"c_charge\":\"Charging\",\"c_forecast\":\"Forecast\",\"loading\":\"Loading ...\",\"stale\":\"Outdated\",\"nostats\":\"No statistics\",\"nostats_hint\":\"Home Assistant has no long-term statistics for the grid import counter. It needs a state_class of total or total_increasing.\",\"updated\":\"Updated %s\",\"partial\":\"%d of %d slots are incomplete\",\"measured\":\"Measured, last 24 hours\",\"import\":\"Grid import\",\"export_meter\":\"Grid export\",\"solar_meter\":\"Solar production\",\"charge\":\"Battery charge\",\"discharge\":\"Battery discharge\",\"soc\":\"Battery level\",\"forecast\":\"Solar forecast\",\"mix_estimated\":\"Mix (estimated)\",\"mix\":\"Mix\",\"home_from_solar\":\"  from solar\",\"home_from_battery\":\"  from battery\",\"home_from_grid\":\"  from grid\",\"export_from_solar\":\"  from solar\",\"export_from_battery\":\"  from battery\",\"charge_from_solar\":\"  from solar\",\"charge_from_grid\":\"  from grid\",\"estimate_note\":\"Estimated: aggregate meters cannot tell whether grid energy charged the battery or served the home. Solar serves the home first, then the battery, then the grid; the battery serves what the home still needs. The measured values above are exact.\"},"
    "\"room\":{\"switches_off\":\"Switches off\",\"switch_on\":\"Switch on\",\"switches_on_n\":\"%d switches on\",\"idle\":\"Idle\",\"closed\":\"Closed\",\"open_n\":\"%d open\",\"lights_off\":\"Lights off\",\"light_on\":\"Light on\",\"lights_on_n\":\"%d lights on\",\"present\":\"Present\",\"empty\":\"Empty\",\"battery_n\":\"Low batteries: %d\"},"
    "\"binary\":{\"just_now\":\"just now\",\"ringing\":\"Triggered\",\"last\":\"Last\",\"no_event\":\"No event\",\"home\":\"Home\",\"away\":\"Away\",\"motion\":\"Motion\",\"clear\":\"Clear\",\"unlocked\":\"Unlocked\",\"locked\":\"Locked\",\"wet\":\"Wet\",\"dry\":\"Dry\",\"alarm\":\"Alarm\",\"ok\":\"OK\",\"low\":\"Low\",\"connected\":\"Connected\",\"disconnected\":\"Disconnected\",\"on\":\"On\",\"off\":\"Off\"},"
    "\"scene\":{\"scene\":\"Scene\",\"script\":\"Script\",\"button\":\"Button\",\"lock\":\"Unlock\"},"
    "\"lock\":{\"state\":{\"locked\":\"Locked\",\"unlocked\":\"Unlocked\",\"locking\":\"Locking ...\",\"unlocking\":\"Unlocking ...\",\"open\":\"Open\",\"opening\":\"Opening ...\",\"jammed\":\"Jammed\"},\"lock\":\"Lock\",\"unlock\":\"Unlock\",\"open\":\"Open\",\"confirm_unlock\":\"Unlock %s?\",\"confirm_open\":\"Open %s?\"},"
    "\"alarm\":{\"state\":{\"disarmed\":\"Disarmed\",\"armed_home\":\"Armed home\",\"armed_away\":\"Armed away\",\"armed_night\":\"Armed night\",\"armed_vacation\":\"Armed vacation\",\"armed_custom_bypass\":\"Armed custom\",\"arming\":\"Arming ...\",\"pending\":\"Pending\",\"disarming\":\"Disarming ...\",\"triggered\":\"Triggered\"},\"action\":{\"disarm\":\"Disarm\",\"arm_home\":\"Arm home\",\"arm_away\":\"Arm away\",\"arm_night\":\"Arm night\",\"arm_vacation\":\"Arm vacation\",\"arm_custom_bypass\":\"Arm custom bypass\"},\"ok\":\"OK\"},"
    "\"price\":{\"today\":\"Today\",\"tomorrow\":\"Tomorrow\",\"min\":\"min\",\"max\":\"max\",\"average\":\"Average\",\"cheap\":\"Cheap\",\"no_cheap\":\"No cheap window left\",\"no_data\":\"No prices\",\"no_today\":\"No prices for today\",\"no_tomorrow\":\"Tomorrow not published yet\",\"no_clock\":\"No clock\"},"
    "\"parqet\":{\"xirr\":\"XIRR\",\"ttwror\":\"TTWROR\",\"dividends\":\"Dividends\",\"realized\":\"Realised\",\"performance\":\"Performance\",\"holdings\":\"Holdings\",\"loading\":\"Loading\",\"no_answer\":\"No answer (the reply may be too large)\",\"failed\":\"Not available\",\"no_data\":\"No data\",\"largest\":\"Largest\",\"top\":\"Top 5\",\"flop\":\"Flop 5\",\"pick_interval\":\"Pick an interval\"},"
    "\"stock\":{\"title\":\"Stocks & ETFs\",\"lookup_failed\":\"Source unknown\",\"unsupported\":\"Not a quote\",\"period\":{\"1d\":\"1D\",\"1w\":\"1W\",\"30d\":\"30D\",\"ytd\":\"YTD\",\"1y\":\"1Y\"},\"source\":{\"closes\":\"Closes\",\"recorded\":\"Recorded\",\"averages\":\"HA averages\"},\"since\":\"since\",\"from\":\"from\",\"prev_close\":\"prev. close\",\"trend\":\"trend\",\"history_failed\":\"History not available\",\"history_empty\":\"No history for this period\",\"market\":{\"open\":\"Market open\",\"pre\":\"Pre-market\",\"post\":\"After hours\",\"closed\":\"Market closed\"},\"quote_time\":\"quote\",\"updated\":\"updated\",\"last_price\":\"last price\"},"
    "\"vacmode\":{\"vacuum\":\"Vacuum\",\"mop\":\"Mop\",\"vac_and_mop\":\"Vacuum & mop\",\"vacuum_and_mop\":\"Vacuum & mop\",\"mop_after_vac\":\"Vacuum, then mop\",\"mop_after_vacuum\":\"Vacuum, then mop\",\"quick_vac\":\"Quick vacuum\",\"auto_vac\":\"Auto vacuum\",\"deep_clean_vac\":\"Deep clean vacuum\",\"quiet_vac\":\"Quiet vacuum\",\"quick_mop\":\"Quick mop\",\"auto_mop\":\"Auto mop\",\"deep_clean_mop\":\"Deep clean mop\",\"quick_vac_and_mop\":\"Quick vacuum & mop\",\"auto_vac_and_mop\":\"Auto vacuum & mop\",\"deep_clean_vac_and_mop\":\"Deep clean vacuum & mop\",\"quiet_vac_and_mop\":\"Quiet vacuum & mop\",\"custom\":\"Custom\"},"
    "\"car\":{\"precond\":\"Precondition\"},"
    "\"fan\":{\"oscillate\":\"Oscillate\"},"
    "\"contact\":{\"open\":\"Open\",\"opened\":\"Opened\",\"closed\":\"Closed\"},"
    "\"climate\":{\"current\":\"Now\",\"fan_short\":\"Fan\",\"fan_title\":\"Fan mode\",\"mode_title\":\"Mode\",\"swing_short\":\"Swing\",\"swing_title\":\"Swing mode\",\"mode\":{\"off\":\"Off\",\"heat\":\"Heat\",\"cool\":\"Cool\",\"auto\":\"Auto\",\"dry\":\"Dry\",\"fan_only\":\"Fan only\",\"heat_cool\":\"Heat/Cool\",\"cooling\":\"Cooling\",\"drying\":\"Drying\",\"idle\":\"Idle\",\"fan\":\"Fan\",\"defrosting\":\"Defrosting\",\"preheating\":\"Preheating\",\"vertical\":\"Vertical\",\"horizontal\":\"Horizontal\",\"both\":\"Both\"}},"
    "\"roborock\":{\"fan_title\":\"Suction level\",\"ready\":\"Ready\",\"cleaning\":\"Cleaning\",\"returning\":\"Returning\",\"dock\":\"Dock\",\"fan\":\"Fan\",\"clean_selected_short\":\"Clean\",\"map_ready\":\"Map connected\",\"map_loading\":\"Loading map\",\"map_waiting\":\"Waiting for map\",\"map_failed\":\"Map preview unavailable\",\"ready_to_charge\":\"On dock\",\"mode\":\"Mode\",\"mode_title\":\"Cleaning mode\",\"state\":{\"cleaning\":\"Cleaning\",\"docked\":\"Docked\",\"returning\":\"Returning\",\"idle\":\"Idle\",\"paused\":\"Paused\",\"error\":\"Error\",\"unavailable\":\"Unavailable\",\"unknown\":\"Unknown\",\"segment_cleaning\":\"Room cleaning\",\"room_cleaning\":\"Room cleaning\",\"zoned_cleaning\":\"Zone cleaning\",\"spot_cleaning\":\"Spot cleaning\",\"going_to_target\":\"Going to target\",\"target_found\":\"Target reached\"}},"
    "\"topbar\":{\"ha\":\"HA\",\"ap\":\"AP\"},"
    "\"sensor\":{\"age\":{\"just_now\":\"just now\",\"min_one\":\"1 min ago\",\"min_many\":\"%d min ago\",\"hour_one\":\"1 hour ago\",\"hour_many\":\"%d hours ago\",\"day_one\":\"1 day ago\",\"day_many\":\"%d days ago\"},\"in\":{\"now\":\"now\",\"min_one\":\"in 1 min\",\"min_many\":\"in %d min\",\"hour_one\":\"in 1 hour\",\"hour_many\":\"in %d hours\",\"day_one\":\"in 1 day\",\"day_many\":\"in %d days\"},\"day\":{\"today\":\"today\",\"tomorrow\":\"tomorrow\",\"yesterday\":\"yesterday\"},\"dur\":{\"d\":\"d\",\"h\":\"h\",\"min\":\"min\",\"s\":\"s\"}},"
    "\"heating\":{\"target_format\":\"Target %.1f°C\",\"active\":\"heating active\"},"
    "\"light\":{\"effect\":\"Effect\",\"color_title\":\"Light color\",\"white\":\"White\",\"color_temperature\":\"Color temperature\",\"warm\":\"Warm\",\"cool\":\"Cool\",\"rgb_color\":\"RGB colour\",\"presets\":\"Presets\",\"hue\":\"Hue\",\"saturation\":\"Saturation\"},"
    "\"weather\":{\"unavailable\":\"Unavailable\",\"humidity_format\":\"Humidity %d%%\",\"low_short\":\"Low\",\"rain_short\":\"Rain\",\"now\":\"Now\",\"today\":\"Today\",\"report_title\":\"Weather report\",\"day_full_0\":\"Sunday\",\"day_full_1\":\"Monday\",\"day_full_2\":\"Tuesday\",\"day_full_3\":\"Wednesday\",\"day_full_4\":\"Thursday\",\"day_full_5\":\"Friday\",\"day_full_6\":\"Saturday\",\"pressure\":\"Pressure\",\"wind_gust\":\"Wind gusts\",\"uv_index\":\"UV index\",\"cloud_cover\":\"Cloud cover\",\"sunshine\":\"Sunshine\",\"rain_duration\":\"Rain duration\",\"dew_point\":\"Dew point\",\"humidity\":\"Humidity\",\"wind\":\"Wind\",\"visibility\":\"Visibility\",\"rain_chance\":\"Chance of rain\",\"precipitation\":\"Precipitation\",\"compass\":\"N,NE,E,SE,S,SW,W,NW\",\"weekday_0\":\"Sun\",\"weekday_1\":\"Mon\",\"weekday_2\":\"Tue\",\"weekday_3\":\"Wed\",\"weekday_4\":\"Thu\",\"weekday_5\":\"Fri\",\"weekday_6\":\"Sat\",\"condition\":{\"clear_night\":\"Clear night\",\"cloudy\":\"Cloudy\",\"exceptional\":\"Exceptional weather\",\"fog\":\"Fog\",\"hail\":\"Hail\",\"lightning\":\"Thunderstorm\",\"lightning_rainy\":\"Thunderstorm with rain\",\"partlycloudy\":\"Partly cloudy\",\"pouring\":\"Pouring\",\"rainy\":\"Rainy\",\"snowy\":\"Snowy\",\"snowy_rainy\":\"Sleet\",\"sunny\":\"Sunny\",\"windy\":\"Windy\",\"windy_variant\":\"Windy, cloudy\"}},"
    "\"graph\":{\"no_history\":\"no history\",\"no_data\":\"no data\",\"min\":\"min\",\"max\":\"max\"},"
    "\"notification\":{\"dismiss\":\"Dismiss\",\"action\":\"Open\",\"sent\":\"Sent\"},"
    "\"conn\":{\"wifi\":\"Wi-Fi\",\"ha\":\"Home Assistant\",\"mqtt\":\"MQTT\",\"status\":\"Status\",\"host\":\"Host\",\"broker\":\"Broker\",\"device_id\":\"Device ID\",\"connected\":\"Connected\",\"disconnected\":\"Not connected\",\"syncing\":\"Connected, syncing\",\"disabled\":\"Disabled\",\"diag_reason\":\"Last disconnect reason\",\"diag_reconnects\":\"Reconnect attempts\",\"diag_hard_recover\":\"C6 hardware resets\",\"firmware\":\"Firmware\",\"version\":\"Version\",\"built\":\"Built\",\"update_available\":\"Update available\"},"
    "\"wifi\":{\"info\":{\"ip\":\"IP address\",\"signal\":\"Signal strength\",\"setup_ap\":\"Setup AP active\",\"signal_excellent\":\"Excellent\",\"signal_good\":\"Good\",\"signal_fair\":\"Fair\",\"signal_weak\":\"Weak\"}},"
    "\"solar\":{\"rest\":\"Remaining\",\"today\":\"Today\",\"tomorrow\":\"Tomorrow\",\"day_3\":\"Day 3\",\"day_4\":\"Day 4\",\"day_5\":\"Day 5\",\"day_generic\":\"Day\",\"weekday_0\":\"Su\",\"weekday_1\":\"Mo\",\"weekday_2\":\"Tu\",\"weekday_3\":\"We\",\"weekday_4\":\"Th\",\"weekday_5\":\"Fr\",\"weekday_6\":\"Sa\"},"
    "\"energy\":{\"title\":\"Energy\",\"waiting\":\"Waiting for energy data\",\"ha_waiting\":\"Home Assistant energy data\",\"today\":\"Today\",\"grid\":\"Grid\",\"solar\":\"Solar\",\"home\":\"Home\",\"battery\":\"Battery\",\"grid_in\":\"in\",\"grid_out\":\"out\",\"battery_charge\":\"chg\",\"battery_out\":\"out\",\"home_now\":\"Home now\",\"water\":\"Water\",\"gas\":\"Gas\",\"autarky\":\"Self-sufficiency\",\"details_prompt\":\"Select Gas, Water or Solar\"},"
    "\"sonos_favorites\":{\"sensor_hint\":\"Requires the Home Assistant Sonos integration and its favourites sensor.\",\"setup_hint\":\"Enable the Sonos integration and sensor.sonos_favorites in Home Assistant.\",\"empty\":\"No favorites found.\"},"
    "\"evcc\":{\"wallbox\":\"Wallbox\",\"add\":\"+ Wallbox (evcc)\",\"loadpoint\":\"Loadpoint\",\"choose\":\"Choose a loadpoint\",\"find\":\"Find loadpoints\",\"finding\":\"Finding loadpoints...\",\"failed\":\"Not confirmed\",\"none_found\":\"No enabled evcc loadpoints found\",\"replace\":\"Replace the existing entity bindings with this loadpoint? Manual bindings will be replaced.\",\"missing\":\"Missing or disabled entities\",\"bound\":\"Loadpoint configured\",\"entities\":\"Advanced entities\",\"hint\":\"Missing entities must be enabled in Home Assistant. Redetecting preserves manual bindings.\",\"mode\":\"Charge mode\",\"vehicle\":\"Vehicle\",\"target\":\"Target SOC\",\"power\":\"Charge power (sensor)\",\"soc\":\"Vehicle SOC (sensor)\",\"connected\":\"Connected\",\"charging\":\"Charging\",\"disabled\":\"Disabled in configuration (binary_sensor)\",\"effective\":\"Effective\",\"off\":\"Off\",\"smart\":\"Smart\",\"fast\":\"Fast\",\"none\":\"No vehicle\",\"ready\":\"Not connected\",\"unavailable\":\"Unavailable\",\"pending\":\"Waiting...\",\"apply\":\"Apply\",\"energy\":\"Charged energy (sensor)\",\"remaining\":\"Remaining\",\"sessions\":\"Sessions\",\"charged\":\"Charged\",\"change\":\"Change\",\"last\":\"Last session\",\"power_label\":\"Power\",\"soc_label\":\"SOC\",\"target_label\":\"Target SOC\",\"car\":\"Vehicle\",\"start\":\"Start\",\"sessions_loading\":\"Loading sessions...\",\"sessions_error\":\"Could not load sessions\",\"sessions_empty\":\"No completed sessions\",\"sessions_unsupported\":\"Sessions unavailable\",\"sessions_preview\":\"Sessions available on the panel\"},"
    "\"camera\":{\"live\":\"Live camera\",\"connecting\":\"Connecting...\",\"paused\":\"Paused\",\"waiting\":\"Waiting for camera\",\"unsupported\":\"MJPEG unavailable\",\"failed\":\"Camera connection lost\"},"
    "\"bambu\":{\"state\":{\"missing\":\"No data\",\"unavailable\":\"Unavailable\",\"other\":\"Unknown\",\"offline\":\"Offline\",\"idle\":\"Idle\",\"init\":\"Starting\",\"prepare\":\"Preparing\",\"slicing\":\"Slicing\",\"running\":\"Printing\",\"pause\":\"Paused\",\"finish\":\"Finished\",\"failed\":\"Failed\"},\"pause\":\"Pause\",\"resume\":\"Resume\",\"stop\":\"Stop\",\"status\":\"Status\",\"nozzle\":\"Nozzle\",\"bed\":\"Bed\",\"progress\":\"Progress\",\"remaining\":\"Remaining\",\"min\":\"min\",\"stage\":\"Step\",\"job\":\"Job\",\"stop_title\":\"Stop this print?\",\"cancel\":\"Cancel\",\"not_confirmed\":\"Command not confirmed\",\"send_failed\":\"Could not send command\",\"sending\":\"Sending...\",\"last_job\":\"Last job\",\"details\":\"Printer status\",\"not_connected\":\"Home Assistant not connected\"},"
    "\"autoupdate\":{\"rollback_title\":\"Update rolled back\",\"rollback_message\":\"The automatic update to\",\"rollback_message2\":\"failed. The panel returned to the previous firmware and will not try this version again.\"},"
    "\"todo\":{\"empty\":\"no open items\",\"loading\":\"loading...\"},"
    "\"settings\":{\"wifi\":{\"ssid\":\"SSID\"}},"
    "\"boot\":{\"initializing_system\":\"Initializing system\",\"initializing_wifi\":\"Initializing Wi-Fi\",\"initializing_touch\":\"Initializing touch\",\"wifi_setup_title\":\"Wi-Fi setup\",\"wifi_connect_failed\":\"Wi-Fi connection failed\",\"wifi_credentials_missing\":\"Wi-Fi credentials missing\",\"open_editor\":\"Open BETTA Editor:\",\"ha_setup_title\":\"Home Assistant setup\",\"wifi_connected\":\"Wi-Fi connected\",\"ha_credentials_missing\":\"Home Assistant credentials missing\",\"set_ha_url_token\":\"Enter the Home Assistant URL and token\",\"loading_dashboard\":\"Loading dashboard\",\"setup_ap_prefix\":\"Setup AP\",\"setup_ap_password\":\"Password\",\"offline_mode\":\"Offline mode\"},"
    "\"ota\":{\"title\":\"Firmware update\",\"preparing\":\"Preparing update\",\"downloading\":\"Downloading firmware...\",\"receiving\":\"Receiving firmware...\",\"writing\":\"Writing firmware\",\"writing_version\":\"Writing firmware %s\",\"success\":\"Update written. Restarting...\",\"failed\":\"Update failed\",\"unknown_size\":\"%s / unknown\"}"
    "}}";

static bool i18n_store_is_valid_language_code(const char *code)
{
    if (code == NULL) {
        return false;
    }

    size_t len = strlen(code);
    if (len < 2 || len >= APP_UI_LANGUAGE_MAX_LEN) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = code[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool i18n_store_normalize_language_code(const char *input, char *out_code, size_t out_len)
{
    if (input == NULL || out_code == NULL || out_len == 0) {
        return false;
    }

    while (*input != '\0' && isspace((unsigned char)*input)) {
        input++;
    }

    size_t out = 0;
    while (input[out] != '\0' && !isspace((unsigned char)input[out])) {
        if (out + 1 >= out_len) {
            return false;
        }

        char c = (char)tolower((unsigned char)input[out]);
        out_code[out] = c;
        out++;
    }
    out_code[out] = '\0';

    if (out == 0) {
        return false;
    }
    return i18n_store_is_valid_language_code(out_code);
}

bool i18n_store_is_builtin_language(const char *language_code)
{
    if (language_code == NULL) {
        return false;
    }
    return strcmp(language_code, "de") == 0 || strcmp(language_code, "en") == 0;
}

const char *i18n_store_builtin_translation_json(const char *language_code)
{
    if (language_code == NULL) {
        return NULL;
    }
    if (strcmp(language_code, "en") == 0) {
        return I18N_BUILTIN_EN;
    }
    if (strcmp(language_code, "de") == 0) {
        return I18N_BUILTIN_DE;
    }
    return NULL;
}

static bool i18n_store_ensure_dir(void)
{
    struct stat st = {0};
    if (stat(APP_I18N_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }

    (void)mkdir(APP_I18N_DIR, 0775);
    if (stat(APP_I18N_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    return false;
}

static bool i18n_store_build_path(const char *language_code, char *out_path, size_t out_path_len)
{
    if (language_code == NULL || out_path == NULL || out_path_len == 0) {
        return false;
    }
    int written = snprintf(out_path, out_path_len, "%s/%s.json", APP_I18N_DIR, language_code);
    return written > 0 && (size_t)written < out_path_len;
}

esp_err_t i18n_store_load_custom_translation(const char *language_code, char **out_json)
{
    if (language_code == NULL || out_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    char lang[APP_UI_LANGUAGE_MAX_LEN] = {0};
    if (!i18n_store_normalize_language_code(language_code, lang, sizeof(lang))) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[96] = {0};
    if (!i18n_store_build_path(lang, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }

    long size = ftell(f);
    if (size <= 0 || (size_t)size > APP_I18N_MAX_JSON_LEN) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);

    char *buf = calloc((size_t)size + 1U, sizeof(char));
    if (buf == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buf, 1U, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(buf);
        return ESP_FAIL;
    }

    *out_json = buf;
    return ESP_OK;
}

esp_err_t i18n_store_save_custom_translation(const char *language_code, const char *json_payload, size_t payload_len)
{
    if (language_code == NULL || json_payload == NULL || payload_len == 0 || payload_len > APP_I18N_MAX_JSON_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    char lang[APP_UI_LANGUAGE_MAX_LEN] = {0};
    if (!i18n_store_normalize_language_code(language_code, lang, sizeof(lang))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!i18n_store_ensure_dir()) {
        return ESP_FAIL;
    }

    char path[96] = {0};
    if (!i18n_store_build_path(lang, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }

    return atomic_file_write(path, json_payload, payload_len);
}

bool i18n_store_custom_translation_exists(const char *language_code)
{
    char lang[APP_UI_LANGUAGE_MAX_LEN] = {0};
    if (!i18n_store_normalize_language_code(language_code, lang, sizeof(lang))) {
        return false;
    }

    char path[96] = {0};
    if (!i18n_store_build_path(lang, path, sizeof(path))) {
        return false;
    }

    struct stat st = {0};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool i18n_store_add_language(
    char (*out_codes)[APP_UI_LANGUAGE_MAX_LEN],
    size_t max_codes,
    size_t *count,
    const char *code)
{
    if (out_codes == NULL || count == NULL || code == NULL || code[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < *count; i++) {
        if (strncmp(out_codes[i], code, APP_UI_LANGUAGE_MAX_LEN) == 0) {
            return false;
        }
    }
    if (*count >= max_codes) {
        return false;
    }
    strlcpy(out_codes[*count], code, APP_UI_LANGUAGE_MAX_LEN);
    (*count)++;
    return true;
}

esp_err_t i18n_store_list_languages(
    char (*out_codes)[APP_UI_LANGUAGE_MAX_LEN],
    size_t max_codes,
    size_t *out_count)
{
    if (out_codes == NULL || max_codes == 0 || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t count = 0;
    (void)i18n_store_add_language(out_codes, max_codes, &count, "en");
    (void)i18n_store_add_language(out_codes, max_codes, &count, "de");

    if (!i18n_store_ensure_dir()) {
        *out_count = count;
        return ESP_OK;
    }

    DIR *dir = opendir(APP_I18N_DIR);
    if (dir == NULL) {
        *out_count = count;
        return (errno == ENOENT) ? ESP_OK : ESP_FAIL;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len <= strlen(".json")) {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (strcmp(name + (len - 5), ".json") != 0) {
            continue;
        }

        char code[APP_UI_LANGUAGE_MAX_LEN] = {0};
        size_t base_len = len - 5;
        if (base_len >= sizeof(code)) {
            continue;
        }
        memcpy(code, name, base_len);
        code[base_len] = '\0';

        char normalized[APP_UI_LANGUAGE_MAX_LEN] = {0};
        if (!i18n_store_normalize_language_code(code, normalized, sizeof(normalized))) {
            continue;
        }

        (void)i18n_store_add_language(out_codes, max_codes, &count, normalized);
    }
    closedir(dir);

    *out_count = count;
    return ESP_OK;
}
