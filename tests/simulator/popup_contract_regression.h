/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdint.h>
/* Read-only audit after the UI has settled, including actual dialog cases. */
void sim_popup_contract_check(void);
void sim_popup_contract_regression(void (*advance)(uint32_t));
