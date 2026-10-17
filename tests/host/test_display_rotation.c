/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include <assert.h>
#include <stdio.h>
#include "drivers/display_rotation.h"

static void check_board(uint16_t width, uint16_t height)
{
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        const int w = (rotation & 1) ? height : width;
        const int h = (rotation & 1) ? width : height;
        display_rect_t out;
        assert(display_rotation_rect((display_rect_t){0, 0, w, h}, width, height, rotation, &out));
        assert(out.x1 == 0 && out.y1 == 0 && out.x2 == width && out.y2 == height);
        for (int corner = 0; corner < 4; ++corner) {
            const int x = (corner & 1) ? w - 3 : 0;
            const int y = (corner & 2) ? h - 5 : 0;
            const display_rect_t in = {x, y, x + 3, y + 5};
            assert(display_rotation_rect(in, width, height, rotation, &out));
            assert(out.x1 >= 0 && out.y1 >= 0 && out.x2 <= width && out.y2 <= height);
            assert((out.x2 - out.x1) * (out.y2 - out.y1) == 15);
            assert(out.x2 - out.x1 == ((rotation & 1) ? 5 : 3));
            display_rect_t back;
            assert(display_rotation_rect(out, w, h, (4 - rotation) % 4, &back));
            assert(back.x1 == in.x1 && back.x2 == in.x2 && back.y1 == in.y1 && back.y2 == in.y2);
        }
        assert(!display_rotation_rect((display_rect_t){0, 0, w + 1, h}, width, height, rotation, &out));
        assert(!display_rotation_rect((display_rect_t){0, 0, w, h + 1}, width, height, rotation, &out));
    }
}

int main(void)
{
    check_board(720, 720);
    check_board(800, 1280);
    check_board(720, 1280);
    display_rect_t out;
    assert(display_rotation_rect((display_rect_t){0, 0, 1, 1}, 720, 1280, 1, &out));
    assert(out.x1 == 0 && out.y1 == 1279);
    assert(display_rotation_rect((display_rect_t){0, 0, 1, 1}, 720, 1280, 3, &out));
    assert(out.x1 == 719 && out.y1 == 0);
    assert(!display_rotation_rect((display_rect_t){-1, 0, 1, 1}, 720, 1280, 0, &out));
    assert(!display_rotation_rect((display_rect_t){0, 0, 0, 1}, 720, 1280, 0, &out));
    assert(!display_rotation_rect((display_rect_t){0, 0, 1, 1}, 720, 1280, 4, &out));
    assert(!display_rotation_rect((display_rect_t){0, 0, 1, 1}, 0, 1280, 0, &out));
    assert(!display_rotation_rect((display_rect_t){0, 0, 1, 1}, 720, 1280, 0, NULL));
    puts("display rotation: PASS (three native geometries, all rotations, edge/odd rectangles and invalid areas)");
    return 0;
}
