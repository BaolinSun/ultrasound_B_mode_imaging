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
#include "AFE5832/afe_power.h"
#include "TX7332/tx_power.h"
//#include "ps_2_pl_ctrl.h"
#include "usb/usb.h"
#include "usb/usb_transport.h"
#include "xiltimer.h"
#include "system_ctrl/temp.h"
#include "system_ctrl/cpu1_power.h"
#include "bram/bram.h"
#include "data_transfer/sdio_bridge.h"
#include "data_transfer/frame_store.h"
#include "uart/uart_log.h"

XScuGic xInterruptController;
void led_init(void);
void intr_init(void);
void led_blink(u8 times,u32 time);


int main() {
	u32 cnt = 0;

	if (zynq_uart_log_init() != XST_SUCCESS) return XST_FAILURE;
	xil_printf("@UART_LOG UART0 MIO27/TX -> ESP32 GPIO10/RX 115200 8N1\r\n");
	cpu1_power_init(); /* Warning only on failure; must precede IRQ init. */
	Xil_DCacheDisable();

	intr_init();
	sleep(1);

	system_ctrl_init();
	gpio_init();
	uart_init();
	IIC_Init();
	hv_init();

	sleep(1);

	AFE5832_init();
	/* Keep the former wrapper's 1 s + main's 1 s settling delay. */
	sleep(2);
	led_init();

	sleep(0.2);
	para_down();

	/* A hot application download can inherit USB IN DMA using a DDR slot. */
	int frame_init_rc = -1;
	if (usb_transport_boot_quiesce() == 0) frame_init_rc = frame_store_init();
	else xil_printf("@USB FATAL inherited DMA not idle; DDR INIT blocked\r\n");
	const volatile frame_store_init_diag_t *fd=frame_store_init_diagnostic();
	xil_printf("@DDR INIT rc=%d stage=%u version=%08x seq=%u ack=%u result=%u flags=%08x\r\n",
	    frame_init_rc,(unsigned)fd->stage,(unsigned)fd->version,(unsigned)fd->sequence,
	    (unsigned)fd->ack,(unsigned)fd->result,(unsigned)fd->flags);
	xil_printf("@DDR SD1 clock_before=%08x present_before=%08x clock_after=%08x present_after=%08x reset=%02x elapsed_ms=%u cleanup=%u clock_ready=%08x\r\n",
	    (unsigned)fd->sd_clock_before,(unsigned)fd->sd_present_before,
	    (unsigned)fd->sd_clock_after,(unsigned)fd->sd_present_after,
	    (unsigned)fd->sd_reset_after,(unsigned)fd->dma_elapsed_ms,
	    (unsigned)fd->sd_cleanup_phase,(unsigned)fd->sd_clock_ready);
	if(frame_init_rc!=0) xil_printf("@DDR ERROR frame-store ABI/init; image upload disabled\r\n");
	if(Xil_In32(FS_BASE+FS_DIAG_VERSION)==FS_DIAG_MAGIC_VALUE) {
	    xil_printf("@DDR PL_DIAG axi=%08x seq=%u cmd=%u pending=%u parser=%08x\r\n",
	        (unsigned)Xil_In32(FS_BASE+FS_DIAG_AXI),(unsigned)Xil_In32(FS_BASE+FS_DIAG_SEQUENCE),
	        (unsigned)Xil_In32(FS_BASE+FS_DIAG_COMMAND),(unsigned)Xil_In32(FS_BASE+FS_DIAG_PENDING),
	        (unsigned)Xil_In32(FS_BASE+FS_DIAG_PARSER));
	    xil_printf("@DDR PL_SOURCE flags=%08x samples=%u age_cycles=%u\r\n",
	        (unsigned)Xil_In32(FS_BASE+FS_DIAG_INPUT),(unsigned)Xil_In32(FS_BASE+FS_DIAG_SAMPLES),
	        (unsigned)Xil_In32(FS_BASE+FS_DIAG_AGE));
	}
	usb_init();
	sdio_bridge_init();
	afe_power_init();
    tx_power_init();
    xil_printf("@TX state=%s flags=%08x error=%s\r\n",tx_power_state_name(),(unsigned)tx_power_flags(),tx_power_error());
	xil_printf("@AFE state=%s flags=%08x error=%s\r\n",afe_power_state_name(),(unsigned)afe_power_flags(),afe_power_error());
	temperature_service_init();
	xil_printf("@READY CONFIG\r\n");
	xil_printf("Beamform ready; STOP.\r\n");
	XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, 1);
	led_blink(5,250000);

	while (1) {
		temperature_service_poll();
		afe_power_poll();
        tx_power_poll();
		uart_process_commands();
		usb_process_commands();
#if DATA_OUTPUT_MODE == DATA_OUTPUT_SDIO
		sdio_bridge_poll();
#else
		usb_send_adc_data(NULL, 0, 0);
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
