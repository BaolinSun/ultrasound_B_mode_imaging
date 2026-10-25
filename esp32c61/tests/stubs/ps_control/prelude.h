#ifndef PS_CONTROL_PRELUDE_H
#define PS_CONTROL_PRELUDE_H
#define __UART_H_
#define _AFE5832_H
#define _BRAM_H_
#define __HV_H
#include "xuartps.h"
#include "xil_printf.h"
#include <stdint.h>
typedef uint64_t u64;
typedef void (*command_response_fn)(const char *);
#define AFE5832_REG_MAP_NORMAL 0U
#define AFE5832_REG_MAP_GLOBAL 0U
#define AFE5832_REG_MAP_ADC 1U
#define AFE5832_REG_MAP_VCA 2U
#define AFE5832_REG_MAP_DTGC 3U
#define BRAM_CFG_STATUS_OK 0U
#define BRAM_CFG_STATUS_UNKNOWN 1U
#define BRAM_CFG_STATUS_RANGE 2U
#define BRAM_CFG_STATUS_ARGS 3U
#define BRAM_CFG_STATUS_BUSY 4U
#define BRAM_BEAMFORM_CTRL_ADDR 0x4001152cU
#define DEBUG_PRINT(...) ((void)0)
#define XST_INVALID_PARAM 2
u32 XBram_ReadReg(u32,u32);
void Beamform_Stop(void);
int Beamform_Start(void);
void Beamform_Pause(void);
int Beamform_Resume(void);
void Beamform_Halt(void);
u32 Beamform_GetRunRequest(void);
u32 Beamform_GetSpiStartRequest(void);
u32 Beamform_GetDownsample(void);
void hv_enable(void);
void hv_disable(void);
u32 hv_is_enabled(void);
u32 Rx_SetGainShift(u32);
u32 AFE5832_SetRegMap(u32);
u32 AFE5832_WriteRegMap(u32,u32,u32);
u32 AFE5832_ReadRegMap(u32,u32,u32*);
u32 Bram_WriteSectionWords(const char*,u32,const u32*,u32);
u32 Tx7332_WriteSectionWords(const char*,u32,const u32*,u32);
u32 Tx7332_ReadRegister(u32,u32*);
void uart_intrhandler(void*);
u32 setup_uart_intr_system(void);
void uart_process_commands(void);
int uart_idle_pending(void);
extern XUartPs uartps;
void uart_handle_usb_line(const char*,command_response_fn);
void uart_handle_wifi_line(const char*,u32,command_response_fn);
#endif
