#include "uart_log.h"
#include "xuartps.h"
#include <assert.h>
#include <stdio.h>
static XUartPs_Config uart0={0xe0000000U,0x403bU},uart1={0xe0001000U,0x4032U};
static int fail,call,sends;
XUartPs_Config *XUartPs_LookupConfig(UINTPTR base) {
    assert(base==uart0.BaseAddress);
    if(fail==4)return NULL;
    return fail==5?&uart1:&uart0;
}
int XUartPs_CfgInitialize(XUartPs *u,XUartPs_Config *c,UINTPTR base) {
    assert(c==&uart0 && base==uart0.BaseAddress);u->Config=*c;return ++call==fail?1:0;
}
int XUartPs_SetBaudRate(XUartPs *u,u32 baud) {
    assert(u->Config.BaseAddress==uart0.BaseAddress && baud==115200);return ++call==fail?1:0;
}
int XUartPs_SetDataFormat(XUartPs *u,XUartPsFormat *f) {
    assert(u->Config.BaseAddress==uart0.BaseAddress && f->BaudRate==115200 &&
           f->DataBits==XUARTPS_FORMAT_8_BITS && f->Parity==XUARTPS_FORMAT_NO_PARITY &&
           f->StopBits==XUARTPS_FORMAT_1_STOP_BIT);return ++call==fail?1:0;
}
void XUartPs_SetOperMode(XUartPs *u,u8 mode) { assert(u->Config.BaseAddress==uart0.BaseAddress && mode==0); }
void XUartPs_SetInterruptMask(XUartPs *u,u32 mask) { assert(u->Config.BaseAddress==uart0.BaseAddress && mask==0); }
void XUartPs_SendByte(UINTPTR base,u8 c) { assert(base==uart0.BaseAddress && c==(u8)'Z');sends++; }
void outbyte(char);
int main(void) {
    for(fail=1;fail<=5;fail++) {call=0;assert(zynq_uart_log_init()==XST_FAILURE);}
    fail=0;call=0;assert(zynq_uart_log_init()==XST_SUCCESS && call==3);
    outbyte('Z');assert(sends==1);
    puts("PASS: Zynq UART0 base/115200/8N1/no RX IRQ/init failures/outbyte routing");
    return 0;
}
