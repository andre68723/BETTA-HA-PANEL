/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The room card's optional extra field: one piece of information (the humidity) or one direct action (a cover,
 * a media player) for an entity that already belongs to the card. Pure decisions, so the validator, the widget and the
 * tests agree: which mode an entity fits, and which service a cover or media player action calls in which state.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ROOM_EXTRA_DEFAULT = 0, /* the card as it always was */
    ROOM_EXTRA_HUMIDITY,
    ROOM_EXTRA_COVER,
    ROOM_EXTRA_MEDIA_PLAYER,
} room_extra_mode_t;

/* "", "default", "humidity", "cover", "media_player"; anything else is invalid (false) and yields the default. */
bool room_extra_mode_parse(const char *text, room_extra_mode_t *out);
const char *room_extra_mode_name(room_extra_mode_t mode);

/* The domain an entity must have for the mode ("sensor", "cover", "media_player"); NULL for the default. */
const char *room_extra_mode_domain(room_extra_mode_t mode);
bool room_extra_entity_fits(room_extra_mode_t mode, const char *entity_id);

/* What a tap on the action does. */
typedef enum {
    ROOM_ACTION_NONE = 0, /* disabled: unavailable, unknown state, or the entity cannot do it */
    ROOM_ACTION_COVER_OPEN,
    ROOM_ACTION_COVER_CLOSE,
    ROOM_ACTION_COVER_STOP,
    ROOM_ACTION_MEDIA_PLAY,
    ROOM_ACTION_MEDIA_PAUSE,
} room_action_t;

#define ROOM_COVER_FEAT_STOP 8U        /* CoverEntityFeature.STOP */
#define ROOM_MEDIA_FEAT_PAUSE 1U       /* MediaPlayerEntityFeature.PAUSE */
#define ROOM_MEDIA_FEAT_PLAY 16384U    /* MediaPlayerEntityFeature.PLAY */

/* A closed cover opens, an open one closes; a moving one offers Stop when the entity supports it. An unknown or
 * unavailable state never guesses. `features_known` false (no supported_features attribute) allows Stop. */
room_action_t room_extra_cover_action(const char *state, uint32_t features, bool features_known);

/* The two directions of a cover, for a card with room for two buttons. "Up" opens (or stops an opening cover), "down" closes
 * (or stops a closing one). A direction that cannot move any further is NONE: an open cover with position 100 (or without a
 * position) cannot go up, a closed one cannot go down. A partly open cover (state open, position 1..99) allows both. */
room_action_t room_extra_cover_up(const char *state, int position, uint32_t features, bool features_known);
room_action_t room_extra_cover_down(const char *state, int position, uint32_t features, bool features_known);

/* Room card control row: one control per device type for all entities of that type on the card. */
/* A group of lights or switches: on when any is on (tap turns all off), otherwise tap turns all on. Nothing without a usable member. */
bool room_group_is_on(int usable, int on);
/* The service of a tap on such a group: "turn_off" when any member is on, else "turn_on"; NULL when no member is usable. */
const char *room_group_toggle_service(int usable, int on);
/* Merge the action of the next cover into the action of the group so far: a real move (open/close) beats Stop, Stop beats nothing. */
room_action_t room_cover_group_merge(room_action_t group, room_action_t next);

/* Playing pauses (when PAUSE is supported), paused/idle/on plays (when PLAY is supported); off, unavailable and
 * unknown do nothing. Without a supported_features attribute both are allowed. */
room_action_t room_extra_media_action(const char *state, uint32_t features, bool features_known);

/* The Home Assistant service of an action ("cover", "open_cover", ...). NULL for ROOM_ACTION_NONE. */
const char *room_action_domain(room_action_t action);
const char *room_action_service(room_action_t action);
