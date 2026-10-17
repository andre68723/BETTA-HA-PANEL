/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host tests of the Bambu printer widget's pure parts: status normalisation, measurement parsing, remaining-time
 * units, action eligibility, command confirmation and the JSON key -> slot mapping.
 */
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "layout/bambu_printer_slots.h"
#include "ui/widgets/bambu_printer_policy.h"

static int s_checks;
static int s_failures;

#define CHECK(cond) do { s_checks++; if (!(cond)) { s_failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void test_states(void)
{
    CHECK(bambu_state_parse(NULL) == BAMBU_STATE_MISSING);
    CHECK(bambu_state_parse("") == BAMBU_STATE_MISSING);
    CHECK(bambu_state_parse("unavailable") == BAMBU_STATE_UNAVAILABLE);
    CHECK(bambu_state_parse("unknown") == BAMBU_STATE_UNAVAILABLE);
    CHECK(bambu_state_parse("running") == BAMBU_STATE_RUNNING);
    CHECK(bambu_state_parse("pause") == BAMBU_STATE_PAUSE);
    CHECK(bambu_state_parse("finish") == BAMBU_STATE_FINISH);
    CHECK(bambu_state_parse("failed") == BAMBU_STATE_FAILED);
    CHECK(bambu_state_parse("offline") == BAMBU_STATE_OFFLINE);
    CHECK(bambu_state_parse("idle") == BAMBU_STATE_IDLE);
    CHECK(bambu_state_parse("init") == BAMBU_STATE_INIT);
    CHECK(bambu_state_parse("prepare") == BAMBU_STATE_PREPARE);
    CHECK(bambu_state_parse("slicing") == BAMBU_STATE_SLICING);
    CHECK(bambu_state_parse("Running") == BAMBU_STATE_OTHER); /* the integration's option keys are lower case */
    CHECK(bambu_state_parse("calibrating") == BAMBU_STATE_OTHER);
    for (int s = BAMBU_STATE_MISSING; s <= BAMBU_STATE_FAILED; s++) {
        CHECK(bambu_state_key((bambu_state_t)s) != NULL && strncmp(bambu_state_key((bambu_state_t)s), "bambu.state.", 12) == 0);
        CHECK(bambu_state_fallback((bambu_state_t)s)[0] != '\0');
    }
    CHECK(bambu_state_job_active(BAMBU_STATE_RUNNING) && bambu_state_job_active(BAMBU_STATE_PAUSE));
    CHECK(bambu_state_job_active(BAMBU_STATE_PREPARE) && bambu_state_job_active(BAMBU_STATE_SLICING));
    CHECK(!bambu_state_job_active(BAMBU_STATE_IDLE) && !bambu_state_job_active(BAMBU_STATE_OFFLINE));
    CHECK(!bambu_state_job_active(BAMBU_STATE_FINISH) && bambu_state_job_last(BAMBU_STATE_FINISH));
    CHECK(bambu_state_job_last(BAMBU_STATE_FAILED) && !bambu_state_job_last(BAMBU_STATE_RUNNING));
}

