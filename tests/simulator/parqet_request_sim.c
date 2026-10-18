/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* The simulator has no Home Assistant: parqet_request_perf() answers from the figures a regression or fixture stored with sim_parqet_set(). */
#include <string.h>

#include "ha/parqet_request.h"

#define SIM_SLOTS 12

static struct {
    bool used;
    char portfolio_id[40];
    char interval[8];
    parqet_perf_t perf;
} s_sim[SIM_SLOTS];

void sim_parqet_set(const char *portfolio_id, const char *interval, const parqet_perf_t *perf)
{
    for (int i = 0; i < SIM_SLOTS; i++) {
        if (!s_sim[i].used || (strcmp(s_sim[i].portfolio_id, portfolio_id) == 0 && strcmp(s_sim[i].interval, interval) == 0)) {
            s_sim[i].used = true;
            strlcpy(s_sim[i].portfolio_id, portfolio_id, sizeof(s_sim[i].portfolio_id));
            strlcpy(s_sim[i].interval, interval, sizeof(s_sim[i].interval));
            s_sim[i].perf = *perf;
            strlcpy(s_sim[i].perf.interval, interval, sizeof(s_sim[i].perf.interval));
            return;
        }
    }
}

void sim_parqet_clear(void)
{
    memset(s_sim, 0, sizeof(s_sim));
}

esp_err_t parqet_request_perf(const char *entry_id, const char *portfolio_id, const char *interval, void *user, parqet_perf_cb_t cb)
{
    if (entry_id == NULL || portfolio_id == NULL || interval == NULL || cb == NULL) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < SIM_SLOTS; i++) {
        if (s_sim[i].used && strcmp(s_sim[i].portfolio_id, portfolio_id) == 0 && strcmp(s_sim[i].interval, interval) == 0) {
            cb(user, ESP_OK, &s_sim[i].perf);
            return ESP_OK;
        }
    }
    cb(user, ESP_ERR_TIMEOUT, NULL); /* nothing stored: like a reply that never came */
    return ESP_OK;
}

void parqet_request_cancel(void *user)
{
    (void)user;
}
