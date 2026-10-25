#ifndef ZYNQ_UART_LOG_H
#define ZYNQ_UART_LOG_H
#include "xstatus.h"
#define ZYNQ_LOG_UART_BASEADDR 0xE0000000U
#define ZYNQ_LOG_UART_BAUD 115200U
int zynq_uart_log_init(void);
int zynq_uart_command_init(void);
void zynq_uart_command_poll(void);
int zynq_uart_idle_pending(void); /* caller masks IRQ; never changes IMR/GIC */
#endif
