#include "xuartps.h"
/* Deliberately strong and directed to UART1, like a regenerated 2026.1 BSP.
 * Test callers must be intercepted by --wrap, never reach this fallback. */
void outbyte(char c) { XUartPs_SendByte(0xe0001000U,(u8)c); }
