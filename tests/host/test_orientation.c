/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/orientation_policy.h"
#include "ui/display_geometry.h"
#include "layout/layout_orientation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sensor_policy(void)
{
    const float axes[][3] = {{1,0,0}, {0,1,0}, {-1,0,0}, {0,-1,0}};
    const int degrees[] = {90,0,270,180};
    for (int i = 0; i < 4; i++) {
        orientation_policy_t p = {.candidate = -1};
        int requested = -1, current = (degrees[i] + 180) % 360;
        assert(!orientation_policy_update(&p, axes[i][0], axes[i][1], axes[i][2], 100, current, &requested));
        assert(!orientation_policy_update(&p, axes[i][0], axes[i][1], axes[i][2], 1099, current, &requested));
        assert(orientation_policy_update(&p, axes[i][0], axes[i][1], axes[i][2], 1100, current, &requested));
        assert(requested == degrees[i]);
        assert(orientation_policy_update(&p, axes[i][0], axes[i][1], axes[i][2], 1300, current, &requested));
        assert(!orientation_policy_update(&p, axes[i][0], axes[i][1], axes[i][2], 1500, requested, &requested));
    }
    orientation_policy_t p = {.candidate = -1};
    int requested;
    assert(!orientation_policy_update(&p, 0,1,0, 0,90,&requested));
    assert(!orientation_policy_update(&p, 0,0,1, 1100,90,&requested)); /* flat resets dwell */
    assert(!orientation_policy_update(&p, 0,1,0, 1200,90,&requested));
    assert(!orientation_policy_update(&p, .7f,.7f,0, 2300,90,&requested)); /* diagonal */
    assert(!orientation_policy_update(&p, 0,2,0, 3400,90,&requested)); /* shaking */
    assert(!orientation_policy_update(&p, NAN,1,0, 4500,90,&requested));
    assert(!orientation_policy_update(&p, 0,1,0, 5000,90,&requested));
    assert(!orientation_policy_update(&p, 0,1,0, 100,90,&requested)); /* clock reset */
    assert(orientation_policy_update(&p, 0,1,0, 1100,90,&requested));
}
static void boot_policy(void)
{
    const float axes[][3] = {{1,0,0}, {0,1,0}, {-1,0,0}, {0,-1,0}};
    const int degrees[] = {90,0,270,180};
    for (int i = 0; i < 4; i++) {
        assert(orientation_boot_fallback(true, degrees[i]) == degrees[i]);
        assert(orientation_boot_fallback(false, degrees[i]) == 90);
        orientation_boot_policy_t p = {.candidate = -1};
        int requested = -1;
        for (int sample = 0; sample < 3; sample++) {
            assert(orientation_boot_update(&p, axes[i][0], axes[i][1], axes[i][2], &requested) == (sample == 2));
            assert(requested == (sample == 2 ? degrees[i] : -1));
        }
    }
    assert(orientation_boot_fallback(true, -1) == 90);
    assert(orientation_boot_fallback(true, 45) == 90);
    assert(orientation_boot_fallback(true, 360) == 90);
    orientation_boot_policy_t p = {.candidate = -1};
    int requested = -1;
    assert(!orientation_boot_update(&p, -1,0,0, &requested));
    assert(!orientation_boot_update(&p, -1,0,0, &requested));
    assert(!orientation_boot_update(&p, 0,0,1, &requested)); /* Flat placement discards both previous samples. */
    assert(!orientation_boot_update(&p, -1,0,0, &requested));
    assert(!orientation_boot_update(&p, -1,0,0, &requested));
    assert(!orientation_boot_update(&p, 0,1,0, &requested)); /* A changed mount needs three new samples. */
    assert(!orientation_boot_update(&p, 0,1,0, &requested));
    assert(orientation_boot_update(&p, 0,1,0, &requested) && requested == 0);
    const float uncertain[][3] = {{NAN,0,0}, {INFINITY,0,0}, {.7f,.7f,0}, {0,2,0}};
    for (int i = 0; i < 4; i++) {
        requested = 270;
        assert(!orientation_boot_update(&p, uncertain[i][0], uncertain[i][1], uncertain[i][2], &requested));
        assert(requested == 270 && p.samples == 0);
    }
    assert(!orientation_boot_update(NULL, 1,0,0, &requested));
    assert(!orientation_boot_update(&p, 1,0,0, NULL));
}
static void remembered_orientation(void)
{
    orientation_memory_policy_t p = {.candidate = -1};
    assert(!orientation_memory_should_save(&p, true,270,-1,100));
    assert(!orientation_memory_should_save(&p, true,270,-1,5099));
    assert(orientation_memory_should_save(&p, true,270,-1,5100));
    orientation_memory_save_result(&p, 5100, false);
    assert(!orientation_memory_should_save(&p, true,270,-1,35099));
    assert(orientation_memory_should_save(&p, true,270,-1,35100));
    orientation_memory_save_result(&p, 35100, true);
    assert(!orientation_memory_should_save(&p, true,270,270,35101));
    assert(!orientation_memory_should_save(&p, true,270,270,60000)); /* No duplicate NVS write. */
    assert(!orientation_memory_should_save(&p, false,90,270,61000)); /* Disabling auto must retain saved mount. */
    assert(!orientation_memory_should_save(&p, false,90,270,70000));
    assert(!orientation_memory_should_save(&p, true,90,270,71000));
    assert(!orientation_memory_should_save(&p, true,180,270,75000)); /* Applied view changed before stable. */
    assert(!orientation_memory_should_save(&p, true,180,270,79999));
    assert(orientation_memory_should_save(&p, true,180,270,80000));
    assert(!orientation_memory_should_save(&p, true,45,270,90000));
    assert(!orientation_memory_should_save(&p, true,0,270,91000));
    assert(!orientation_memory_should_save(&p, true,0,270,100)); /* Restart stable interval after clock reset. */
    assert(orientation_memory_should_save(&p, true,0,270,5100));
    assert(!orientation_memory_should_save(NULL, true,0,270,99999));
}
static int value(cJSON *r, const char *key) { return cJSON_GetObjectItemCaseSensitive(r,key)->valueint; }
static cJSON *widget(cJSON *root, int index)
{
    return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(root,"pages"),0),"widgets"),index);
}
static void layouts(void)
{
    const char *source = "{\"pages\":[{\"widgets\":["
        "{\"type\":\"sensor\",\"rect\":{\"x\":0,\"y\":0,\"w\":630,\"h\":300}},"
        "{\"type\":\"sensor\",\"rect\":{\"x\":640,\"y\":0,\"w\":300,\"h\":300}},"
        "{\"type\":\"sensor\",\"rect\":{\"x\":950,\"y\":0,\"w\":300,\"h\":300}},"
        "{\"type\":\"sensor\",\"rect\":{\"x\":0,\"y\":310,\"w\":1280,\"h\":280}}]}]}";
    cJSON *root = cJSON_Parse(source);
    assert(root && layout_orientation_project(root,true));
    cJSON *a = cJSON_GetObjectItemCaseSensitive(widget(root,0),"rect");
    cJSON *b = cJSON_GetObjectItemCaseSensitive(widget(root,1),"rect");
    cJSON *c = cJSON_GetObjectItemCaseSensitive(widget(root,2),"rect");
    cJSON *d = cJSON_GetObjectItemCaseSensitive(widget(root,3),"rect");
    assert(value(a,"y") == 0 && value(b,"y") == 310 && value(c,"y") == 310);
    assert(value(c,"x") == 310 && value(d,"w") == 720 && value(d,"y") == 620);
    /* Portrait edits do not alter landscape positions. */
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(b,"y"),2000);
    assert(layout_orientation_canonicalize(root));
    assert(value(cJSON_GetObjectItemCaseSensitive(widget(root,1),"rect"),"x") == 640);
    assert(value(cJSON_GetObjectItemCaseSensitive(widget(root,1),"portrait_rect"),"y") == 2000);
    assert(!cJSON_HasObjectItem(root,"orientation"));
    assert(layout_orientation_project(root,true));
    assert(value(root,"canvas_h") == 2300);
    assert(layout_orientation_canonicalize(root));
    assert(layout_orientation_project(root,false));
    assert(value(cJSON_GetObjectItemCaseSensitive(widget(root,3),"rect"),"w") == 1280);
    cJSON_Delete(root);

    root = cJSON_Parse(source);
    cJSON_AddItemToObject(widget(root,2),"portrait_rect",cJSON_Parse("{\"x\":0,\"y\":0,\"w\":300,\"h\":400}"));
    assert(layout_orientation_project(root,true));
    assert(value(cJSON_GetObjectItemCaseSensitive(widget(root,0),"rect"),"y") >= 410);
    assert(layout_orientation_canonicalize(root));
    cJSON *r = cJSON_GetObjectItemCaseSensitive(widget(root,2),"portrait_rect");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(r,"x"),721);
    assert(!layout_orientation_project(root,true));
    cJSON_Delete(root);

    root = cJSON_Parse("{\"orientation\":\"portrait\",\"pages\":[{\"widgets\":[{\"type\":\"sensor\",\"rect\":{\"x\":0,\"y\":1800,\"w\":720,\"h\":500}}]}]}");
    assert(layout_orientation_canonicalize(root));
    r = cJSON_GetObjectItemCaseSensitive(widget(root,0),"rect");
    assert(value(r,"y") == 100 && layout_orientation_rect_valid(widget(root,0),r,false));
    cJSON_Delete(root);
}
int main(void)
{
    sensor_policy();
    boot_policy();
    remembered_orientation();
    assert(APP_VIEW_WIDTH == 1280 && APP_VIEW_HEIGHT == 720);
    assert(display_geometry_set_rotation(0) && APP_VIEW_WIDTH == 720 && APP_VIEW_CONTENT_HEIGHT == 1160);
    assert(display_geometry_set_rotation(180) && display_geometry_portrait());
    assert(display_geometry_set_rotation(270) && APP_VIEW_WIDTH == 1280);
    assert(!display_geometry_set_rotation(45) && display_geometry_rotation() == 270);
    layouts();
    /* A full-width portrait solar card must survive validation and rendering;
     * its landscape maximum remains 640 pixels. */
    assert(layout_orientation_widget_limits("solar_forecast", "", true).max_w == 720);
    assert(layout_orientation_widget_limits("solar_forecast", "", false).max_w == 640);
    cJSON *solar = cJSON_Parse("{\"type\":\"solar_forecast\"}");
    cJSON *full = cJSON_Parse("{\"x\":0,\"y\":660,\"w\":720,\"h\":270}");
    assert(layout_orientation_rect_valid(solar, full, true));
    assert(!layout_orientation_rect_valid(solar, full, false));
    cJSON_Delete(solar); cJSON_Delete(full);
    puts("PASS: orientation boot/fallback, stable-save/retry, dwell, hysteresis, geometry and layout round trips");
    return 0;
}
