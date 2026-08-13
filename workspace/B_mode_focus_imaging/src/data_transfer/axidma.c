#ifdef USE_DMA
#include "../data_transfer/axidma.h"

#include "../data_process/compress.h"
#include "xscugic.h"
#include "sleep.h"
#include "queue.h"
#include "../main.h"
#include "xil_cache.h"
#include "ps_2_pl_ctrl.h"
#include "../TX7332/tx7332.h"
#include "../usb/usb.h"

//AXI DMA 实测速度772MB/s

//AXIDMA实例
static XAxiDma axidma;
extern XScuGic xInterruptController;
#define 	XAxiDma_mIntrGetIrq(InstancePtr, Direction)	\
		(XAxiDma_ReadReg((InstancePtr)->RegBase + \
		(XAXIDMA_RX_OFFSET * Direction), XAXIDMA_SR_OFFSET))
// 使用缓冲区指示
volatile int buff_addr_cnt = 0;
volatile int rx_done;	//接收完成
volatile int error;		//接收错误
volatile u32 recv_cnt = 0;
volatile float speed;
volatile u8 start_flag;
volatile u8 send_finish = 0;

XTime xtime_before;
XTime xtime_after;
//
volatile u8 task_start = 0;

TaskHandle_t axidma_task_handler;
/*************************共享空间地址********************************/
typedef struct {
	u32 num;
} ShareMem;
// 共享空间地址
ShareMem *share_ptr = (ShareMem *) SHARE_MEM_ADDR;
/*********************************************************************/

#define READ_BIT 16

/**************************接收缓冲区定义******************************/
u8 *rx_buffer_ptr[BUFFER_NUM * BUFFER_PARTATIONS];
#if READ_BIT== 16
#define ONE_PACLAGE_NUM 2
u16 *read_rx_buffer_ptr[BUFFER_NUM * BUFFER_PARTATIONS];
#elif READ_BIT == 32
#define ONE_PACLAGE_NUM 4
u16 *read_rx_buffer_ptr[BUFFER_NUM];
#endif

/*********************************************************************/

void DMA_transfer(u8 **data_ptr, u8 buffer_index, u32 transfer_length) {
	while (send_finish == 0)
		;
	send_finish = 0;
	u32 status;
	status = XAxiDma_SimpleTransfer(&axidma, (UINTPTR) (data_ptr[buffer_index]),
			transfer_length * 64, XAXIDMA_DEVICE_TO_DMA);
	DEBUG_PRINT("DMA status %d\r\n", status);
	// 等待DMA完成

	while (rx_done == 0)
		;
	rx_done = 0;
}

