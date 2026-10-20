#include <stdint.h>
typedef uint64_t XTime;
#if defined(FRAME_STORE_TEST_UNPAREN_FREQ)
/* Deliberately mirror Vitis 2026.1's unparenthesized CPU_FREQ/2 macro. */
#define COUNTS_PER_SECOND 2000U/2
#else
#define COUNTS_PER_SECOND 1000U
#endif
void XTime_GetTime(XTime *value);
