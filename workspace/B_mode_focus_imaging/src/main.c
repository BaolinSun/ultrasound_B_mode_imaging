/*
 * helloworld.c: simple test application
 *
 * Application logs use PS UART0 MIO27 at 115200 (receive-only ESP bridge).
 * Legacy parameter commands retain PS UART1 MIO16/17 at 460800.
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */

#include <stdio.h>
#include <xscugic.h>
#include "xil_printf.h"
#include "xil_cache.h"
#include "main.h"
#include "HV/hv.h"
#include "iic/iic.h"
#include "GPIO/gpio.h"
#include "AFE5832/afe5832.h"
//#include "ps_2_pl_ctrl.h"
#include "usb/usb.h"
#include "xiltimer.h"
#include "tmp/tmp.h"
#include "pus_operation/pus_oper.h"
#include "bram/bram.h"
#include "data_transfer/sdio_bridge.h"
#include "uart/uart_log.h"

XScuGic xInterruptController;
void led_init(void);
void intr_init(void);
void led_blink(u8 times,u32 time);


int main() {
	u32 cnt = 0;

	if (zynq_uart_log_init() != XST_SUCCESS) return XST_FAILURE;
	xil_printf("@UART_LOG UART0 MIO27/TX -> ESP32 GPIO10/RX 115200 8N1\r\n");
	Xil_DCacheDisable();
	// �жϳ�ʼ��
	intr_init();
	sleep(1);
	DEBUG_PRINT("---------------cpu1 start----------\r\n");
	system_ctrl_init();
	gpio_init(); //ok
	uart_init(); //
	IIC_Init(); 	//OK
	hv_init();  //OK
	/* Board clocks are hardware-generated; no software clock-chip setup. */
	sleep(1);
//	Pl_Reset();
	pus_init();		// ����ϵͳ��ʼ��
	sleep(1);
	led_init();	// OK

	sleep(0.2);
	para_down();

	usb_init();
	sdio_bridge_init();
	xil_printf("@READY CONFIG\r\n");
	xil_printf("Beamform ready; STOP.\r\n");
	XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, 1);
	led_blink(5,250000);

	while (1) {
		uart_process_commands();
		usb_process_commands();
#if DATA_OUTPUT_MODE == DATA_OUTPUT_SDIO
		sdio_bridge_poll();
#else
		usb_send_adc_data(rx_buffer_ptr[0], PLANE_WAVE_ANGLE_NUMS*2, 0);
#endif
		cnt ++;
		XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, cnt&0x01);
//		usleep(250000);
	}


	return 0;
}

void led_init(void) {
	XGpioPs_SetDirectionPin(&gpiops, LED_2_EMIO_PIN, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, LED_2_EMIO_PIN, 1);
	XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, 1);
}

void led_blink(u8 times,u32 time)
{
	for (int i=0;i<times;i++)
	{
		XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, 1);
		usleep(time);
		XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, 0);
		usleep(time);
	}
}

// 初始化 通用中断控制器（GIC）
void intr_init(void) {
	XScuGic_Config *pxGICConfig;
	Xil_ExceptionInit();
	pxGICConfig = XScuGic_LookupConfig(XPAR_XSCUGIC_0_BASEADDR);
	XScuGic_CfgInitialize(&xInterruptController, pxGICConfig,
			pxGICConfig->CpuBaseAddress);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler) XScuGic_InterruptHandler,
			&xInterruptController);
	Xil_ExceptionEnable()
	;
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
}
