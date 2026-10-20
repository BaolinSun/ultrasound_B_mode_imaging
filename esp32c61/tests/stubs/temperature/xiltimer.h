#ifndef TEMP_TEST_TIMER_H
#define TEMP_TEST_TIMER_H
#include <stdint.h>
typedef uint64_t XTime;
#if defined(TEMPERATURE_TEST_UNPAREN_FREQ)
/* Same macro shape as the current Vitis BSP; host ticks still count us. */
#define COUNTS_PER_SECOND 2000000U/2
#else
#define COUNTS_PER_SECOND 1000000U
#endif
void XTime_GetTime(XTime*);
#endif
