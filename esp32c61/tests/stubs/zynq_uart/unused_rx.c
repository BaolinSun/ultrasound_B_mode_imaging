/* Link the legacy init/outbyte regression without exercising new RX functions. */
#include "xscugic.h"
#include "xiltimer.h"
#include <assert.h>
XScuGic xInterruptController;
u32 XUartPs_ReadReg(UINTPTR a,u32 o) { (void)a;(void)o;assert(0);return 0; }
void XUartPs_WriteReg(UINTPTR a,u32 o,u32 v) { (void)a;(void)o;(void)v;assert(0); }
int XUartPs_IsReceiveData(UINTPTR a) { (void)a;assert(0);return 0; }
int XUartPs_IsTransmitFull(UINTPTR a) { (void)a;assert(0);return 0; }
void XUartPs_SetFifoThreshold(XUartPs *u,u8 n) { (void)u;(void)n;assert(0); }
void XUartPs_SetRecvTimeout(XUartPs *u,u8 n) { (void)u;(void)n;assert(0); }
int XScuGic_Connect(XScuGic *g,u32 i,Xil_ExceptionHandler h,void *c) { (void)g;(void)i;(void)h;(void)c;assert(0);return 1; }
void XScuGic_Enable(XScuGic *g,u32 i) { (void)g;(void)i;assert(0); }
void XScuGic_InterruptMaptoCpu(XScuGic *g,u8 m,u32 i) { (void)g;(void)m;(void)i;assert(0); }
void XTime_GetTime(XTime *t) { (void)t;assert(0); }
void uart_handle_wifi_line(const char *l,u32 s,command_response_fn fn) { (void)l;(void)s;(void)fn;assert(0); }
