/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host tests of the alarm panel state: parsing, the actions offered per state and feature set, and
 * when an action needs a code.
 */
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ha/ha_alarm_state.h"

static int g_checks, g_failed;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_INT(a, b) do { g_checks++; long va = (long)(a), vb = (long)(b); if (va != vb) { g_failed++; printf("FAIL %s:%d %s == %s (%ld vs %ld)\n", __FILE__, __LINE__, #a, #b, va, vb); } } while (0)

static void test_alarm_state(void)
{
    CHECK_INT(ha_alarm_state_parse("disarmed"), HA_ALARM_DISARMED);
    CHECK_INT(ha_alarm_state_parse("armed_custom_bypass"), HA_ALARM_ARMED_CUSTOM_BYPASS);
    CHECK_INT(ha_alarm_state_parse("unavailable"), HA_ALARM_UNKNOWN);
    CHECK_INT(ha_alarm_state_parse(NULL), HA_ALARM_UNKNOWN);
    CHECK(ha_alarm_state_armed(HA_ALARM_ARMED_NIGHT));
    CHECK(!ha_alarm_state_armed(HA_ALARM_ARMING)); /* not armed yet */
    CHECK(!ha_alarm_state_armed(HA_ALARM_TRIGGERED));
    CHECK(ha_alarm_state_alert(HA_ALARM_TRIGGERED));
    CHECK(ha_alarm_state_alert(HA_ALARM_PENDING));
    CHECK(!ha_alarm_state_alert(HA_ALARM_ARMED_HOME));
    CHECK(strcmp(ha_alarm_state_key(HA_ALARM_ARMED_HOME), "alarm.state.armed_home") == 0);
    CHECK(strcmp(ha_alarm_state_key(HA_ALARM_UNKNOWN), "common.unknown") == 0);
    CHECK(strcmp(ha_alarm_action_service(HA_ALARM_ACTION_ARM_NIGHT), "alarm_arm_night") == 0);
    CHECK(strcmp(ha_alarm_action_service(HA_ALARM_ACTION_DISARM), "alarm_disarm") == 0);
    CHECK(strcmp(ha_alarm_action_key(HA_ALARM_ACTION_ARM_VACATION), "alarm.action.arm_vacation") == 0);

    ha_alarm_action_t actions[HA_ALARM_ACTION_COUNT];
    /* Disarmed: the arm modes the features announce, in display order. */
    size_t n = ha_alarm_actions(HA_ALARM_DISARMED, "{\"supported_features\":7}", actions, HA_ALARM_ACTION_COUNT);
    CHECK_INT((int)n, 3);
    CHECK_INT(actions[0], HA_ALARM_ACTION_ARM_HOME);
    CHECK_INT(actions[1], HA_ALARM_ACTION_ARM_AWAY);
    CHECK_INT(actions[2], HA_ALARM_ACTION_ARM_NIGHT);
    n = ha_alarm_actions(HA_ALARM_DISARMED, "{\"supported_features\":49}", actions, HA_ALARM_ACTION_COUNT);
    CHECK_INT((int)n, 3); /* home, custom bypass, vacation */
    CHECK_INT(actions[1], HA_ALARM_ACTION_ARM_VACATION);
    CHECK_INT(actions[2], HA_ALARM_ACTION_ARM_CUSTOM_BYPASS);
    CHECK_INT((int)ha_alarm_actions(HA_ALARM_DISARMED, "{}", actions, HA_ALARM_ACTION_COUNT), 0);
    CHECK_INT((int)ha_alarm_actions(HA_ALARM_DISARMED, NULL, actions, HA_ALARM_ACTION_COUNT), 0);
    /* Armed, arming, pending, triggered: only disarm. */
    for (int state = HA_ALARM_ARMED_HOME; state <= HA_ALARM_PENDING; state++) {
        if (state == HA_ALARM_DISARMING) continue;
        n = ha_alarm_actions((ha_alarm_state_t)state, "{\"supported_features\":7}", actions, HA_ALARM_ACTION_COUNT);
        CHECK_INT((int)n, 1);
        CHECK_INT(actions[0], HA_ALARM_ACTION_DISARM);
    }
    CHECK_INT((int)ha_alarm_actions(HA_ALARM_TRIGGERED, "{}", actions, HA_ALARM_ACTION_COUNT), 1);
    CHECK_INT((int)ha_alarm_actions(HA_ALARM_DISARMING, "{\"supported_features\":7}", actions, HA_ALARM_ACTION_COUNT), 0);
    CHECK_INT((int)ha_alarm_actions(HA_ALARM_UNKNOWN, "{\"supported_features\":7}", actions, HA_ALARM_ACTION_COUNT), 0);
    CHECK_INT((int)ha_alarm_actions(HA_ALARM_DISARMED, "{\"supported_features\":7}", actions, 2), 2); /* bounded */

    CHECK_INT(ha_alarm_code_format("{\"code_format\":\"number\"}"), HA_ALARM_CODE_NUMBER);
    CHECK_INT(ha_alarm_code_format("{\"code_format\":\"text\"}"), HA_ALARM_CODE_TEXT);
    CHECK_INT(ha_alarm_code_format("{\"code_format\":null}"), HA_ALARM_CODE_NONE);
    CHECK_INT(ha_alarm_code_format("{}"), HA_ALARM_CODE_NONE);
    CHECK_INT(ha_alarm_code_format(NULL), HA_ALARM_CODE_NONE);
    /* No code format: nothing asks for a code. */
    CHECK(!ha_alarm_action_needs_code(HA_ALARM_ACTION_DISARM, "{\"supported_features\":7}"));
    CHECK(!ha_alarm_action_needs_code(HA_ALARM_ACTION_ARM_HOME, "{\"code_format\":null}"));
    /* A code format: disarm always, arming unless code_arm_required is false. */
    CHECK(ha_alarm_action_needs_code(HA_ALARM_ACTION_DISARM, "{\"code_format\":\"number\",\"code_arm_required\":false}"));
    CHECK(ha_alarm_action_needs_code(HA_ALARM_ACTION_ARM_AWAY, "{\"code_format\":\"number\"}"));
    CHECK(ha_alarm_action_needs_code(HA_ALARM_ACTION_ARM_AWAY, "{\"code_format\":\"number\",\"code_arm_required\":true}"));
    CHECK(!ha_alarm_action_needs_code(HA_ALARM_ACTION_ARM_AWAY, "{\"code_format\":\"number\",\"code_arm_required\":false}"));

    /* The layout's alarm_actions: names, mask and the filter over the offered actions. */
    ha_alarm_action_t named;
    CHECK(ha_alarm_action_from_name("arm_night", &named) && named == HA_ALARM_ACTION_ARM_NIGHT);
    CHECK(ha_alarm_action_from_name("disarm", &named) && named == HA_ALARM_ACTION_DISARM);
    CHECK(!ha_alarm_action_from_name("trigger", &named));
    CHECK(!ha_alarm_action_from_name(NULL, &named));
    cJSON *list = cJSON_Parse("[\"arm_home\",\"bogus\",\"arm_away\"]");
    const unsigned mask = ha_alarm_action_mask_parse(list);
    cJSON_Delete(list);
    CHECK_INT(mask, (1U << HA_ALARM_ACTION_ARM_HOME) | (1U << HA_ALARM_ACTION_ARM_AWAY));
    CHECK_INT(ha_alarm_action_mask_parse(NULL), 0);
    ha_alarm_action_t offered[HA_ALARM_ACTION_COUNT];
    size_t count = ha_alarm_actions(HA_ALARM_DISARMED, "{\"supported_features\":7}", offered, HA_ALARM_ACTION_COUNT);
    count = ha_alarm_actions_filter(offered, count, mask);
    CHECK_INT((int)count, 2);
    CHECK_INT(offered[1], HA_ALARM_ACTION_ARM_AWAY);
    /* Disarm left out of the list: an armed panel offers nothing, so a tap cannot disarm it. */
    count = ha_alarm_actions(HA_ALARM_ARMED_HOME, "{}", offered, HA_ALARM_ACTION_COUNT);
    CHECK_INT((int)ha_alarm_actions_filter(offered, count, mask), 0);
    CHECK_INT((int)ha_alarm_actions_filter(offered, count, 0), 1); /* no list: everything */
}

int main(void)
{
    test_alarm_state();
    printf("alarm state: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
