#ifndef UB_AFE_POWER_H
#define UB_AFE_POWER_H
#include "xil_types.h"
void afe_power_init(void);
void afe_power_poll(void);
int afe_power_allowed(void);
int afe_power_idle(void);
int afe_power_command(const char *op,u32 owner);
const char *afe_power_state_name(void);
const char *afe_power_error(void);
u32 afe_power_flags(void);
#endif
