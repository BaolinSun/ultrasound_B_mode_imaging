#ifndef UB_CPU1_FSBL_H
#define UB_CPU1_FSBL_H
/* Boot observes only. CPU0 application owns explicit web shutdown requests. */
#include "cpu1_control.h"
#include "xil_io.h"
#include "xil_printf.h"
static void cpu1_fsbl_before_handoff(void)
{
    unsigned ctrl=Xil_In32(CPU1_RESET_CTRL);
    xil_printf("@CPU1_FSBL startup=observe disabled=%u control=%08x\r\n",
        (unsigned)((ctrl&CPU1_DISABLED_MASK)==CPU1_DISABLED_MASK),ctrl);
}
#endif