void AXIDMA_Task(void *pvParameters) {
	u32 value = 0;
	u32 buf_cnt = 0;
	u32 info_tx7332_value = 0;
	u32 status = 0;
	u32 compress_data_length = 0;
	while (1) {

//		stack_used = uxTaskGetStackHighWaterMark(NULL);
//		DEBUG_PRINT("AXIDMA task Stack used: %d\r\n", stack_used);
		// 等待通知值
		value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		DEBUG_PRINT("AXIDMA : Task Recv TaskNotify value :%x \r\n", value);

		switch (value) {
		case 0x01: {
			xQueueReceive(shared_buffer_queue, &buf_cnt, portMAX_DELAY);

			DEBUG_PRINT("AXIDMA : share buffer  %d \r\n", buf_cnt);
			// 接收完成，启动dma
			if (buf_cnt == 0) {
				XTime_GetTime(&xtime_before);
				status = XAxiDma_SimpleTransfer(&axidma,
						(UINTPTR) (rx_buffer_ptr[0]),
						PKG_LENGTH, XAXIDMA_DEVICE_TO_DMA);
				// 等待DMA完成
				while (rx_done == 0)
					;
				rx_done = 0;
				XTime_GetTime(&xtime_after);
				get_run_time(&xtime_before, &xtime_after);
				DEBUG_PRINT("AXIDMA : 0 rx done\r\n");
			}
		}
			break;
		case 0x00: {
			// PL系统复位
			Pl_Reset();

			xQueueReset(shared_buffer_queue);
		}
			break;
		default: {

			DEBUG_PRINT("AXIDMA : error code \r\n");

		}
			break;
		}
		recv_cnt = 0;
		// 通知USB任务开始传输
//		XScuGic_SoftwareIntr(&xInterruptController, SOFT_INTR_ID_TO_CPU0,
//		XSCUGIC_SPI_CPU0_MASK);
//		compress_data_length = fast_data_bits_compress(read_rx_buffer_ptr[0],
//				4096 * 64 / 2);
//
//		DEBUG_PRINT("compress to %d Bytes\r\n", compress_data_length);
		switch (system_run_state.usb_send_ctrl) {
		case SYSTEM_USB_SEND_ENABLE: {
			xTaskNotify(usb_taskhandle, 0x01, eSetValueWithOverwrite);
		}
			break;
		case SYSTEM_USB_SEND_DISABLE: {
			xQueueSend(TX7332_queue, &info_tx7332_value, portMAX_DELAY);
		}
			break;
		default: {

		}
			break;
		}
	}
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

u32 AXIDMA_Init(void) {
	u32 status = 0;

	XAxiDma_Config *cfg;
	mem_init();
	cfg = XAxiDma_LookupConfig(AXIDMA_DEV_ID);
	if (cfg == NULL) {
		return XST_FAILURE;
	}
	status = XAxiDma_CfgInitialize(&axidma, cfg);
	if (status != XST_SUCCESS) {

		DEBUG_PRINT("cfg fail!\r\n");

		return XST_FAILURE;
	}

	DEBUG_PRINT("cfg success!\r\n");

	if (XAxiDma_HasSg(&axidma)) {
		return XST_FAILURE;
	}
	//建立中断系统
	status = AXIDMA_SetUpIntrSystem();
	if (status != XST_SUCCESS) {

		DEBUG_PRINT("intr setup fail!!!!!\r\n");

		return status;
	}

	DEBUG_PRINT("intr setup success!\r\n");

//	// 创建
//	xTaskCreate(AXIDMA_Task, "axidma task", 4096, NULL, AXIDMA_TASK_PRIO,
//			&axidma_task_handler);

	return XST_SUCCESS;
}

u32 AXIDMA_SetUpIntrSystem(void) {
	u32 status = 0;
	Xil_ExceptionInit();
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler) XScuGic_InterruptHandler, (void *) &axidma);
	Xil_ExceptionEnable()
	;
	XScuGic_InterruptUnmapFromCpu(&xInterruptController, XSCUGIC_SPI_CPU0_MASK,
			TX_INTR_ID);
	XScuGic_InterruptMaptoCpu(&xInterruptController, XSCUGIC_SPI_CPU1_MASK,
	TX_INTR_ID);
	//设置优先级和触发类型
	//TX : FIFO -> DDR
	//设置优先级
	XScuGic_SetPriorityTriggerType(&xInterruptController, TX_INTR_ID, 0xA0,
			0x03);

	status = XScuGic_Connect(&xInterruptController, TX_INTR_ID,
			(Xil_InterruptHandler) AXIDMA_TX_IntrHandler, (void *) &axidma);
	if (status != XST_SUCCESS) {
		return status;
	}
	XScuGic_Enable(&xInterruptController, TX_INTR_ID);
	// 将	AXIDMA中断绑定到CPU1
	XScuGic_InterruptMaptoCpu(&xInterruptController, XSCUGIC_SPI_CPU1_MASK,
	TX_INTR_ID);
	//先关闭中断
	XAxiDma_IntrDisable(&axidma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	//再使能 DMA 中断
	XAxiDma_IntrEnable(&axidma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);

	XScuGic_InterruptUnmapFromCpu(&xInterruptController, XSCUGIC_SPI_CPU0_MASK,
			MAXI_SEND_FINISH_INTR);
	XScuGic_InterruptMaptoCpu(&xInterruptController, XSCUGIC_SPI_CPU1_MASK,
	MAXI_SEND_FINISH_INTR);
	// 设定发送完成中断触发类型与优先级
	XScuGic_SetPriorityTriggerType(&xInterruptController,
	MAXI_SEND_FINISH_INTR, 0xA0, 0x03);
	// 绑定处理函数
	status = XScuGic_Connect(&xInterruptController, MAXI_SEND_FINISH_INTR,
			(Xil_InterruptHandler) Maxi_send_finish_intrHandler, NULL);
	if (status != XST_SUCCESS) {
		return status;
	}
	//使能中断
	XScuGic_Enable(&xInterruptController, MAXI_SEND_FINISH_INTR);
	// 将	中断绑定到CPU1

	return XST_SUCCESS;
}

void Maxi_send_finish_intrHandler(void *callback) {

	DEBUG_PRINT("send finish \r\n");

//	static BaseType_t HighTaskWoken = pdFALSE;
	// 接收到中断后关闭，等待下次中断
	PS_2_PL_CTRL_mWriteReg(PS_2_PL_CTRL_BASEADDR,
			PS_2_PL_CTRL_S00_AXI_SLV_REG2_OFFSET, 0x00000000);
	// 通知
	// 改为标志位
	send_finish = 1;
//	xTaskNotifyFromISR(axidma_task_handler, 0x01, eSetValueWithOverwrite,
//			&HighTaskWoken);
}

u32 AXIDMA_TX_IntrHandler(void* callBack) {
	u32 irq_status;
	int timeout;
//	u32 error;
	XAxiDma *axidma_inst = (XAxiDma *) callBack;
	// 获取中断类型
	irq_status = XAxiDma_mIntrGetIrq(axidma_inst, XAXIDMA_DEVICE_TO_DMA);
	DEBUG_PRINT("irq_satus : %x \r\n", irq_status);
	irq_status = XAxiDma_IntrGetIrq(axidma_inst, XAXIDMA_DEVICE_TO_DMA);
	DEBUG_PRINT("irq_satus : %x \r\n", irq_status);
	// 获取中断类型
	XAxiDma_IntrAckIrq(axidma_inst, irq_status, XAXIDMA_DEVICE_TO_DMA);
	//
	if (irq_status & XAXIDMA_IRQ_ERROR_MASK) {
		error = 1;

		DEBUG_PRINT("AXIDMA_TX_IntrHandler : ----DMA INTR ERROR-------\r\n");

		// 复位
		XAxiDma_Reset(axidma_inst);
		timeout = RESET_TIMEOUT_COUNTER;
		while (timeout) {
			// 查询是否复位成功
			if (XAxiDma_ResetIsDone(axidma_inst)) {
				break;
			}
			timeout -= 1;
		}
		return XST_SUCCESS;
	}
	// 正常中断
	if (irq_status & XAXIDMA_IRQ_IOC_MASK) {
		rx_done = 1;
		recv_cnt++;
	}
	return XST_SUCCESS;
}
void AXIDMA_set_sample_length(u32 sample_length) {
	PS_2_PL_CTRL_mWriteReg(PS_2_PL_CTRL_BASEADDR,
			PS_2_PL_CTRL_S00_AXI_SLV_REG4_OFFSET, sample_length & 0xFFFF);
}
#endif
