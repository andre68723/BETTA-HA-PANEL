/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "util/cover_guard.h"
#include "ha/ha_auth_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *origin = "http://ha.example:8123";
    const struct { const char *input, *output; cover_http_auth_t auth; } cases[] = {
        {"http://ha.example:8123/api/camera_proxy/camera.test?token=old",
         "http://ha.example:8123/api/camera_proxy/camera.test", COVER_HTTP_BEARER},
        {"http://ha.example:8123/api/camera_proxy/camera.test?width=320&token=old&height=180",
         "http://ha.example:8123/api/camera_proxy/camera.test?width=320&height=180", COVER_HTTP_BEARER},
        {"http://ha.example:8123/api/camera_proxy/camera.test?token=old&token=older#image",
         "http://ha.example:8123/api/camera_proxy/camera.test#image", COVER_HTTP_BEARER},
        {"http://ha.example:8123/api/camera_proxy_stream/camera.test?token=old&interval=2",
         "http://ha.example:8123/api/camera_proxy_stream/camera.test?interval=2", COVER_HTTP_BEARER},
        {"http://ha.example:8123/api/camera_proxy/camera.test?not_token=kept&token&width=1",
         "http://ha.example:8123/api/camera_proxy/camera.test?not_token=kept&width=1", COVER_HTTP_BEARER},
        {"http://ha.example:8123/api/camera_proxy/camera.test",
         "http://ha.example:8123/api/camera_proxy/camera.test", COVER_HTTP_BEARER},
        {"http://ha.example:8123/api/media_player_proxy/media_player.test?token=signed",
         "http://ha.example:8123/api/media_player_proxy/media_player.test?token=signed", COVER_HTTP_SIGNED_URL},
        {"http://foreign.example/api/camera_proxy/camera.test?token=signed",
         "http://foreign.example/api/camera_proxy/camera.test?token=signed", COVER_HTTP_ANONYMOUS},
        {"http://ha.example:8123.evil.example/api/camera_proxy/camera.test?token=signed",
         "http://ha.example:8123.evil.example/api/camera_proxy/camera.test?token=signed", COVER_HTTP_ANONYMOUS},
        {"http://ha.example:81234/api/camera_proxy/camera.test?token=signed",
         "http://ha.example:81234/api/camera_proxy/camera.test?token=signed", COVER_HTTP_ANONYMOUS},
        {"http://ha.example:8123/other?token=signed",
         "http://ha.example:8123/other?token=signed", COVER_HTTP_SIGNED_URL},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char url[512]; snprintf(url, sizeof(url), "%s", cases[i].input);
        assert(cover_prepare_image_auth(url, origin) == cases[i].auth);
        assert(strcmp(url, cases[i].output) == 0);
    }
    assert(cover_url_is_camera_proxy("/api/camera_proxy/camera.test?token=old"));
    assert(!cover_url_is_camera_proxy("http://ha.example:8123?next=/api/camera_proxy/camera.test"));

    ha_auth_state_reset();
    assert(ha_auth_state_report_image_fetch(401, true));
    assert(ha_auth_state_request_retry(1000));
    assert(ha_auth_state_blocked()); /* pressing the button cannot unblock an old transport */
    assert(!ha_auth_state_request_retry(1001));
    assert(ha_auth_state_take_retry());
    assert(!ha_auth_state_take_retry());
    assert(ha_auth_state_blocked());
    ha_auth_state_begin_retry();
    assert(!ha_auth_state_blocked());
    assert(ha_auth_state_report_http(403, false, false)); /* still banned at HA */
    assert(!ha_auth_state_retry_available(15999));
    assert(ha_auth_state_retry_available(16000));
    assert(!ha_auth_state_take_retry()); /* cooldown never schedules another attempt */
    assert(ha_auth_state_request_retry(16000));
    assert(ha_auth_state_take_retry());
    ha_auth_state_begin_retry();
    assert(!ha_auth_state_blocked());
    puts("cover auth: 11 URL policies, explicit retry lifecycle and rejection cooldown passed");
}
