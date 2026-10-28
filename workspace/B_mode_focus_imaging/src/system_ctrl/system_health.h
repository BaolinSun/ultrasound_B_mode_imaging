#ifndef SYSTEM_HEALTH_H
#define SYSTEM_HEALTH_H
#include <stdint.h>
enum { HEALTH_BOOT=1U, HEALTH_DDR=2U, HEALTH_AFE=4U, HEALTH_TX=8U,
       HEALTH_LINK=16U, HEALTH_UART=32U, HEALTH_TEMP=64U, HEALTH_CONFIG=128U };
void system_health_init(void);
void system_health_boot_complete(void);
void system_health_poll(void);
uint32_t system_health_faults(void);
#endif
