/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The built-in DE/EN translation documents: strict JSON (no trailing data), one root object holding
 * only "lvgl", the same keys in both languages and a working ui_i18n_get() lookup. A surplus closing
 * brace once ended "lvgl" after "roborock": cJSON_Parse ignores trailing data, so "topbar", "energy"
 * and everything after them sat at the root and every lookup returned the English fallback. */
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "settings/i18n_store.h"
#include "ui/ui_i18n.h"

static int failures;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static cJSON *parse_strict(const char *lang)
{
    const char *json = i18n_store_builtin_translation_json(lang);
    CHECK(json != NULL, "%s: no built-in document", lang);
    if (json == NULL) {
        return NULL;
    }
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &end, 1);
    CHECK(root != NULL, "%s: not strict JSON, parse stopped near offset %ld", lang,
          end != NULL ? (long)(end - json) : -1L);
    return root;
}

/* Every leaf of `a` must exist as a string leaf in `b`; returns the number of leaves in `a`. */
static int compare_leaves(const char *path, const cJSON *a, const cJSON *b, const char *a_name, const char *b_name)
{
    int count = 0;
    for (const cJSON *item = a->child; item != NULL; item = item->next) {
        for (const cJSON *previous = a->child; previous != item; previous = previous->next) {
            CHECK(strcmp(previous->string, item->string) != 0, "%s.%s: duplicate JSON member in %s",
                  path, item->string, a_name);
        }
        char next[160];
        snprintf(next, sizeof(next), "%s%s%s", path, path[0] != '\0' ? "." : "", item->string);
        const cJSON *other = cJSON_GetObjectItemCaseSensitive((cJSON *)b, item->string);
        if (cJSON_IsObject(item)) {
            CHECK(cJSON_IsObject(other), "%s is a section in %s but not in %s", next, a_name, b_name);
            if (cJSON_IsObject(other)) {
                count += compare_leaves(next, item, other, a_name, b_name);
            }
        } else {
            count++;
            CHECK(cJSON_IsString(item) && item->valuestring[0] != '\0', "%s: empty or not a string in %s", next, a_name);
            CHECK(cJSON_IsString(other), "%s is in %s but not in %s", next, a_name, b_name);
        }
    }
    return count;
}

static void test_documents(void)
{
    cJSON *de = parse_strict("de");
    cJSON *en = parse_strict("en");
    if (de == NULL || en == NULL) {
        cJSON_Delete(de);
        cJSON_Delete(en);
        return;
    }
    CHECK(cJSON_GetArraySize(de) == 1 && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(de, "lvgl")),
          "de: the root must hold only \"lvgl\" (a section outside it is never found)");
    CHECK(cJSON_GetArraySize(en) == 1 && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(en, "lvgl")),
          "en: the root must hold only \"lvgl\" (a section outside it is never found)");
    const cJSON *de_l = cJSON_GetObjectItemCaseSensitive(de, "lvgl");
    const cJSON *en_l = cJSON_GetObjectItemCaseSensitive(en, "lvgl");
    if (cJSON_IsObject(de_l) && cJSON_IsObject(en_l)) {
        int a = compare_leaves("", de_l, en_l, "de", "en");
        int b = compare_leaves("", en_l, de_l, "en", "de");
        CHECK(a == b, "de has %d keys, en has %d", a, b);
    }
    cJSON_Delete(de);
    cJSON_Delete(en);
}

static void test_lookup(void)
{
    struct {
        const char *lang;
        const char *key;
        const char *expected;
    } cases[] = {
        {"de", "topbar.ap", "AP"},
        {"de", "energy.title", "Energie"},
        {"de", "energy.water", "Wasser"},
        {"de", "energy.gas", "Gas"},
        {"de", "energy.autarky", "Autarkie"},
        {"de", "sensor.age.just_now", "gerade eben"},
        {"de", "boot.initializing_system", "System wird initialisiert"},
        {"en", "energy.title", "Energy"},
        {"en", "energy.water", "Water"},
        {"en", "energy.gas", "Gas"},
        {"en", "sensor.age.just_now", "just now"},
        {"en", "boot.initializing_system", "Initializing system"},
        {"de", "roborock.state.cleaning", "Reinigt"},
        {"de", "roborock.state.returning", "Kehrt zurück"},
        {"de", "roborock.state.paused", "Pausiert"},
        {"en", "roborock.state.cleaning", "Cleaning"},
        {"en", "roborock.state.docked", "Docked"},
        {"de", "ota.receiving", "Firmware wird empfangen ..."},
        {"en", "ota.receiving", "Receiving firmware..."},
        {"de", "solar.details_title", "Solarprognose heute"},
        {"en", "solar.details_title", "Solar forecast today"},
        {"de", "solar.remaining", "Verbleibend"},
        {"en", "solar.remaining", "Remaining"},
        {"de", "solar.total", "Gesamt"},
        {"en", "solar.total", "Total"},
        {"de", "solar.rest", "Rest"},
        {"en", "solar.rest", "Remaining"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(ui_i18n_init(cases[i].lang) == ESP_OK, "init %s", cases[i].lang);
        const char *got = ui_i18n_get(cases[i].key, "<<fallback>>");
        CHECK(strcmp(got, cases[i].expected) == 0, "%s %s: got \"%s\", want \"%s\"", cases[i].lang, cases[i].key, got,
              cases[i].expected);
    }
    ui_i18n_reset();
}

int main(void)
{
    test_documents();
    test_lookup();
    if (failures != 0) {
        printf("test_i18n_builtin: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_i18n_builtin: ok\n");
    return 0;
}
