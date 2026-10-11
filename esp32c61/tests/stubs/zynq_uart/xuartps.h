#ifndef FAKE_ZYNQ_UART_H
#define FAKE_ZYNQ_UART_H
#include <stdint.h>
typedef uint8_t u8;
typedef unsigned u32;
typedef uintptr_t UINTPTR;
typedef struct {UINTPTR BaseAddress;u32 IntrId;} XUartPs_Config;
typedef struct {XUartPs_Config Config;} XUartPs;
typedef struct {u32 DataBits,ParityMode,StopBits;} XUartPsFormat;
#define XUARTPS_FORMAT_NO_PARITY 0
#define XUARTPS_FORMAT_1_STOP_BIT 1
#define XUARTPS_OPER_MODE_NORMAL 0
XUartPs_Config *XUartPs_LookupConfig(UINTPTR);
int XUartPs_CfgInitialize(XUartPs*,XUartPs_Config*,UINTPTR);
int XUartPs_SetBaudRate(XUartPs*,u32);
int XUartPs_SetDataFormat(XUartPs*,XUartPsFormat*);
void XUartPs_SetOperMode(XUartPs*,u8);
void XUartPs_SetInterruptMask(XUartPs*,u32);
void XUartPs_SendByte(UINTPTR,u8);
#endif
