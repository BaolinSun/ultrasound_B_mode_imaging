/*
 * dmaData.h
 *
 *  Created on: 2024��5��13��
 *      Author: sdulh
 */

#ifndef SRC_DMADATA_DMADATA_H_
#define SRC_DMADATA_DMADATA_H_

#ifdef USE_DMA

#error "USE_DMA is unsupported: this hardware design has no AXI DMA or PL DMA interrupt"

#include "xaxidma.h"
#include "xdebug.h"
#include "xparameters.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "../main.h"
#include "../imaging_config.h"

/****************************AXIDMA����*****************************************/

#define AXIDMA_DEV_ID XPAR_AXIDMA_0_DEVICE_ID
// PL-PS中断ID
#define TX_INTR_ID XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR

#define MAXI_SEND_FINISH_INTR XPAR_FABRIC_RECV_DATA_WR_AXIFIFO_0_RECV_FINISH_INTR_INTR
// xsugic控制器设备ID
#define INTC_DEVICE_ID XPAR_SCUGIC_SINGLE_DEVICE_ID
// Raw RF frame bytes: 32 channels * 16-bit = 64 bytes per sample point.
#define PKG_LENGTH RAW_FRAME_BYTES
#define BUFFER_LENGTH_HEX RAW_FRAME_BYTES
/****************************************************************************/



#define BUFFER_PARTATIONS 2
/******************************存储空间***********************************/
#define DDR_BASE_ADDR XPAR_PS7_DDR_0_S_AXI_BASEADDR   //0x00100000
#define BUFFER_NUM 50 //一次读完整个FIFO，所以只需要两个buff
#define MEM_BASE_ADDR 0x18000000		//fifo写入的DDR起始地址
#define SHARE_MEM_ADDR MEM_BASE_ADDR	//
/****************************************************************************/
#define RESET_TIMEOUT_COUNTER 10000 //

#define CPU1_START_ADDR 0x12000000

extern volatile u8 start_flag;
extern TaskHandle_t axidma_task_handler;
extern volatile u8 task_start;
u8 *rx_buffer_ptr[BUFFER_NUM*BUFFER_PARTATIONS];
extern u16 *read_rx_buffer_ptr[BUFFER_NUM*BUFFER_PARTATIONS];
/***************************AXIDMA***************************/
void AXIDMA_Task(void *pvParameters);
u32 AXIDMA_Init(void);
/*****************************AXIDMA***********************/
u32 AXIDMA_SetUpIntrSystem(void);
u32 AXIDMA_TX_IntrHandler(void* callBack);

void AXIDMA_set_sample_length(u32 sample_length);
void Maxi_send_finish_intrHandler(void *callback);

TaskHandle_t memRead_task_handler;
// 共享数据空间初始化函数
void mem_init(void);
void DMA_transfer(u8 **data_ptr, u8 buffer_index, u32 transfer_length);
void memRead_Task(void *pvParameters);

#endif

#endif /* SRC_DMADATA_DMADATA_H_ */
