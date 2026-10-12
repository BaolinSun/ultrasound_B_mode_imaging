#include "uart/uart_log.h"
#include "xuartps_hw.h"

/* Link with -Wl,--wrap=outbyte. BSP may keep its strong outbyte definition;
 * external xil_printf()/printf() character calls resolve to this wrapper.
 * UART0 MIO27/TX -> ESP32 GPIO10/RX; no return/control channel. */
void __wrap_outbyte(char c)
{
    XUartPs_SendByte(ZYNQ_LOG_UART_BASEADDR,(u8)c);
}