static void test_numbers(void)
{
    float value = -1.0f;
    CHECK(bambu_parse_number("220", &value) && value == 220.0f);
    CHECK(bambu_parse_number("-3.5", &value) && value == -3.5f);
    CHECK(!bambu_parse_number("", &value));
    CHECK(!bambu_parse_number(NULL, &value));
    CHECK(!bambu_parse_number("unknown", &value));
    CHECK(!bambu_parse_number("unavailable", &value));
    CHECK(!bambu_parse_number("12abc", &value));
    CHECK(!bambu_parse_number("nan", &value));
    CHECK(!bambu_parse_number("inf", &value));
    CHECK(!bambu_parse_number("1e30", &value));

    int percent = -1;
    CHECK(bambu_progress_percent("57", &percent) && percent == 57);
    CHECK(bambu_progress_percent("56.6", &percent) && percent == 57);
    CHECK(bambu_progress_percent("140", &percent) && percent == 100);
    CHECK(bambu_progress_percent("-4", &percent) && percent == 0);
    CHECK(!bambu_progress_percent("unknown", &percent));
    CHECK(!bambu_progress_percent("nan", &percent));

    int minutes = -1;
    CHECK(bambu_remaining_minutes("17", "min", &minutes) && minutes == 17);
    CHECK(bambu_remaining_minutes("0.2833", "h", &minutes) && minutes == 17);
    CHECK(bambu_remaining_minutes("1", "h", &minutes) && minutes == 60);
    CHECK(bambu_remaining_minutes("6.5", "h", &minutes) && minutes == 390);
    CHECK(bambu_remaining_minutes("17.2", "min", &minutes) && minutes == 18); /* rounded up */
    CHECK(bambu_remaining_minutes("90", "s", &minutes) && minutes == 2);
    CHECK(bambu_remaining_minutes("0", "min", &minutes) && minutes == 0);
    CHECK(!bambu_remaining_minutes("17", "", &minutes));    /* the number alone never decides the unit */
    CHECK(!bambu_remaining_minutes("17", NULL, &minutes));
    CHECK(!bambu_remaining_minutes("17", "d", &minutes));
    CHECK(!bambu_remaining_minutes("-1", "min", &minutes));
    CHECK(!bambu_remaining_minutes("unknown", "min", &minutes));
    CHECK(!bambu_remaining_minutes("1e9", "h", &minutes));
}

static void test_actions(void)
{
    /* every state with every button usable and a synchronised panel */
    for (int s = BAMBU_STATE_MISSING; s <= BAMBU_STATE_FAILED; s++) {
        const bambu_actions_t a = bambu_actions((bambu_state_t)s, true, false, true, true, true);
        const bool running = s == BAMBU_STATE_RUNNING;
        const bool paused = s == BAMBU_STATE_PAUSE;
        CHECK(a.primary_enabled == (running || paused));
        CHECK(a.stop_enabled == (running || paused));
        CHECK(a.primary_resume == paused);
    }
    bambu_actions_t a = bambu_actions(BAMBU_STATE_RUNNING, true, false, true, false, true);
    CHECK(a.primary_enabled && !a.primary_resume && a.stop_enabled);
    a = bambu_actions(BAMBU_STATE_PAUSE, true, false, true, false, true); /* resume entity missing */
    CHECK(!a.primary_enabled && a.primary_resume && a.stop_enabled);
    a = bambu_actions(BAMBU_STATE_PAUSE, true, false, false, true, false);
    CHECK(a.primary_enabled && a.primary_resume && !a.stop_enabled);
    a = bambu_actions(BAMBU_STATE_RUNNING, false, false, true, true, true); /* HA not connected or not synchronised */
    CHECK(!a.primary_enabled && !a.stop_enabled);
    a = bambu_actions(BAMBU_STATE_RUNNING, true, true, true, true, true); /* a command is in flight */
    CHECK(!a.primary_enabled && !a.stop_enabled);
    a = bambu_actions(BAMBU_STATE_PAUSE, true, true, true, true, true);
    CHECK(!a.primary_enabled && a.primary_resume && !a.stop_enabled);

    CHECK(bambu_button_usable("unknown")); /* normal before the first press */
    CHECK(bambu_button_usable("2026-10-06T12:00:00+00:00"));
    CHECK(!bambu_button_usable("unavailable"));
    CHECK(!bambu_button_usable(""));
    CHECK(!bambu_button_usable(NULL));

    CHECK(bambu_command_confirmed(BAMBU_CMD_PAUSE, BAMBU_STATE_PAUSE));
    CHECK(!bambu_command_confirmed(BAMBU_CMD_PAUSE, BAMBU_STATE_RUNNING));
    CHECK(bambu_command_confirmed(BAMBU_CMD_RESUME, BAMBU_STATE_RUNNING));
    CHECK(!bambu_command_confirmed(BAMBU_CMD_RESUME, BAMBU_STATE_PAUSE));
    CHECK(bambu_command_confirmed(BAMBU_CMD_STOP, BAMBU_STATE_IDLE));
    CHECK(bambu_command_confirmed(BAMBU_CMD_STOP, BAMBU_STATE_FAILED));
    CHECK(bambu_command_confirmed(BAMBU_CMD_STOP, BAMBU_STATE_FINISH));
    CHECK(!bambu_command_confirmed(BAMBU_CMD_STOP, BAMBU_STATE_RUNNING));
    CHECK(!bambu_command_confirmed(BAMBU_CMD_STOP, BAMBU_STATE_OFFLINE));
}

