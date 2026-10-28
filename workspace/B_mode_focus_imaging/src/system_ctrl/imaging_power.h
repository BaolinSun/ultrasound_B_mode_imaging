#ifndef UB_IMAGING_POWER_H
#define UB_IMAGING_POWER_H
#include "startup_recovery.h"
#include "../AFE5832/afe_power.h"
#include "../TX7332/tx_power.h"
static inline int imaging_power_allowed(void) {return !startup_recovery_blocks() && afe_power_allowed() && tx_power_allowed();}
#endif
