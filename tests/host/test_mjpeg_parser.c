/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ha/mjpeg_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, frames;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "MJPEG line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static bool frame(void *user, const uint8_t *jpeg, size_t length)
{
    CHECK(user == &frames);
    CHECK(length == 8 && !memcmp(jpeg, "\xff\xd8" "ABCD" "\xff\xd9", 8));
    frames++;
    return true;
}
static bool cancel(void *user, const uint8_t *jpeg, size_t length) { return false; }
static bool accepts(const char *body)
{
    mjpeg_parser_t p;
    uint8_t buffer[32];
    CHECK(mjpeg_parser_init(&p, "multipart/x-mixed-replace; boundary=x", buffer, sizeof(buffer)));
    return mjpeg_parser_feed(&p, (const uint8_t *)body, strlen(body), frame, &frames);
}
int main(void)
{
    static const char body[] = "--x\r\nContent-Type: image/jpeg\r\nContent-Length: 8\r\n\r\n"
        "\xff\xd8" "ABCD" "\xff\xd9" "\r\n--x\r\ncontent-length: 8\r\ncontent-type: image/jpeg\r\n\r\n"
        "\xff\xd8" "ABCD" "\xff\xd9" "\r\n--x--\r\n";
    for (unsigned mode = 0; mode < 3; mode++) {
        const char *type = mode == 0 ? "multipart/x-mixed-replace; boundary=x" : mode == 1 ?
            "Multipart/X-Mixed-Replace; foo=bar; BOUNDARY=\"x\"" : "multipart/x-mixed-replace; boundary=--x";
        for (size_t split = 1; split < sizeof(body); split++) {
            mjpeg_parser_t p;
            uint8_t buffer[32];
            CHECK(mjpeg_parser_init(&p, type, buffer, sizeof(buffer)));
            frames = 0;
            CHECK(mjpeg_parser_feed(&p, (const uint8_t *)body, split, frame, &frames));
            CHECK(mjpeg_parser_feed(&p, (const uint8_t *)body + split, sizeof(body) - 1 - split, frame, &frames));
            CHECK(frames == 2 && p.ended);
        }
    }
    mjpeg_parser_t p;
    uint8_t buffer[32];
    frames = 0;
    CHECK(mjpeg_parser_init(&p, "multipart/x-mixed-replace; boundary=x", buffer, sizeof(buffer)));
    for (size_t i = 0; i < sizeof(body) - 1; i++) CHECK(mjpeg_parser_feed(&p, (const uint8_t *)body + i, 1, frame, &frames));
    CHECK(frames == 2);
    CHECK(!mjpeg_parser_init(&p, "image/jpeg", buffer, sizeof(buffer)));
    CHECK(!mjpeg_parser_init(&p, "multipart/x-mixed-replace; boundary=\"x", buffer, sizeof(buffer)));
    CHECK(!mjpeg_parser_init(&p, "multipart/x-mixed-replace; boundary=", buffer, sizeof(buffer)));
    CHECK(!accepts("--x\r\nContent-Type: image/png\r\nContent-Length: 8\r\n\r\n"));
    CHECK(!accepts("--x\r\nContent-Type: image/jpeg\r\n\r\n"));
    CHECK(!accepts("--x\r\nContent-Type: image/jpeg\r\nContent-Length: 99999999999999999999\r\n\r\n"));
    CHECK(!accepts("--x\r\nContent-Length: 8\r\nContent-Length: 8\r\nContent-Type: image/jpeg\r\n\r\n"));
    CHECK(!accepts("--x\r\nContent-Length: -8\r\nContent-Type: image/jpeg\r\n\r\n"));
    CHECK(!accepts("--x\r\nContent-Length: 8x\r\nContent-Type: image/jpeg\r\n\r\n"));
    CHECK(!accepts("--x\r\nContent-Length: 0\r\nContent-Type: image/jpeg\r\n\r\n"));
    CHECK(!accepts("--x\r\nContent-Length: 8\r\nContent-Type: image/jpeg\r\n\r\n12345678"));
    CHECK(mjpeg_parser_init(&p, "multipart/x-mixed-replace; boundary=x", buffer, sizeof(buffer)));
    CHECK(!mjpeg_parser_feed(&p, (const uint8_t *)body, sizeof(body) - 1, cancel, NULL));
    CHECK(p.failed);
    CHECK(!mjpeg_parser_feed(&p, (const uint8_t *)body, 1, frame, &frames));
    char large[MJPEG_HEADER_MAX + 8]; memset(large, 'A', sizeof(large));
    CHECK(mjpeg_parser_init(&p, "multipart/x-mixed-replace; boundary=x", buffer, sizeof(buffer)));
    CHECK(!mjpeg_parser_feed(&p, (const uint8_t *)large, sizeof(large), frame, &frames));
    char path[144];
    CHECK(mjpeg_camera_path("camera.printer_1", path, sizeof(path)));
    CHECK(!strcmp(path, "/api/camera_proxy_stream/camera.printer_1"));
    CHECK(!mjpeg_camera_path("camera.printer?token=secret", path, sizeof(path)));
    CHECK(!mjpeg_camera_path("camera../host", path, sizeof(path)));
    CHECK(!mjpeg_camera_path("image.printer", path, sizeof(path)));
    CHECK(!mjpeg_camera_path("camera.", path, sizeof(path)));
    CHECK(!mjpeg_camera_path("http://host", path, sizeof(path)));
    CHECK(!mjpeg_camera_path("camera.printer", path, 8));
    printf("MJPEG parser: %u checks passed\n", checks);
}
