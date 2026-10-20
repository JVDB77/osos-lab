/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CFW_BALANCE_H
#define CFW_BALANCE_H
#include <stdint.h>

void cfw_balance_load_preferences(void);
int cfw_balance_action(const char *action, uint32_t argument);
/* Attenuates one channel of interleaved stereo samples in place. */
void cfw_balance_apply(int16_t *samples, uint32_t frames);
#endif
