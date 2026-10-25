#ifndef UB_CPU1_FSBL_H
#define UB_CPU1_FSBL_H
/* Copied into the generated FSBL with the same common core as the app. */
#include "cpu1_platform.h"
#include "cpu1_control.c"
#include "xil_printf.h"
static void cpu1_fsbl_before_handoff(void)
{
    cpu1_result r = cpu1_platform_disable();
    xil_printf("@CPU1_FSBL disabled=%u before=%08x after=%08x error=%s lock_restored=%u\r\n",
        (unsigned)((r.after & CPU1_DISABLED_MASK) == CPU1_DISABLED_MASK),
        (unsigned)r.before, (unsigned)r.after, cpu1_error_name(r.error), r.lock_restored);
    if (r.error != CPU1_OK)
        xil_printf("@CPU1_FSBL WARNING continuing handoff despite disable failure\r\n");
    /* No boot failure, CPU0 reset, clock/PLL change, or image admission gate. */
}
#endif
