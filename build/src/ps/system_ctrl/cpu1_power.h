#ifndef UB_CPU1_POWER_H
#define UB_CPU1_POWER_H
void cpu1_power_init(void);
const char *cpu1_power_disable(void); /* NULL success; explicit foreground command only. */
unsigned cpu1_power_is_disabled(void);
#endif
