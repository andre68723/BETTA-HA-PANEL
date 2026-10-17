/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/notification_markup.h"
#include "ui/notification_text.h"

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Markup line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void expect(const char *src, const char *plain, const char *emphasis)
{
    char out[768], strong[768] = {0};
    notif_markup_run_t runs[NOTIF_MARKUP_MAX_RUNS];
    const notif_markup_result_t result = notif_markup_parse(src, out, sizeof(out), runs, NOTIF_MARKUP_MAX_RUNS);
    CHECK(!strcmp(out, plain));
    CHECK(result.length == strlen(out));
    CHECK(result.bold == (emphasis[0] != 0));
    size_t offset = 0;
    for (size_t i = 0; i < result.count; i++) {
        CHECK(runs[i].offset == offset);
        CHECK(runs[i].length > 0 && offset + runs[i].length <= result.length);
        if (runs[i].bold) strncat(strong, out + offset, runs[i].length);
        offset += runs[i].length;
    }
    CHECK(offset == result.length);
    CHECK(!strcmp(strong, emphasis));
}

int main(void)
{
    expect("", "", "");
    expect("Plain notification\nTemperature: 11.8 °C", "Plain notification\nTemperature: 11.8 °C", "");
    expect("**Dense Fog Detected!**\n\nTime: 09:00\n\n__Effects on Solar Production:__",
           "Dense Fog Detected!\n\nTime: 09:00\n\nEffects on Solar Production:",
           "Dense Fog Detected!Effects on Solar Production:");
    expect("Humidity: **100%**, temperature __11.8 °C__.", "Humidity: 100%, temperature 11.8 °C.", "100%11.8 °C");
    expect("**CO₂ über 1000 ppm ⚠️** / Wasser: __12 m³__", "CO₂ über 1000 ppm ⚠️ / Wasser: 12 m³", "CO₂ über 1000 ppm ⚠️12 m³");
    expect("plain **unclosed", "plain **unclosed", "");
    expect("** ** __ __ **** ______", "** ** __ __ **** ______", "");
    expect("***italic+bold***", "***italic+bold***", "");
    expect("sensor.some__value__here", "sensor.some__value__here", "");
    expect("__a__b", "__a__b", "");
    expect("*italic* and _italic_", "*italic* and _italic_", "");
    expect("\\**literal\\** and **bold**", "**literal** and bold", "bold");
    expect("`**code**` and **bold**", "`**code**` and bold", "bold");
    expect("**line one\nline two**", "line one\nline two", "line one\nline two");
    expect("**orphan\n\n**heading**", "**orphan\n\nheading", "heading");
    expect("**a**__b__", "ab", "ab");
    expect("<b>literal HTML</b>", "<b>literal HTML</b>", "");

    /* Every output capacity keeps a UTF-8 prefix, even at a byte boundary
     * inside a bold segment. Surrounding guard bytes cannot be overwritten. */
    const char *source = "a **Größe₂** danach";
    const char *visible = "a Größe₂ danach";
    for (size_t capacity = 1; capacity < strlen(visible) + 2; capacity++) {
        char guarded[64]; memset(guarded, 0x5a, sizeof(guarded));
        notif_markup_run_t runs[NOTIF_MARKUP_MAX_RUNS];
        const notif_markup_result_t result = notif_markup_parse(source, guarded + 1, capacity, runs, NOTIF_MARKUP_MAX_RUNS);
        CHECK(guarded[0] == 0x5a && guarded[capacity + 1] == 0x5a);
        CHECK(result.length < capacity && guarded[result.length + 1] == 0);
        CHECK(!strncmp(guarded + 1, visible, result.length));
        CHECK(notif_text_fit(guarded + 1, result.length, result.length) == result.length);
    }
    char out[768]; notif_markup_run_t runs[NOTIF_MARKUP_MAX_RUNS];
    const char *many = "a **b** c __d__ e **f** g";
    notif_markup_result_t result = notif_markup_parse(many, out, sizeof(out), runs, 2);
    CHECK(!strcmp(out, many) && !result.bold && result.count == 1);
    CHECK(notif_markup_parse(many, NULL, 0, runs, 2).length == 0);
    CHECK(notif_markup_parse(NULL, out, sizeof(out), runs, 2).length == 0 && !out[0]);
    result = notif_markup_parse(many, out, sizeof(out), NULL, 0);
    CHECK(!strcmp(out, many) && !result.bold);
    printf("Notification markup: %u checks passed\n", checks);
    return 0;
}
