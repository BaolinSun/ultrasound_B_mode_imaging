#include "system_ctrl.h"
#include "sleep.h"

#define READ_BIT 16

/**************************���ջ���������******************************/
u8 *rx_buffer_ptr[BUFFER_NUM * BUFFER_PARTATIONS];
#if READ_BIT== 16
#define ONE_PACLAGE_NUM 2
u16 *read_rx_buffer_ptr[BUFFER_NUM * BUFFER_PARTATIONS];
#elif READ_BIT == 32
#define ONE_PACLAGE_NUM 4
u16 *read_rx_buffer_ptr[BUFFER_NUM];
#endif

//XGpio axiGpio0;
run_ctrl system_run_state;

void system_ctrl_init(void) {
//	XGpio_Config cfg;
	system_run_state.pulse_ctrl = SYSTEM_PULSE_GEN_ENABLE;
	system_run_state.usb_send_ctrl = SYSTEM_USB_SEND_DISABLE;
	mem_init();
//	XGpio_Initialize(&axiGpio0, XPAR_GPIO_0_DEVICE_ID);
//	XGpio_DiscreteWrite(&axiGpio0, 1, 0x01);
}

void mem_init(void) {
	// 初始化接收缓冲区
	for (int i = 0; i < BUFFER_NUM * BUFFER_PARTATIONS; i++) {
		rx_buffer_ptr[i] = (u8*) (MEM_BASE_ADDR + BUFFER_LENGTH_HEX * i);
#if READ_BIT== 16
		read_rx_buffer_ptr[i] = (u16 *) rx_buffer_ptr[i];
#elif READ_BIT == 32
		read_rx_buffer_ptr[i - 1] = (u32 *) rx_buffer_ptr[i - 1];
#endif

	}
}
void Pl_Reset_axiGpio(void) {
//	XGpio_DiscreteWrite(&axiGpio0, 1, 0x00);
//	usleep(1000);
//	XGpio_DiscreteWrite(&axiGpio0, 1, 0x01);
}

void Pl_Reset(void) {
	Xil_Out32(XSLCR_UNLOCK_ADDR, XSLCR_UNLOCK_CODE);
	Xil_Out32(XSLCR_FPGA_RST_CTRL_ADDR, 0x0F);
	usleep(1000);
	// and release the FPGA Reset Signal
	Xil_Out32(XSLCR_FPGA_RST_CTRL_ADDR, 0x00);
	Xil_Out32(XSLCR_LOCK_ADDR, XSLCR_LOCK_CODE);
	usleep(10000);
}

void cpu1_intr_init(void) {
	/***********************异常初始化******************************/
	Xil_ExceptionInit();
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler) XScuGic_InterruptHandler,
			&xInterruptController);
	Xil_ExceptionEnable()
	;
	/************************************************************/
	XScuGic_Connect(&xInterruptController, SOFT_INTR_ID_TO_CPU1,
			(Xil_InterruptHandler) soft_intr_handler,
			(void *) &xInterruptController);
	//指定优先级
	XScuGic_SetPriorityTriggerType(&xInterruptController, SOFT_INTR_ID_TO_CPU1,
	SOFT_INTR_PRIORITY << 3, 0x02);
	XScuGic_Enable(&xInterruptController, SOFT_INTR_ID_TO_CPU1);
}
// CPU1 接收CPU0的中断，指示开始接收数据
void soft_intr_handler(void *Callbackref) {
	static u32 info_tx7332_value = 0;

}
void get_run_time(XTime *before, XTime *after, char *str) {
	u64 elapsed_time = 0;
	elapsed_time = *after - *before;
	printf("%s run time :%.3f us\r\n", str,
			(float) elapsed_time * 2.0f
					* 1E6/XPAR_CPU_CORE_CLOCK_FREQ_HZ);
}

void delay_spi_nop(void) {
	for (int i = 0; i < 100; i++) {
		;
	}
}

void L2CacheSeperate(void) {
	Xil_Out32(L2Cpl310_BASE_ADDR + REG9_D_LOCKDOWN0_OFFSET,
	REG9_D_LOCKDOWN1_VALUE);
	Xil_Out32(L2Cpl310_BASE_ADDR + REG9_I_LOCKDOWN0_OFFSET,
	REG9_I_LOCKDOWN1_VALUE);
	Xil_Out32(L2Cpl310_BASE_ADDR + REG9_D_LOCKDOWN1_OFFSET,
	REG9_D_LOCKDOWN0_VALUE);
	Xil_Out32(L2Cpl310_BASE_ADDR + REG9_I_LOCKDOWN1_OFFSET,
	REG9_I_LOCKDOWN0_VALUE);
}

