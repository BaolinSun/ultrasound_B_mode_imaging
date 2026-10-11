#include "uart/uart_log.h"
#include "xuartps_hw.h"

/* Overrides BSP character output for both xil_printf() and printf().
 * UART0 MIO27/TX -> ESP32 GPIO10/RX; no return/control channel. */
void outbyte(char c)
{
    XUartPs_SendByte(ZYNQ_LOG_UART_BASEADDR,(u8)c);
}
