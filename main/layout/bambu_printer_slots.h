/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The entity bindings of the Bambu printer widget (bambu_printer): their JSON keys, the domain each accepts and
 * the generic widget slot each is stored in. One table for the layout validator, the subscription collector, the
 * firmware parser and the desktop simulator.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"
#include "cJSON.h"

/* extra_entity_id[] of the Bambu printer widget; the primary entity is the print status, the nozzle and bed
 * temperatures use secondary_entity_id and tertiary_entity_id. */
#define UI_BAMBU_EXTRA_PROGRESS 0
#define UI_BAMBU_EXTRA_REMAINING 1
#define UI_BAMBU_EXTRA_PAUSE 2
#define UI_BAMBU_EXTRA_RESUME 3
#define UI_BAMBU_EXTRA_STOP 4
#define UI_BAMBU_EXTRA_STAGE 5
#define UI_BAMBU_EXTRA_JOB 6
#define UI_BAMBU_EXTRA_CAMERA 7
#define BAMBU_EXTRA_COUNT 8 /* extra_entity_id[] entries it needs; ui_widget_factory.h asserts the capacity */

#define BAMBU_SLOT_PRIMARY (-1)
#define BAMBU_SLOT_SECONDARY (-2)
#define BAMBU_SLOT_TERTIARY (-3)

typedef struct {
    const char *key;    /* JSON key of the widget */
    int slot;           /* BAMBU_SLOT_* or an UI_BAMBU_EXTRA_* index */
    const char *domain; /* the only entity domain accepted */
    bool action;        /* a button the widget presses */
    const char *translation_key; /* the integration's entity registry translation_key, to find the entity of a printer */
} bambu_printer_binding_t;

extern const bambu_printer_binding_t k_bambu_printer_bindings[];
extern const size_t k_bambu_printer_binding_count;

/* Copies each configured binding of `widget` into the generic slots. Strings of APP_MAX_ENTITY_ID_LEN or more are not
 * copied (the validator rejects them), never truncated. */
void bambu_printer_slots_from_json(const cJSON *widget, char *primary, char *secondary, char *tertiary,
                                   char (*extra)[APP_MAX_ENTITY_ID_LEN], size_t extra_count);
