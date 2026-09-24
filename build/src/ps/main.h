#ifndef __MAIN_H
#define __MAIN_H

/* Xilinx includes. */
#include "xil_printf.h"
#include "xparameters.h"
#include "xgpiops.h"
#include "uart/uart.h"
#include "sleep.h"
#include "xscugic.h"
#include "xiltimer.h"
#include "stdio.h"
#include "system_ctrl/system_ctrl.h"


extern XScuGic xInterruptController;

/* No PS-to-PL control peripheral is mapped at the legacy 0x43C10000 address. */

#define USE_KEY_TASK 1
#define LED_TASK_PRIO 2
#define UART_TASK_PRIO 4
#define KEY_TASK_PRIORITY 4
#define TX7332_TASK_PRIO 5
#define TX7332_REG_TASK_PRIO 4
#define AXIDMA_TASK_PRIO 6
#define AFE5832_TASK_PRIO 2
#define USB_TASK_PRIO 7

#define LED_1_EMIO_PIN 55
#define LED_2_EMIO_PIN 54

#define L2Cpl310_BASE_ADDR 0xF8F02000U
#define REG9_D_LOCKDOWN0_OFFSET 0x00000900U
#define REG9_I_LOCKDOWN0_OFFSET 0x00000904U
#define REG9_D_LOCKDOWN1_OFFSET 0x00000908U
#define REG9_I_LOCKDOWN1_OFFSET 0x0000090CU

#define REG9_D_LOCKDOWN0_VALUE 0x0000000F
#define REG9_I_LOCKDOWN0_VALUE 0x0000000F
#define REG9_D_LOCKDOWN1_VALUE 0x000000F0
#define REG9_I_LOCKDOWN1_VALUE 0x000000F0
#define CPU1_ID XSCUGIC_SPI_CPU1_MASK
#define CPU0_ID XSCUGIC_SPI_CPU0_MASK
#define SOFT_INTR_PRIORITY 20
#define SOFT_INTR_ID_TO_CPU0 1
#define SOFT_INTR_ID_TO_CPU1 2

// assert FPGA Reset Signal
#define XSLCR_BASEADDR					0xF8000000U
#define XSLCR_LOCK_ADDR				    (XSLCR_BASEADDR + 0x00000004U)
#define XSLCR_UNLOCK_ADDR				(XSLCR_BASEADDR + 0x00000008U)
#define XSLCR_FPGA_RST_CTRL_ADDR        (XSLCR_BASEADDR + 0x00000240U)

/**< SLCR unlock code */
#define XSLCR_LOCK_CODE		    0x0000767BU
#define XSLCR_UNLOCK_CODE		0x0000DF0DU

//#define USE_REG_DEBUG
#define SAMPLE_LENGTH 2048
#define PLANE_WAVE_ANGLE_NUMS 11

//#define USE_DEBUG_PRINT

#ifdef USE_DEBUG_PRINT
#define DEBUG_PRINT(fmt, ...)  xil_printf("[DEBUG] "fmt, ##__VA_ARGS__ )
#define USB_DEBUG_PRINT(fmt,...) \
	do { char str_buffer[128] ;\
			snprintf(str_buffer,sizeof(str_buffer),fmt,##__VA_ARGS__);\
			usb_printf(str_buffer,strlen(str_buffer)); }while (0)
#else
#define DEBUG_PRINT(fmt,...) do {} while(0)
#define USB_DEBUG_PRINT(fmt,...) do {} while(0)
#endif

#endif
