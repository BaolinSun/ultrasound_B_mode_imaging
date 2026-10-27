#include "cpu1_power.h"
#include <stddef.h>
#include "xil_printf.h"
#include "../../../../common/cpu1_platform.h"
#include "../../../../common/cpu1_control.c"
void cpu1_power_init(void)
{
    unsigned ctrl=Xil_In32(CPU1_RESET_CTRL);
    xil_printf("@CPU1 startup=observe disabled=%u control=%08x\r\n",
        (unsigned)((ctrl&CPU1_DISABLED_MASK)==CPU1_DISABLED_MASK),ctrl);
}
const char *cpu1_power_disable(void)
{
    cpu1_result r = cpu1_platform_disable();
    xil_printf("@CPU1 disabled=%u before=%08x after=%08x error=%s lock_restored=%u\r\n",
        (unsigned)((r.after & CPU1_DISABLED_MASK) == CPU1_DISABLED_MASK),
        (unsigned)r.before, (unsigned)r.after, cpu1_error_name(r.error), r.lock_restored);
    if (r.error != CPU1_OK)
        xil_printf("@CPU1 WARNING disable not confirmed; continuing normal application\r\n");
    return r.error==CPU1_OK?NULL:cpu1_error_name(r.error);
}
unsigned cpu1_power_is_disabled(void)
{
    return (Xil_In32(CPU1_RESET_CTRL) & CPU1_DISABLED_MASK) == CPU1_DISABLED_MASK;
}
