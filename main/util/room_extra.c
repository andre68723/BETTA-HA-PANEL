/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/room_extra.h"

#include <string.h>

bool room_extra_mode_parse(const char *text, room_extra_mode_t *out)
{
    room_extra_mode_t mode = ROOM_EXTRA_DEFAULT;
    bool valid = true;
    if (text == NULL || text[0] == '\0' || strcmp(text, "default") == 0) {
        mode = ROOM_EXTRA_DEFAULT;
    } else if (strcmp(text, "humidity") == 0) {
        mode = ROOM_EXTRA_HUMIDITY;
    } else if (strcmp(text, "cover") == 0) {
        mode = ROOM_EXTRA_COVER;
    } else if (strcmp(text, "media_player") == 0) {
        mode = ROOM_EXTRA_MEDIA_PLAYER;
    } else {
        valid = false;
    }
    if (out != NULL) {
        *out = mode;
    }
    return valid;
}

const char *room_extra_mode_name(room_extra_mode_t mode)
{
    switch (mode) {
    case ROOM_EXTRA_HUMIDITY: return "humidity";
    case ROOM_EXTRA_COVER: return "cover";
    case ROOM_EXTRA_MEDIA_PLAYER: return "media_player";
    case ROOM_EXTRA_DEFAULT: break;
    }
    return "default";
}

const char *room_extra_mode_domain(room_extra_mode_t mode)
{
    switch (mode) {
    case ROOM_EXTRA_HUMIDITY: return "sensor";
    case ROOM_EXTRA_COVER: return "cover";
    case ROOM_EXTRA_MEDIA_PLAYER: return "media_player";
    case ROOM_EXTRA_DEFAULT: break;
    }
    return NULL;
}

bool room_extra_entity_fits(room_extra_mode_t mode, const char *entity_id)
{
    const char *domain = room_extra_mode_domain(mode);
    if (domain == NULL || entity_id == NULL) {
        return domain == NULL;
    }
    const size_t n = strlen(domain);
    return strncmp(entity_id, domain, n) == 0 && entity_id[n] == '.' && entity_id[n + 1] != '\0';
}

static bool state_is(const char *state, const char *value)
{
    return state != NULL && strcmp(state, value) == 0;
}

room_action_t room_extra_cover_action(const char *state, uint32_t features, bool features_known)
{
    if (state_is(state, "closed")) return ROOM_ACTION_COVER_OPEN;
    if (state_is(state, "open")) return ROOM_ACTION_COVER_CLOSE;
    if (state_is(state, "opening") || state_is(state, "closing")) {
        return (!features_known || (features & ROOM_COVER_FEAT_STOP) != 0U) ? ROOM_ACTION_COVER_STOP : ROOM_ACTION_NONE;
    }
    return ROOM_ACTION_NONE; /* unavailable, unknown, anything else: never guess the next action */
}

static room_action_t cover_stop_if_supported(uint32_t features, bool features_known)
{
    return (!features_known || (features & ROOM_COVER_FEAT_STOP) != 0U) ? ROOM_ACTION_COVER_STOP : ROOM_ACTION_NONE;
}

room_action_t room_extra_cover_up(const char *state, int position, uint32_t features, bool features_known)
{
    if (state_is(state, "opening")) return cover_stop_if_supported(features, features_known);
    if (state_is(state, "closed") || state_is(state, "closing")) return ROOM_ACTION_COVER_OPEN;
    if (state_is(state, "open")) return (position >= 0 && position < 100) ? ROOM_ACTION_COVER_OPEN : ROOM_ACTION_NONE;
    return ROOM_ACTION_NONE;
}

room_action_t room_extra_cover_down(const char *state, int position, uint32_t features, bool features_known)
{
    (void)position;
    if (state_is(state, "closing")) return cover_stop_if_supported(features, features_known);
    if (state_is(state, "open") || state_is(state, "opening")) return ROOM_ACTION_COVER_CLOSE;
    return ROOM_ACTION_NONE; /* closed, unavailable, unknown: nothing to do */
}

bool room_group_is_on(int usable, int on)
{
    return usable > 0 && on > 0;
}

const char *room_group_toggle_service(int usable, int on)
{
    if (usable <= 0) return NULL;
    return on > 0 ? "turn_off" : "turn_on";
}

room_action_t room_cover_group_merge(room_action_t group, room_action_t next)
{
    const bool group_moves = group == ROOM_ACTION_COVER_OPEN || group == ROOM_ACTION_COVER_CLOSE;
    if (group_moves) return group;
    const bool next_moves = next == ROOM_ACTION_COVER_OPEN || next == ROOM_ACTION_COVER_CLOSE;
    if (next_moves) return next;
    return group == ROOM_ACTION_COVER_STOP ? group : next;
}

room_action_t room_extra_media_action(const char *state, uint32_t features, bool features_known)
{
    if (state_is(state, "playing")) {
        return (!features_known || (features & ROOM_MEDIA_FEAT_PAUSE) != 0U) ? ROOM_ACTION_MEDIA_PAUSE : ROOM_ACTION_NONE;
    }
    if (state_is(state, "paused") || state_is(state, "idle") || state_is(state, "on") || state_is(state, "standby")) {
        return (!features_known || (features & ROOM_MEDIA_FEAT_PLAY) != 0U) ? ROOM_ACTION_MEDIA_PLAY : ROOM_ACTION_NONE;
    }
    return ROOM_ACTION_NONE;
}

const char *room_action_domain(room_action_t action)
{
    switch (action) {
    case ROOM_ACTION_COVER_OPEN:
    case ROOM_ACTION_COVER_CLOSE:
    case ROOM_ACTION_COVER_STOP: return "cover";
    case ROOM_ACTION_MEDIA_PLAY:
    case ROOM_ACTION_MEDIA_PAUSE: return "media_player";
    case ROOM_ACTION_NONE: break;
    }
    return NULL;
}

const char *room_action_service(room_action_t action)
{
    switch (action) {
    case ROOM_ACTION_COVER_OPEN: return "open_cover";
    case ROOM_ACTION_COVER_CLOSE: return "close_cover";
    case ROOM_ACTION_COVER_STOP: return "stop_cover";
    case ROOM_ACTION_MEDIA_PLAY: return "media_play";
    case ROOM_ACTION_MEDIA_PAUSE: return "media_pause";
    case ROOM_ACTION_NONE: break;
    }
    return NULL;
}
