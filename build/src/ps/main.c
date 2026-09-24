/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
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
#include "CLOCK/system_clk.h"
#include "AFE5832/afe5832.h"
#include "TX7332/tx7332.h"
#include "key/key.h"
//#include "ps_2_pl_ctrl.h"
#include "usb/usb.h"
#include "xiltimer.h"
#include "data_transfer/data_recv.h"
#include "timer/timer.h"
#include "linux_wifi/linux_wifi.h"
#include "tmp/tmp.h"
#include "pus_operation/pus_oper.h"
#include "bram/bram.h"

XScuGic xInterruptController;
void led_init(void);
void intr_init(void);
void led_blink(u8 times,u32 time);


int main() {
	u32 pre_buffer_cnt = 50;
	u32 now_buffer_cnt = 0;
	u32 cnt = 0;
	XTime begin;
	XTime end;

	Xil_DCacheDisable();
	// �жϳ�ʼ��
	intr_init();
	sleep(1);
	DEBUG_PRINT("---------------cpu1 start----------\r\n");
	system_ctrl_init();
	gpio_init(); //ok
//	wifi_mem_init();
	uart_init(); //
	IIC_Init(); 	//OK
	hw_spi_init();
	hv_init();  //OK
	sys_clk_init();
	sleep(1);
//	Pl_Reset();
	data_recv_init();
//	key_init();	// ok
	pus_init();		// ����ϵͳ��ʼ��
	sleep(1);
	led_init();	// OK

	sleep(0.2);
	para_down();

	usb_init();
	xil_printf("@READY CONFIG\r\n");
	xil_printf("Beamform ready; STOP.\r\n");
	XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, 1);
	led_blink(5,250000);

	while (1) {
		uart_process_commands();
		usb_process_commands();
//		data_recv(MEM_BASE_ADDR,SAMPLE_LENGTH, 1,PLANE_WAVE_ANGLE_NUMS);
		usb_send_adc_data(rx_buffer_ptr[0], PLANE_WAVE_ANGLE_NUMS*2, 0);
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
