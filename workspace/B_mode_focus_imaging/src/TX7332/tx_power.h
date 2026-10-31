#ifndef UB_TX_POWER_H
#define UB_TX_POWER_H
#include "xil_types.h"
void tx_power_init(void);
int tx_power_confirm_config_idle(void);
int tx_power_config_idle_retained(void);
u32 tx_power_operation_sequence(void);
void tx_power_poll(void);
int tx_power_allowed(void);
int tx_power_command(const char *,u32);
int tx_power_idle(void);
int tx_power_sleep_confirmed(void);
const char *tx_power_state_name(void);
const char *tx_power_error(void);
u32 tx_power_flags(void);
#endif
