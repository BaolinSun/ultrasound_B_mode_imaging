#ifndef UB_CPU0_IDLE_H
#define UB_CPU0_IDLE_H
void cpu0_idle_init(void);
unsigned cpu0_idle_enabled(void); /* feature ready, not instantaneous CPU state */
int cpu0_idle_quiet(void); /* both chips SLEEP and accepted transports idle */
void cpu0_foreground_poll(void); /* main-only, never recursively from ISR/transfer */
#endif
