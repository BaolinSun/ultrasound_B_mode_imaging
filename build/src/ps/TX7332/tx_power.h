#ifndef UB_TX_POWER_H
#define UB_TX_POWER_H
#include "xil_types.h"
void tx_power_init(void);
void tx_power_poll(void);
int tx_power_allowed(void);
int tx_power_command(const char *,u32);
int tx_power_idle(void);
const char *tx_power_state_name(void);
const char *tx_power_error(void);
u32 tx_power_flags(void);
#endif
