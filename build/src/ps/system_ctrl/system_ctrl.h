#ifndef __SYSTEM_CTRL_H
#define __SYSTEM_CTRL_H

#include "xparameters.h"
#include "stdlib.h"
#include "xil_types.h"

#include "../main.h"
#include "../imaging_config.h"
#define SYSTEM_PULSE_GEN_ENABLE 0x01
#define SYSTEM_PULSE_GEN_DISABLE 0x02
#define SYSTEM_USB_SEND_ENABLE 0x01
#define SYSTEM_USB_SEND_DISABLE 0X02

// DDR ring window used by PL beamformed-data writer and USB readback.
#define PKG_LENGTH DDR_RING_BYTES
#define BUFFER_LENGTH_HEX DDR_RING_BYTES
/****************************************************************************/



#define BUFFER_PARTATIONS 1
/******************************�洢�ռ�***********************************/
#define DDR_BASE_ADDR XPAR_PS7_DDR_0_BASEADDRESS
#define BUFFER_NUM 1 // current USB path reads one PL DDR ring window
#define MEM_BASE_ADDR DDR_RING_BASE_ADDR		//fifoд���DDR��ʼ��ַ
/****************************************************************************/
extern u8 *rx_buffer_ptr[BUFFER_NUM*BUFFER_PARTATIONS];
extern u16 *read_rx_buffer_ptr[BUFFER_NUM*BUFFER_PARTATIONS];

typedef struct{
	u8 pulse_ctrl;
	u8 usb_send_ctrl;
} run_ctrl;

extern run_ctrl system_run_state;
void Pl_Reset_axiGpio(void);
void system_ctrl_init(void);
void mem_init(void);
void get_run_time(XTime *before, XTime *after,char *str);
void Pl_Reset(void);
void L2CacheSeperate(void);
void delay_spi_nop(void);
void cpu1_intr_init(void);
void soft_intr_handler(void *Callbackref);
#endif
