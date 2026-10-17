/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host tests of the notification text policy: supported emoji, simplifications, the [Emoji] marker, sequences, controls, capacity.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/notification_text.h"

static int g_checks, g_failed;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { g_checks++; const char *va = (a), *vb = (b); if (strcmp(va, vb) != 0) { g_failed++; printf("FAIL %s:%d \"%s\" != \"%s\"\n", __FILE__, __LINE__, va, vb); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long va = (long)(a), vb = (long)(b); if (va != vb) { g_failed++; printf("FAIL %s:%d %s (%ld) != %s (%ld)\n", __FILE__, __LINE__, #a, va, #b, vb); } } while (0)

static notif_text_result_t run(const char *in, char *out, size_t cap)
{
    return notif_text_sanitize(in, strlen(in), out, cap);
}

static void expect(const char *in, const char *want)
{
    char out[512];
    run(in, out, sizeof(out));
    g_checks++;
    if (strcmp(out, want) != 0) { g_failed++; printf("FAIL \"%s\" -> \"%s\", wanted \"%s\"\n", in, out, want); }
}

static void test_plain_text(void)
{
    expect("", "");
    expect("Hallo Welt", "Hallo Welt");
    expect("Türklingel: Größe 21,5 °C – 99 €", "Türklingel: Größe 21,5 °C – 99 €");
    expect("Arbeitszimmer: CO₂ über 1000 ppm (1134 ppm) – Zeit zum Lüften!",
           "Arbeitszimmer: CO₂ über 1000 ppm (1134 ppm) – Zeit zum Lüften!");
    expect("Wasser: 12,3 m³ · Fläche: 20 m² · PM₂.₅", "Wasser: 12,3 m³ · Fläche: 20 m² · PM₂.₅");
    expect("₀₁₂₃₄₅₆₇₈₉", "₀₁₂₃₄₅₆₇₈₉");
    expect("  leading spaces", "leading spaces");
    expect("trailing  \n\n", "trailing");
    expect("a\r\nb", "a\nb");               /* CR dropped */
    expect("a \nb", "a\nb");                /* spaces before a newline dropped */
    expect("a\n   b", "a\nb");              /* spaces at the line start dropped */
    expect("a\tb", "a b");                  /* tab is a space */
    expect("a\x01\x02\x7F" "b", "ab");      /* controls dropped */
    expect("one  two", "one two");
    CHECK(run("x", (char[1]){0}, 1).length == 0);
}

static void test_symbol_replacements(void)
{
    /* separator lines of box-drawing characters stay lines; the heavy line of a daily summary */
    expect("\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81", "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93");
    expect("\xE2\x94\x80\xE2\x94\x80", "\xE2\x80\x93\xE2\x80\x93");   /* light line */
    expect("a\xE2\x94\x82" "b", "a|b");                                     /* vertical */
    expect("\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90", "+\xE2\x80\x93+");  /* corners */
    /* a delta before a number: the deviation of a forecast */
    expect("Solar ML 47.1 kWh (\xCE\x94 0.7)", "Solar ML 47.1 kWh (\xC2\xB1 0.7)");
    /* the plain check mark has no glyph in the emoji font: its heavy twin does */
    expect("ok \xE2\x9C\x93", "ok \xE2\x9C\x94");
    CHECK(notif_text_emoji_supported(0x2714U));
    /* emoji of a daily summary that became supported */
    expect("\xF0\x9F\x93\xA4 Export", "\xF0\x9F\x93\xA4 Export");          /* outbox tray */
    expect("\xF0\x9F\x93\xA5 Import", "\xF0\x9F\x93\xA5 Import");          /* inbox tray */
    expect("\xF0\x9F\x8F\x86 Bester", "\xF0\x9F\x8F\x86 Bester");          /* trophy */
    expect("\xF0\x9F\x9B\xB0\xEF\xB8\x8F Solcast", "\xF0\x9F\x9B\xB0 Solcast"); /* satellite + FE0F */
    expect("\xF0\x9F\x8C\x90 pvnode", "\xF0\x9F\x8C\x90 pvnode");          /* globe */
    expect("\xF0\x9F\x93\x8A \xF0\x9F\x93\x88 \xF0\x9F\x93\x89", "\xF0\x9F\x93\x8A \xF0\x9F\x93\x88 \xF0\x9F\x93\x89");
    /* the whole line of the report that showed boxes: no replacement marker is left */
    char out[512];
    const notif_text_result_t r = run("\xF0\x9F\xA4\x96 Solar ML 47.1 kWh (\xCE\x94 0.7) \xE2\x9C\x93\n"
                                       "\xF0\x9F\x9B\xB0\xEF\xB8\x8F Solcast 46.2 kWh (\xCE\x94 1.6)\n"
                                       "\xF0\x9F\x8C\x90 pvnode 50.2 kWh (\xCE\x94 2.4)", out, sizeof(out));
    CHECK_EQ(r.replaced, 0);
    CHECK(strstr(out, NOTIF_TEXT_EMOJI_MARKER) == NULL);
}

static void test_supported_emoji(void)
{
    expect("Fertig \xE2\x9C\x85", "Fertig \xE2\x9C\x85");                     /* check mark button */
    expect("Achtung \xE2\x9A\xA0\xEF\xB8\x8F bitte", "Achtung \xE2\x9A\xA0 bitte"); /* warning + FE0F: selector dropped */
    expect("\xE2\x9D\xA4\xEF\xB8\x8F", "\xE2\x9D\xA4");                        /* red heart */
    expect("\xF0\x9F\x94\x94 Klingel", "\xF0\x9F\x94\x94 Klingel");            /* bell */
    expect("\xF0\x9F\x98\x8A", "\xF0\x9F\x98\x8A");                            /* smiling face */
    expect("\xF0\x9F\x91\x8D\xF0\x9F\x91\x8D", "\xF0\x9F\x91\x8D\xF0\x9F\x91\x8D"); /* repeated */
    expect("\xE2\x98\x80\xEF\xB8\x8E", "\xE2\x98\x80");                        /* text presentation selector dropped */
    expect("Hi\xF0\x9F\x98\x8A!", "Hi\xF0\x9F\x98\x8A!");
    CHECK(notif_text_emoji_supported(0x1F514U));
    CHECK(notif_text_emoji_supported(0x2705U));
    CHECK(!notif_text_emoji_supported(0x1F984U));  /* unicorn: not in the manifest */
    CHECK(!notif_text_emoji_supported('A'));
}

static void test_simplification_and_markers(void)
{
    char out[128];
    notif_text_result_t r = run("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", out, sizeof(out)); /* thumbs up + medium skin tone */
    CHECK_STR(out, "\xF0\x9F\x91\x8D");
    CHECK_EQ(r.simplified, 1); CHECK_EQ(r.replaced, 0);

    r = run("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB", out, sizeof(out));         /* woman technologist (ZWJ) */
    CHECK_STR(out, "[Emoji]"); CHECK_EQ(r.replaced, 1);
    r = run("\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA", out, sizeof(out));                       /* flag Germany */
    CHECK_STR(out, "[Emoji]"); CHECK_EQ(r.replaced, 1);
    r = run("1\xEF\xB8\x8F\xE2\x83\xA3", out, sizeof(out));                              /* keycap 1 */
    CHECK_STR(out, "[Emoji]"); CHECK_EQ(r.replaced, 1);
    r = run("\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF", out, sizeof(out)); /* England flag, tag sequence */
    CHECK_STR(out, "[Emoji]"); CHECK_EQ(r.replaced, 1);
    r = run("\xF0\x9F\xA6\x84", out, sizeof(out));                                       /* unicorn: unsupported */
    CHECK_STR(out, "[Emoji]"); CHECK_EQ(r.replaced, 1);
    expect("a \xF0\x9F\xA6\x84 b \xF0\x9F\xA6\x84", "a [Emoji] b [Emoji]");
    expect("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", "[Emoji]"); /* family: one marker, not three */
    /* a digit is not a keycap without U+20E3 and a lone regional indicator is dropped */
    expect("Raum 1 und #2", "Raum 1 und #2");
    expect("a\xF0\x9F\x87\xA9" "b", "ab");
    /* stray modifiers, joiners and selectors are dropped */
    expect("a\xF0\x9F\x8F\xBD" "b", "ab");
    expect("a\xE2\x80\x8D" "b", "ab");
    expect("a\xEF\xB8\x8F" "b", "ab");
    /* the double arrow is a supported glyph of the emoji font now (it used to become a tilde); other arrows are still dropped */
    expect("a \xE2\x86\x94 b", "a \xE2\x86\x94 b");
    expect("a \xE2\x86\x92 b", "a b");
    /* a supported arrow emoji is kept */
    expect("\xE2\x9E\xA1", "\xE2\x9E\xA1");
    /* the diagonal and vertical arrows are emoji glyphs too; their FE0F is dropped, a bare one is kept as well */
    expect("\xE2\x86\x97\xEF\xB8\x8F 3.4x besser", "\xE2\x86\x97 3.4x besser");
    expect("\xE2\x86\x96 \xE2\x86\x98 \xE2\x86\x99 \xE2\x86\x95", "\xE2\x86\x96 \xE2\x86\x98 \xE2\x86\x99 \xE2\x86\x95");
    /* an arrow outside the font is still dropped */
    expect("a \xE2\x86\x91 b", "a b");
}

static void test_invalid_utf8(void)
{
    expect("a\xC0\xAF" "b", "ab");        /* overlong */
    expect("a\xFF" "b", "ab");
    expect("a\xE2\x82" "b", "ab");        /* truncated 3-byte sequence */
    expect("a\xED\xA0\x80" "b", "ab");    /* UTF-16 surrogate */
    expect("a\xF4\x90\x80\x80" "b", "ab"); /* beyond U+10FFFF */
    char out[16];
    notif_text_result_t r = notif_text_sanitize(NULL, 5, out, sizeof(out));
    CHECK_EQ(r.length, 0); CHECK_STR(out, "");
}

static int valid_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
        if (!n) return 0;
        for (int i = 1; i < n; i++) if ((p[i] & 0xC0) != 0x80) return 0;
        p += n;
    }
    return 1;
}

static void test_capacity(void)
{
    char out[64];
    memset(out, 'x', sizeof(out));
    notif_text_result_t r = notif_text_sanitize("abc", 3, out, 0);        /* capacity 0: nothing written */
    CHECK_EQ(r.length, 0); CHECK(out[0] == 'x');
    r = notif_text_sanitize("abc", 3, out, 1);                              /* capacity 1: empty string */
    CHECK_EQ(r.length, 0); CHECK(out[0] == '\0'); CHECK(r.truncated);
    r = notif_text_sanitize("abc", 3, out, 4);                              /* exact fit */
    CHECK_STR(out, "abc"); CHECK(!r.truncated);
    r = notif_text_sanitize("abcd", 4, out, 4);                             /* one byte too many */
    CHECK(r.truncated); CHECK(strlen(out) < 4); CHECK(valid_utf8(out));

    /* truncation: ends at a recent word boundary and adds an ellipsis when it fits */
    r = notif_text_sanitize("Die Waschmaschine ist fertig und wartet", 38, out, 30);
    CHECK(r.truncated); CHECK(valid_utf8(out)); CHECK(strlen(out) < 30);
    CHECK(strstr(out, "\xE2\x80\xA6") != NULL);
    CHECK(strstr(out, "Die Waschmaschine ist") != NULL);
    /* an unbroken word is cut at a character boundary, never dropped entirely */
    r = notif_text_sanitize("Donaudampfschifffahrtsgesellschaft", 34, out, 16);
    CHECK(r.truncated); CHECK(strncmp(out, "Donaudampfsc", 8) == 0); CHECK(valid_utf8(out)); CHECK(strlen(out) <= 15);
    /* never splits a UTF-8 sequence or an emoji */
    r = notif_text_sanitize("\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4", 10, out, 8);
    CHECK(valid_utf8(out)); CHECK(r.truncated); CHECK(strlen(out) <= 7);
    r = notif_text_sanitize("ab\xF0\x9F\x94\x94\xF0\x9F\x94\x94", 10, out, 8);
    CHECK(valid_utf8(out)); CHECK(strlen(out) <= 7);
    /* the marker is atomic and counts 7 bytes: replacing a 4-byte emoji grows the text by 3 */
    r = notif_text_sanitize("\xF0\x9F\xA6\x84", 4, out, 8);
    CHECK_STR(out, "[Emoji]"); CHECK(!r.truncated);
    r = notif_text_sanitize("\xF0\x9F\xA6\x84", 4, out, 7);                  /* 6 bytes of room: the marker does not fit */
    CHECK(strchr(out, '[') == NULL || strcmp(out, "[Emoji]") == 0); CHECK(r.truncated);
    r = notif_text_sanitize("ab \xF0\x9F\xA6\x84\xF0\x9F\xA6\x84", 11, out, 13);  /* the second marker does not fit */
    CHECK(r.truncated); CHECK(valid_utf8(out)); CHECK(strlen(out) <= 12);
    CHECK(strstr(out, "[Emo") == NULL || strstr(out, "[Emoji]") != NULL); /* never half a marker */
    /* the ellipsis only when its 3 bytes and the terminator fit */
    r = notif_text_sanitize("abcdefghij", 10, out, 6);
    CHECK(r.truncated); CHECK(strlen(out) <= 5); CHECK(valid_utf8(out));
    r = notif_text_sanitize("abcdefghij", 10, out, 4);
    CHECK(strlen(out) <= 3); CHECK(valid_utf8(out));
    /* growth by markers is bounded by the destination, source and destination are separate */
    char big[2048]; memset(big, 0, sizeof(big));
    char src[600]; src[0] = 0;
    for (int i = 0; i < 100; i++) strcat(src, "\xF0\x9F\xA6\x84");
    r = notif_text_sanitize(src, strlen(src), big, 64);
    CHECK(r.truncated); CHECK(strlen(big) < 64); CHECK(valid_utf8(big));
    CHECK(strlen(big) % 7 == 0 || strstr(big, "\xE2\x80\xA6") != NULL);
    /* a text with an explicit length stops at the length, and at an embedded NUL */
    r = notif_text_sanitize("abcdef", 3, out, sizeof(out)); CHECK_STR(out, "abc");
    r = notif_text_sanitize("ab\0cd", 5, out, sizeof(out)); CHECK_STR(out, "ab");
}

/* every capacity from 0 to 40 gives a NUL-terminated, valid, bounded string for a mixed input */
static void test_every_capacity(void)
{
    const char *inputs[] = {
        "Wetter \xE2\x98\x80 21 \xC2\xB0" "C \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB \xF0\x9F\x87\xA9\xF0\x9F\x87\xAA fertig \xF0\x9F\x94\x94",
        "\xF0\x9F\xA6\x84\xF0\x9F\xA6\x84\xF0\x9F\xA6\x84 abc",
        "Gr\xC3\xBC\xC3\x9F" "e aus K\xC3\xB6ln, es ist sehr sch\xC3\xB6n heute",
    };
    for (size_t k = 0; k < sizeof(inputs) / sizeof(inputs[0]); k++) {
        for (size_t cap = 0; cap <= 40; cap++) {
            char out[64]; memset(out, 0x55, sizeof(out));
            notif_text_result_t r = notif_text_sanitize(inputs[k], strlen(inputs[k]), out, cap);
            if (cap == 0) { CHECK(out[0] == 0x55); continue; }
            CHECK(out[r.length] == '\0');
            CHECK(r.length < cap);
            CHECK(strlen(out) == r.length);
            CHECK(valid_utf8(out));
            CHECK(out[cap < sizeof(out) ? cap : sizeof(out) - 1] == 0x55 || cap >= sizeof(out)); /* no write past the capacity */
        }
    }
}

/* Ingress clipping: a field is cut before a whole codepoint or emoji unit that does not fit, never inside one. */
static void test_ingress_fit(void)
{
    static const char *const seqs[] = {
        "\xE2\x9D\xA4",                                                    /* heart */
        "\xE2\x9D\xA4\xEF\xB8\x8F",                                      /* heart + FE0F */
        "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD",                              /* thumbs up + skin tone */
        "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA",                              /* flag DE */
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", /* family, ZWJ */
        "1\xEF\xB8\x8F\xE2\x83\xA3",                                      /* keycap 1 */
        "\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF", /* England */
        "\xC3\xA4",                                                        /* plain 2-byte letter */
    };
    static const size_t caps[] = {63, 767, 23}; /* usable bytes of title, message, action label */
    for (size_t c = 0; c < sizeof(caps) / sizeof(caps[0]); c++) {
        const size_t max = caps[c];
        for (size_t q = 0; q < sizeof(seqs) / sizeof(seqs[0]); q++) {
            const size_t len = strlen(seqs[q]);
            for (size_t pad = max - len; pad <= max; pad++) {
                char in[1024], want[1024], out[1024];
                memset(in, 'a', pad);
                memcpy(in + pad, seqs[q], len);
                strcpy(in + pad + len, "zz");
                memset(want, 'a', pad);
                size_t want_len = pad;
                if (pad + len <= max) {
                    memcpy(want + pad, seqs[q], len);
                    want_len += len;
                    for (size_t z = 0; z < 2 && want_len < max; z++) want[want_len++] = 'z';
                }
                want[want_len] = '\0';
                const size_t got = notif_text_copy(out, max + 1U, in);
                CHECK_EQ(got, want_len);
                CHECK_STR(out, want);
            }
        }
    }
    /* adjacent units: only the second one is dropped when it does not fit */
    {
        char out[16];
        const char *two = "\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x9D\xA4\xEF\xB8\x8F"; /* 12 bytes */
        CHECK_EQ(notif_text_copy(out, 12, two), 6);
        CHECK_EQ(notif_text_copy(out, 13, two), 12);
    }
    /* a NUL ends the text; a text that fits is untouched; a NULL source and a zero capacity are safe */
    CHECK_EQ(notif_text_fit("abc\0def", 7, 10), 3);
    CHECK_EQ(notif_text_fit("abcdef", 6, 6), 6);
    CHECK_EQ(notif_text_fit(NULL, 0, 4), 0);
    {
        char out[4] = {'x', 'x', 'x', 'x'};
        CHECK_EQ(notif_text_copy(out, 4, NULL), 0);
        CHECK_STR(out, "");
        CHECK_EQ(notif_text_copy(out, 0, "abc"), 0);
    }
    /* raw ingress changes nothing else: selectors and skin tones are kept (the sanitizer decides later) */
    {
        char out[64];
        const char *tone = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
        notif_text_copy(out, sizeof(out), tone);
        CHECK_STR(out, tone);
    }
}

int main(void)
{
    test_plain_text();
    test_supported_emoji();
    test_symbol_replacements();
    test_simplification_and_markers();
    test_invalid_utf8();
    test_capacity();
    test_every_capacity();
    test_ingress_fit();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed == 0) printf("PASS: notification text, emoji and capacity rules\n");
    return g_failed == 0 ? 0 : 1;
}
