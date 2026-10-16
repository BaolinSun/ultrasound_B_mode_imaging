#include "uart_log.h"
#include "xscugic.h"
#include "xiltimer.h"
#include "wifi_control_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
XScuGic xInterruptController;
static XUartPs_Config config={0xe0000000U,59U};
static uint64_t clock_ticks;
static unsigned hv,run,enables,writes,afe_fail,tx_full;
static char received[1024],transmitted[1024],answer[128];
static size_t receive_at,receive_len,transmit_len;
static void (*irq_handler)(void*);
void XTime_GetTime(XTime *t) { *t=clock_ticks++; }
XUartPs_Config *XUartPs_LookupConfig(UINTPTR base) { assert(base==config.BaseAddress); return &config; }
int XUartPs_CfgInitialize(XUartPs *u,XUartPs_Config *c,UINTPTR base) { assert(base==c->BaseAddress);u->Config=*c;return 0; }
int XUartPs_SetBaudRate(XUartPs *u,u32 rate) { (void)u;assert(rate==115200);return 0; }
int XUartPs_SetDataFormat(XUartPs *u,XUartPsFormat *f) { (void)u;assert(f->BaudRate==115200 && f->DataBits==0 && f->Parity==4 && f->StopBits==0);return 0; }
void XUartPs_SetOperMode(XUartPs *u,u8 m) { (void)u;(void)m; }
void XUartPs_SetInterruptMask(XUartPs *u,u32 m) { (void)u;(void)m; }
void XUartPs_SendByte(UINTPTR base,u8 c) { (void)base;(void)c; }
u32 XUartPs_ReadReg(UINTPTR base,u32 off) { (void)base; if(off==XUARTPS_FIFO_OFFSET)return (u8)received[receive_at++];return 0; }
void XUartPs_WriteReg(UINTPTR base,u32 off,u32 v) { (void)base; if(off==XUARTPS_FIFO_OFFSET) { assert(transmit_len<sizeof(transmitted)-1);transmitted[transmit_len++]=(char)v;transmitted[transmit_len]=0; } }
int XUartPs_IsReceiveData(UINTPTR base) { (void)base;return receive_at<receive_len; }
int XUartPs_IsTransmitFull(UINTPTR base) { (void)base;return tx_full; }
int XUartPs_SelfTest(XUartPs *u) { (void)u;return 0; }
void XUartPs_SetFifoThreshold(XUartPs *u,u8 n) { (void)u;(void)n; }
void XUartPs_SetRecvTimeout(XUartPs *u,u8 n) { (void)u;(void)n; }
u32 XUartPs_Recv(XUartPs *u,u8 *p,u32 n) { (void)u;(void)p;(void)n;return 0; }
int XScuGic_Connect(XScuGic *g,u32 id,Xil_ExceptionHandler fn,void *ctx) { (void)g;(void)id;(void)ctx;irq_handler=fn;return 0; }
void XScuGic_Enable(XScuGic *g,u32 id) { (void)g;(void)id; }
void XScuGic_InterruptMaptoCpu(XScuGic *g,u8 mask,u32 id) { (void)g;(void)id;assert(mask==1); }
u32 XBram_ReadReg(u32 base,u32 off) { (void)base;(void)off;return run; }
void hv_enable(void) { hv=1;enables++; }
void hv_disable(void) { hv=0; }
u32 hv_is_enabled(void) { return hv; }
void Beamform_Stop(void) { hv=run=0; }
void Beamform_Start(void) { hv_enable();run=1; }
void Beamform_Halt(void) { run=0; }
void Beamform_Pause(void) { run=0; }
void Beamform_Resume(void) { run=1; }
u32 Rx_SetGainShift(u32 n) { return n; }
u32 AFE5832_SetRegMap(u32 m) { (void)m;return afe_fail; }
u32 AFE5832_WriteRegMap(u32 m,u32 a,u32 d) { (void)m;(void)a;(void)d;writes++;return afe_fail; }
u32 AFE5832_ReadRegMap(u32 m,u32 a,u32 *d) { (void)m;(void)a;*d=123;return afe_fail; }
u32 Bram_WriteSectionWords(const char *s,u32 a,const u32 *w,u32 n) { if(!strcmp(s,"t_timing")&&a==0)assert(w[0]==6144);(void)n;writes++;return 0; }
u32 Tx7332_WriteSectionWords(const char *s,u32 a,const u32 *w,u32 n) { return Bram_WriteSectionWords(s,a,w,n); }
u32 Tx7332_ReadRegister(u32 a,u32 *d) { (void)a;*d=0x1234;return 0; }
static void capture(const char *s) { snprintf(answer,sizeof(answer),"%s",s); }
static const char *wifi(uint32_t request,const char *command) {
    int n=wc_encode(received,sizeof(received),false,12,request,command);assert(n>0);
    receive_at=0;receive_len=(size_t)n;transmit_len=0;transmitted[0]=0;
    irq_handler(NULL);uart_process_commands();
    wc_message m;assert(wc_decode(transmitted,true,&m));assert(m.session==12 && m.request==request);
    strcpy(answer,m.text);return answer;
}
int main(void) {
    assert(zynq_uart_log_init()==0);assert(zynq_uart_command_init()==0);
    assert(strstr(wifi(1,"STATUS"),"cfg=0"));
    assert(strstr(wifi(2,"CFG BEGIN 123"),"CFG_BEGIN"));assert(!hv&&!run);
    uart_handle_usb_line("CFG END",capture);assert(strstr(answer,"CFG_OWNER")&&!hv&&!run);
    assert(strstr(wifi(3,"BRAM WR t_timing 0 1 0x1800"),"BRAM_WR"));assert(writes==1);
    assert(strstr(wifi(4,"CFG END"),"CFG_END"));assert(!hv&&!run&&enables==0);
    assert(strstr(wifi(5,"HV ENABLE"),"HV_ENABLE"));assert(enables==1);
    assert(strstr(wifi(5,"HV ENABLE"),"HV_ENABLE"));assert(enables==1);
    assert(strstr(wifi(5,"HV DISABLE"),"REQUEST_CONFLICT"));assert(enables==1);
    assert(strstr(wifi(4,"HV ENABLE"),"STALE_REQUEST"));assert(enables==1);
    assert(strstr(wifi(6,"HV DISABLE"),"HV_DISABLE"));assert(!hv&&!run);
    assert(strstr(wifi(7,"CFG BEGIN 124"),"CFG_BEGIN"));
    assert(strstr(wifi(8,"BRAM WR t_timing 0 1 2048"),"FRAME_LAYOUT"));assert(writes==1&&!hv&&!run);
    assert(strstr(wifi(9,"STATUS"),"cfg=0"));
    assert(strstr(wifi(10,"CFG BEGIN 125"),"CFG_BEGIN"));
    clock_ticks+=60000001;uart_process_commands();assert(strstr(wifi(11,"STATUS"),"cfg=0"));
    assert(strstr(wifi(12,"CFG BEGIN 126"),"CFG_BEGIN"));
    afe_fail=1;assert(strstr(wifi(13,"AFE WR ADC 1 2"),"AFE_WR_FAIL"));afe_fail=0;
    assert(strstr(wifi(14,"STATUS"),"cfg=0"));assert(!hv&&!run);
    uart_handle_usb_line("CFG BEGIN 50",capture);assert(strstr(answer,"CFG_BEGIN"));
    uart_handle_usb_line("CFG END",capture);assert(strstr(answer,"CFG_END")&&hv&&run&&enables==2);
    assert(strstr(wifi(15,"HV DISABLE"),"HV_DISABLE"));
    /* CRC failure and overlong line cannot execute a suffix command. */
    strcpy(received,"@WFC1 0000000c 00000010 9 HV ENABLE*00000000\n");receive_at=0;receive_len=strlen(received);
    irq_handler(NULL);uart_process_commands();assert(!hv&&enables==2);
    memset(received,'x',300);strcpy(received+300,"HV ENABLE\n");receive_at=0;receive_len=strlen(received);
    irq_handler(NULL);uart_process_commands();assert(!hv);
    assert(strstr(wifi(16,"STATUS"),"cfg=0"));
    uart_handle_wifi_line("CFG BEGIN 20",12,capture);
    uart_handle_wifi_line("BRAM WR t_timing 0 1 06144",12,capture);assert(strstr(answer,"BRAM_WR"));
    uart_handle_wifi_line("CFG END",12,capture);assert(!hv&&!run);
    tx_full=1;(void)wc_encode(received,sizeof(received),false,12,17,"STATUS");
    receive_at=0;receive_len=strlen(received);transmit_len=0;irq_handler(NULL);uart_process_commands();assert(transmit_len==0);
    puts("PASS PS: framing/CRC/overflow/replay/owners/timeout/safe-END/legacy USB/AFE failure/bounded TX");
    return 0;
}
