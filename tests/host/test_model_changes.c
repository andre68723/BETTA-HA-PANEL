/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/model_reconcile.h"
/* Include the real cache so the wrap test can position its counter without
 * billions of updates. All change/consume operations use the public API. */
#include "../../main/ha/ha_model.c"

static ha_state_t sample;
static void update(const char *id, const char *value)
{
    memset(&sample, 0, sizeof(sample));
    snprintf(sample.entity_id, sizeof(sample.entity_id), "%s", id);
    snprintf(sample.state, sizeof(sample.state), "%s", value);
    assert(ha_model_upsert_state(&sample) == ESP_OK);
}

int main(void)
{
    assert(ha_model_init() == ESP_OK);
    ha_model_change_t batch[2];
    size_t count;
    uint32_t next, cursor = ha_model_state_revision();
    assert(ha_model_changes(cursor, batch, 2, &count, &next) == ESP_OK && count == 0 && next == cursor);
    update("sensor.b", "1");
    update("sensor.a", "1");
    update("sensor.c", "1");
    assert(ha_model_changes(cursor, batch, 2, &count, &next) == ESP_OK && count == 2);
    assert(!strcmp(batch[0].entity_id, "sensor.b") && !strcmp(batch[1].entity_id, "sensor.a"));
    assert(next == batch[1].version.revision && next != ha_model_state_revision());
    /* Do not acknowledge on lock failure: rereading the old cursor retains all pending work. */
    uint32_t retry_next;
    assert(ha_model_changes(cursor, batch, 2, &count, &retry_next) == ESP_OK && retry_next == next);
    cursor = next;
    update("sensor.b", "2"); /* A newer update while an older batch was being consumed. */
    assert(ha_model_changes(cursor, batch, 2, &count, &next) == ESP_OK && count == 2);
    assert(!strcmp(batch[0].entity_id, "sensor.c") && !strcmp(batch[1].entity_id, "sensor.b"));
    ha_state_version_t delivered;
    assert(ha_model_get_state_versioned("sensor.b", &sample, &delivered));
    ui_applied_state_t seen = {.revision = delivered.revision, .valid = true};
    assert(ui_model_already_applied(&seen, batch[1].version.revision));
    assert(delivered.slot == batch[1].version.slot && !strcmp(sample.state, "2"));
    cursor = next;
    assert(ha_model_upsert_state(&sample) == ESP_OK);
    assert(ha_model_state_revision() == cursor); /* Identical state is not another recovery job. */
    assert(ha_model_set_entity_name("sensor.b", "New name") == ESP_OK);
    assert(ha_model_changes(cursor, batch, 2, &count, &next) == ESP_OK && count == 1);
    assert(!strcmp(batch[0].entity_id, "sensor.b"));
    assert(!ui_model_already_applied(&seen, batch[0].version.revision));
    cursor = next;
    ha_model_reset();
    assert(ha_model_changes(cursor, batch, 2, &count, &next) == ESP_OK && count == 0 && next != cursor);
    cursor = next;
    update("sensor.new_after_reset", "on");
    assert(ha_model_changes(cursor, batch, 2, &count, &next) == ESP_OK && count == 1);
    assert(!strcmp(batch[0].entity_id, "sensor.new_after_reset"));
    ha_model_reset();
    s_state_revision = UINT32_MAX - 1;
    cursor = s_state_revision;
    update("sensor.before_wrap", "1");
    update("sensor.after_wrap", "1");
    assert(ha_model_changes(cursor, batch, 1, &count, &next) == ESP_OK && count == 1 && next == UINT32_MAX);
    assert(!strcmp(batch[0].entity_id, "sensor.before_wrap"));
    assert(ha_model_changes(next, batch, 1, &count, &cursor) == ESP_OK && count == 1 && cursor == 0);
    assert(!strcmp(batch[0].entity_id, "sensor.after_wrap"));
    seen = (ui_applied_state_t){.revision = 0, .valid = true};
    assert(ui_model_already_applied(&seen, UINT32_MAX));
    seen.valid = false;
    assert(!ui_model_already_applied(&seen, 0));
    puts("Model changes: partial/retried batches, event dedup, concurrent updates, names, reset and revision wrap passed");
    return 0;
}
