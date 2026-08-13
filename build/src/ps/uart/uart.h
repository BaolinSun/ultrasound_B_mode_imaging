#ifndef __UART_H_
#define __UART_H_

#include "xuartps.h"
#include "../main.h"
#include "xil_printf.h"

typedef struct {
	u32 chip_code; // 芯片编号
	u32 reg_length; // 寄存器组数量
	u32 reg[100];	// 寄存器组
	u32 data[100]; // 寄存器数据
} reginfo;

extern u8 pulse_ctrl;
extern XUartPs uartps;

typedef void (*command_response_fn)(const char *text);

u32 uart_init(void);
void uart_process_commands(void);
void uart_handle_usb_line(const char *line, command_response_fn response_fn);
int uart_wait_config_done(void);
int uart_wait_afe_config_done(void);

void uart_intrhandler(void *callbackref);
u32 setup_uart_intr_system(void);

#endif

