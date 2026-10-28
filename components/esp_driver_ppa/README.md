# esp_driver_ppa (vendored)

Copy of ESP-IDF 6.1 `components/esp_driver_ppa` (Apache-2.0, Espressif) without `test_apps/`, with one change in
`src/ppa_srm.c`: the upstream fix `469aa16c3502c6860dbc70511dcb2f922bc6c57d` (esp-idf#19096, #19023). It computes the
P4 hardware workaround DIG-734 (bypass of the macro block order) from the rotated block size; without it a 90/270
degree RGB565 rotation can never complete. Remove this directory once the used ESP-IDF release contains the fix.
