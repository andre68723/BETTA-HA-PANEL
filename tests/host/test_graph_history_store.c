/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ui/graph_history_store.h"
#include "util/atomic_file.h"
#include "util/transfer_deadline.h"
int main(void)
{
    char directory[] = "/tmp/betta-graph-store-XXXXXX";
    assert(mkdtemp(directory));
    char retained[160], orphan[160], temp[160], unrelated[160], pending[160];
    snprintf(retained,sizeof(retained),"%s/keep.grph",directory);
    snprintf(orphan,sizeof(orphan),"%s/remove.grph",directory);
    snprintf(temp,sizeof(temp),"%s/keep.grph.tmp",directory);
    snprintf(unrelated,sizeof(unrelated),"%s/readme.txt",directory);
    snprintf(pending,sizeof(pending),"%s/pending.grph",directory);
    cJSON *layout = cJSON_Parse("{\"pages\":[{\"widgets\":[{\"id\":\"keep\",\"type\":\"graph\"},{\"id\":\"pending\",\"type\":\"graph\"}]}]}");
    assert(layout);
    assert(atomic_file_write(retained,"old",3) == ESP_OK);
    assert(atomic_file_write(orphan,"unused",6) == ESP_OK);
    assert(atomic_file_write(temp,"interrupted",11) == ESP_OK);
    assert(atomic_file_write(unrelated,"unrelated",9) == ESP_OK);
    assert(graph_history_store_cleanup(directory,NULL) != ESP_OK);
    assert(access(orphan,F_OK) == 0);
    assert(graph_history_store_cleanup(directory,layout) == ESP_OK);
    assert(access(retained,F_OK) == 0 && access(orphan,F_OK) != 0 && access(temp,F_OK) != 0 && access(unrelated,F_OK) == 0);
    assert(graph_history_store_save(directory,orphan,layout,"stale",5,4*1024*1024,0) == ESP_ERR_NOT_FOUND);
    assert(access(orphan,F_OK) != 0);
    assert(graph_history_store_save(directory,retained,layout,"new",3,4*1024*1024,4*1024*1024-512*1024) == ESP_ERR_NO_MEM);
    char bytes[4] = {0}; FILE *f = fopen(retained,"rb"); assert(f); assert(fread(bytes,1,3,f)==3); fclose(f); assert(strcmp(bytes,"old")==0);
    char *large = calloc(1024*1024,1); assert(large);
    assert(graph_history_store_save(directory,pending,layout,large,1024*1024,4*1024*1024,0) == ESP_ERR_NO_MEM);
    assert(graph_history_store_save(directory,retained,layout,large,1024*1024,4*1024*1024,0) == ESP_OK);
    assert(graph_history_store_save(directory,pending,layout,"x",1,4*1024*1024,1024*1024) == ESP_ERR_NO_MEM);
    assert(graph_history_store_save(directory,retained,layout,"new",3,4*1024*1024,1024*1024) == ESP_OK);
    assert(graph_history_store_save(directory,pending,layout,"x",1,4*1024*1024,3) == ESP_OK);
    char safe[32]; graph_history_store_id("a/b.c",safe,sizeof(safe)); assert(strcmp(safe,"a_b_c")==0);
    /* Atomic rename failure leaves a complete file and removes the temporary candidate. */
    assert(mkdir(temp,0700) == 0);
    assert(graph_history_store_save(directory,retained,layout,"broken",6,4*1024*1024,4) == ESP_FAIL);
    f=fopen(retained,"rb"); assert(f); memset(bytes,0,sizeof(bytes)); assert(fread(bytes,1,3,f)==3); fclose(f); assert(strcmp(bytes,"new")==0);
    assert(rmdir(temp)==0);
    cJSON_Delete(layout);
    layout=cJSON_Parse("{\"pages\":[{\"widgets\":[{\"id\":\"keep\",\"type\":\"graph\",\"graph_ha_history\":true}]}]}");
    assert(graph_history_store_cleanup(directory,layout)==ESP_OK);
    assert(access(retained,F_OK)!=0 && access(pending,F_OK)!=0);
    cJSON_Delete(layout); free(large); unlink(unrelated); assert(rmdir(directory)==0);
    transfer_deadline_t d; transfer_deadline_init(&d,100,30000,180000);
    assert(!transfer_deadline_expired(&d,30099)); assert(transfer_deadline_expired(&d,30100));
    transfer_deadline_progress(&d,25000); assert(!transfer_deadline_expired(&d,54000));
    for(int64_t now=25000; now<180100; now+=10000) transfer_deadline_progress(&d,now);
    assert(transfer_deadline_expired(&d,180100));
    puts("graph history store: quota, reserve, cleanup, stale jobs, atomic failure and transfer deadlines passed");
    return 0;
}
