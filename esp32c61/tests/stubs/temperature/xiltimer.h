#ifndef TEMP_TEST_TIMER_H
#define TEMP_TEST_TIMER_H
#include <stdint.h>
typedef uint64_t XTime;
#define COUNTS_PER_SECOND 1000000U
void XTime_GetTime(XTime*);
#endif