static void test_slots(void)
{
    CHECK(k_bambu_printer_binding_count == 11);
    /* every extra slot is used exactly once and the generic ones once each */
    int used[16] = {0};
    for (size_t i = 0; i < k_bambu_printer_binding_count; i++) {
        const int slot = k_bambu_printer_bindings[i].slot;
        if (slot >= 0) {
            CHECK(slot < BAMBU_EXTRA_COUNT);
            used[slot]++;
        }
    }
    for (int i = 0; i < BAMBU_EXTRA_COUNT; i++) CHECK(used[i] == 1);

    cJSON *json = cJSON_Parse("{\"entity_id\":\"sensor.st\",\"nozzle_temp_entity_id\":\"sensor.n\",\"bed_temp_entity_id\":\"sensor.b\","
                              "\"progress_entity_id\":\"sensor.p\",\"remaining_time_entity_id\":\"sensor.r\","
                              "\"pause_entity_id\":\"button.pa\",\"resume_entity_id\":\"button.re\",\"stop_entity_id\":\"button.st\","
                              "\"stage_entity_id\":\"sensor.sg\",\"job_name_entity_id\":\"sensor.jb\",\"camera_entity_id\":\"camera.printer\"}");
    char primary[APP_MAX_ENTITY_ID_LEN] = "";
    char secondary[APP_MAX_ENTITY_ID_LEN] = "";
    char tertiary[APP_MAX_ENTITY_ID_LEN] = "";
    char extra[BAMBU_EXTRA_COUNT][APP_MAX_ENTITY_ID_LEN];
    memset(extra, 0, sizeof(extra));
    bambu_printer_slots_from_json(json, primary, secondary, tertiary, extra, BAMBU_EXTRA_COUNT);
    CHECK(!strcmp(primary, "sensor.st") && !strcmp(secondary, "sensor.n") && !strcmp(tertiary, "sensor.b"));
    CHECK(!strcmp(extra[UI_BAMBU_EXTRA_PROGRESS], "sensor.p") && !strcmp(extra[UI_BAMBU_EXTRA_REMAINING], "sensor.r"));
    CHECK(!strcmp(extra[UI_BAMBU_EXTRA_PAUSE], "button.pa") && !strcmp(extra[UI_BAMBU_EXTRA_RESUME], "button.re"));
    CHECK(!strcmp(extra[UI_BAMBU_EXTRA_STOP], "button.st") && !strcmp(extra[UI_BAMBU_EXTRA_STAGE], "sensor.sg"));
    CHECK(!strcmp(extra[UI_BAMBU_EXTRA_JOB], "sensor.jb"));
    CHECK(!strcmp(extra[UI_BAMBU_EXTRA_CAMERA], "camera.printer"));
    cJSON_Delete(json);

    /* an overlong id is not copied (never truncated), a missing key leaves the slot untouched */
    char long_id[200];
    memset(long_id, 'a', sizeof(long_id) - 1);
    long_id[sizeof(long_id) - 1] = '\0';
    json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "nozzle_temp_entity_id", long_id);
    memset(secondary, 0, sizeof(secondary));
    bambu_printer_slots_from_json(json, primary, secondary, tertiary, extra, BAMBU_EXTRA_COUNT);
    CHECK(secondary[0] == '\0');
    cJSON_Delete(json);
}

int main(void)
{
    test_states();
    test_numbers();
    test_actions();
    test_slots();
    printf("%d checks, %d failed\n", s_checks, s_failures);
    if (s_failures == 0) printf("PASS: bambu printer policy and slot mapping\n");
    return s_failures == 0 ? 0 : 1;
}
