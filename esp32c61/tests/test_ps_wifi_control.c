#include "uart_log.h"
#include "uart.h"
#include "xscugic.h"
#include "xiltimer.h"
#include "wifi_control_protocol.h"
#include "../../build/src/ps/data_transfer/frame_store.h"
#include <assert.h>
#include <stdio.h>
static int afe_ready=1,tx_ready=1;
static unsigned cpu1_disabled=1;
static unsigned cpu1_calls,report_requests;
static const char *cpu1_failure;
void cpu0_idle_report_request(void){report_requests++;}
const char *cpu1_power_disable(void){cpu1_calls++;if(cpu1_failure)return cpu1_failure;cpu1_disabled=1;return NULL;}
static unsigned idle_enabled=1;
unsigned cpu0_idle_enabled(void){return idle_enabled;}
unsigned cpu1_power_is_disabled(void){return cpu1_disabled;}
int tx_power_allowed(void){return tx_ready;}
int tx_power_idle(void){return 1;}
void tx_power_poll(void){}
const char *tx_power_state_name(void){return tx_ready?"ready":"sleep";}
const char *tx_power_error(void){return "TX_NOT_READY";}
u32 tx_power_flags(void){return tx_ready?9:15;}
int tx_power_command(const char *op,u32 owner){(void)owner;tx_ready=!strcmp(op,"WAKE");return 0;}
int afe_power_allowed(void) { return afe_ready; }
int afe_power_idle(void) { return 1; }
void afe_power_poll(void) { }
const char *afe_power_state_name(void) { return afe_ready?"ready":"sleep"; }
const char *afe_power_error(void) { return "AFE_NOT_READY"; }
u32 afe_power_flags(void) { return afe_ready?31:0; }
int afe_power_command(const char *op,u32 owner) { (void)owner;afe_ready=!strcmp(op,"WAKE");return 0; }
#include <string.h>
XScuGic xInterruptController;
static XUartPs_Config config={0xe0000000U,59U};
static uint64_t clock_ticks;
static unsigned hv,run,enables,writes,afe_fail,tx_full,resume_failure,downsample,pause_failure;
static unsigned usb_drain_failure, usb_config_blocked;
int usb_image_config_begin(void) { usb_config_blocked=!usb_drain_failure;return usb_drain_failure?-1:0; }
void usb_image_config_end(void) { usb_config_blocked=0; }
int frame_store_pause(void) { assert(!hv&&!run);return pause_failure?-1:0; }
const volatile frame_store_pause_diag_t *frame_store_pause_diagnostic(void) {
    static const frame_store_pause_diag_t d={.reason=FS_DRAIN_TIMEOUT,.version=FS_MAGIC,.flags=13,.axi=0x2e101};return &d;
}
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
static unsigned uart_sr=XUARTPS_SR_RXEMPTY|XUARTPS_SR_TXEMPTY;
static unsigned uart_imr=XUARTPS_IXR_RXOVR|XUARTPS_IXR_TOUT;
u32 XUartPs_ReadReg(UINTPTR base,u32 off) { (void)base; if(off==XUARTPS_FIFO_OFFSET)return (u8)received[receive_at++];if(off==XUARTPS_SR_OFFSET)return uart_sr;if(off==XUARTPS_IMR_OFFSET)return uart_imr;return 0; }
void XUartPs_WriteReg(UINTPTR base,u32 off,u32 v) { (void)base; if(off==XUARTPS_FIFO_OFFSET) { assert(transmit_len<sizeof(transmitted)-1);transmitted[transmit_len++]=(char)v;transmitted[transmit_len]=0; } }
int XUartPs_IsReceiveData(UINTPTR base) { (void)base;return receive_at<receive_len; }
int XUartPs_IsTransmitFull(UINTPTR base) { (void)base;return tx_full; }
int XUartPs_SelfTest(XUartPs *u) { (void)u;return 0; }
void XUartPs_SetFifoThreshold(XUartPs *u,u8 n) { (void)u;(void)n; }
void XUartPs_SetRecvTimeout(XUartPs *u,u8 n) { (void)u;(void)n; }
u32 XUartPs_Recv(XUartPs *u,u8 *p,u32 n) { (void)u;(void)p;(void)n;return 0; }
int XScuGic_Connect(XScuGic *g,u32 id,Xil_ExceptionHandler fn,void *ctx) { (void)g;(void)id;(void)ctx;irq_handler=fn;return 0; }
void XScuGic_Enable(XScuGic *g,u32 id) { (void)g;(void)id; }
void XScuGic_InterruptMaptoCpu(XScuGic *g,u8 mask,u32 id) { (void)g;(void)id;assert(mask==0); }
u32 XBram_ReadReg(u32 base,u32 off) { (void)base;(void)off;assert(0);return 0; } /* No valid run readback in PL. */
void hv_enable(void) { hv=1;enables++; }
void hv_disable(void) { hv=0; }
u32 hv_is_enabled(void) { return hv; }
void Beamform_Stop(void) { hv=run=0; }
int Beamform_Start(void) { if(resume_failure){hv=run=0;return -1;}hv_enable();run=1;return 0; }
void Beamform_Halt(void) { run=0; }
void Beamform_Pause(void) { run=0; }
int Beamform_Resume(void) { if(resume_failure){hv=run=0;return -1;}run=1;return 0; }
u32 Beamform_GetRunRequest(void) { return run; }
u32 Beamform_GetSpiStartRequest(void) { return run; }
u32 Beamform_GetDownsample(void) { return downsample; }
u32 Rx_SetGainShift(u32 n) { return n; }
u32 AFE5832_SetRegMap(u32 m) { (void)m;return afe_fail; }
u32 AFE5832_WriteRegMap(u32 m,u32 a,u32 d) { (void)m;(void)a;(void)d;writes++;return afe_fail; }
u32 AFE5832_ReadRegMap(u32 m,u32 a,u32 *d) { (void)m;(void)a;*d=123;return afe_fail; }
u32 Bram_WriteSectionWords(const char *s,u32 a,const u32 *w,u32 n) {
    if(!strcmp(s,"t_timing")) {
        assert(!hv&&!run);
        if(a==0)assert(w[0]==6144);
        if(a<=9&&a+n>9)downsample=w[9-a];
    }
    writes++;return 0;
}
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
    assert(strstr(answer,"cpu1_disabled=1"));
    assert(strstr(answer,"wfi=1"));
    idle_enabled=0;uart_handle_wifi_line("STATUS",12,capture);assert(strstr(answer,"wfi=0"));idle_enabled=1;
    assert(!zynq_uart_idle_pending());
    uart_sr=XUARTPS_SR_RXEMPTY;assert(zynq_uart_idle_pending());
    uart_sr=XUARTPS_SR_TXEMPTY;assert(zynq_uart_idle_pending());
    uart_sr=XUARTPS_SR_RXEMPTY|XUARTPS_SR_TXEMPTY;
    uart_imr=XUARTPS_IXR_RXOVR;assert(zynq_uart_idle_pending()); /* Short packets need timeout IRQ too. */
    uart_imr=XUARTPS_IXR_TOUT;assert(zynq_uart_idle_pending());
    uart_imr=XUARTPS_IXR_RXOVR|XUARTPS_IXR_TOUT;assert(!zynq_uart_idle_pending());
    assert(uart_idle_pending()); /* UART1 must have been initialized. */
    uartps.Config.BaseAddress=0xe0001000U;assert(!uart_idle_pending());
    uart_sr=0;assert(uart_idle_pending());uart_sr=XUARTPS_SR_RXEMPTY|XUARTPS_SR_TXEMPTY;
    uart_imr=XUARTPS_IXR_RXOVR;assert(uart_idle_pending());
    uart_imr=XUARTPS_IXR_RXOVR|XUARTPS_IXR_TOUT;assert(!uart_idle_pending());
    cpu1_disabled=0;uart_handle_wifi_line("STATUS",12,capture);assert(strstr(answer,"cpu1_disabled=0"));
    cpu1_disabled=1;
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
    uart_handle_wifi_line("BEAM START",12,capture);assert(strstr(answer,"BEAM_START")&&run&&!hv);
    uart_handle_wifi_line("STATUS",12,capture);assert(strstr(answer,"run=1 hv=0 run_src=ps spi=1"));
    uart_handle_wifi_line("BEAM STOP",12,capture);assert(!run&&!hv);

    int split=wc_encode(received,sizeof received,false,12,1000,"STATUS");assert(split>1);
    receive_at=0;receive_len=1;transmit_len=0;transmitted[0]=0;
    irq_handler(NULL);assert(!zynq_uart_idle_pending()); /* Partial line waits for next UART IRQ. */
    receive_len=(size_t)split;irq_handler(NULL);assert(zynq_uart_idle_pending());
    uart_process_commands();assert(!zynq_uart_idle_pending()&&!hv&&!run);
    uart_handle_wifi_line("STATUS",12,capture);assert(strstr(answer,"run=0 hv=0 run_src=ps"));
    uart_handle_wifi_line("CFG BEGIN 20",12,capture);
    uart_handle_wifi_line("BRAM WR t_timing 0 1 06144",12,capture);assert(strstr(answer,"BRAM_WR"));
    uart_handle_wifi_line("CFG END",12,capture);assert(!hv&&!run);
    resume_failure=1;
    uart_handle_wifi_line("BEAM START",12,capture);
    assert(strstr(answer,"@NACK") && strstr(answer,"DDR_RESUME_OR_ABI") && !hv && !run);
    uart_handle_usb_line("CFG BEGIN 21",capture);assert(strstr(answer,"CFG_BEGIN"));
    uart_handle_usb_line("CFG END",capture);
    assert(strstr(answer,"@NACK") && strstr(answer,"DDR_RESUME_OR_ABI") && !hv && !run);
    resume_failure=0;
    tx_full=1;(void)wc_encode(received,sizeof(received),false,12,17,"STATUS");
    receive_at=0;receive_len=strlen(received);transmit_len=0;irq_handler(NULL);uart_process_commands();assert(transmit_len==0);
    tx_full=0;
    for(unsigned d=0;d<=4;d++) {
        char command[64],expected[24];hv=run=1;
        uart_handle_wifi_line("CFG BEGIN 30",12,capture);assert(strstr(answer,"CFG_BEGIN")&&!hv&&!run);
        snprintf(command,sizeof(command),"BRAM WR t_timing 9 1 %u",d);
        unsigned previous=writes;
        uart_handle_wifi_line(command,13,capture);assert(strstr(answer,"CFG_OWNER")&&writes==previous);
        uart_handle_wifi_line(command,12,capture);assert(strstr(answer,"BRAM_WR")&&writes==previous+1);
        uart_handle_wifi_line("CFG END",12,capture);assert(strstr(answer,"CFG_END")&&!hv&&!run);
        uart_handle_wifi_line("STATUS",12,capture);
        snprintf(expected,sizeof(expected),"down=%u",d);assert(strstr(answer,expected)&&strstr(answer,"cfg=0")&&strstr(answer,"run=0 hv=0"));
    }
    unsigned previous=writes;
    pause_failure=1;hv=run=1;
    uart_handle_wifi_line("CFG BEGIN 31",12,capture);assert(strstr(answer,"DDR_DRAIN_BUSY")&&!hv&&!run);
    uart_handle_wifi_line("BRAM WR t_timing 9 1 1",12,capture);assert(strstr(answer,"NO_CFG")&&writes==previous&&downsample==4);
    pause_failure=0;
    uart_handle_wifi_line("CFG BEGIN 32",12,capture);
    uart_handle_wifi_line("BRAM WR t_timing 9 1 5",12,capture);assert(strstr(answer,"FRAME_LAYOUT")&&writes==previous&&downsample==4);
    uart_handle_wifi_line("STATUS",12,capture);assert(strstr(answer,"cfg=0")&&strstr(answer,"down=4"));
    assert(!usb_config_blocked);
    usb_drain_failure=1;hv=run=1;previous=writes;
    uart_handle_wifi_line("CFG BEGIN 33",12,capture);
    assert(strstr(answer,"USB_DRAIN")&&!hv&&!run&&!usb_config_blocked);
    uart_handle_wifi_line("BRAM WR t_timing 9 1 0",12,capture);
    assert(strstr(answer,"NO_CFG")&&writes==previous);
    usb_drain_failure=0;
    afe_ready=0;
    uart_handle_usb_line("CFG BEGIN 901",capture);assert(strstr(answer,"AFE_NOT_READY"));
    uart_handle_usb_line("HV ENABLE",capture);assert(strstr(answer,"AFE_NOT_READY"));
    uart_handle_wifi_line("BEAM START",12,capture);assert(strstr(answer,"AFE_NOT_READY"));
    uart_handle_wifi_line("BRAM WR t_timing 9 1 0",12,capture);assert(strstr(answer,"AFE_NOT_READY"));
    uart_handle_wifi_line("AFE STATUS",12,capture);assert(strstr(answer,"state=sleep"));
    uart_handle_wifi_line("HV DISABLE",12,capture);assert(strstr(answer,"HV_DISABLE")&&!hv&&!run);
    uart_handle_wifi_line("AFE WAKE",12,capture);assert(strstr(answer,"AFE_WAKE")&&afe_ready&&!hv&&!run);
    tx_ready=0;
    uart_handle_wifi_line("BEAM START",12,capture);assert(strstr(answer,"TX_NOT_READY")&&!run&&!hv);
    uart_handle_usb_line("HV ENABLE",capture);assert(strstr(answer,"TX_NOT_READY")&&!hv);
    uart_handle_usb_line("CFG BEGIN 90",capture);assert(strstr(answer,"TX_NOT_READY"));
    uart_handle_wifi_line("TX7332 STATUS",12,capture);assert(strstr(answer,"state=sleep"));
    uart_handle_wifi_line("AFE SLEEP",12,capture);assert(!afe_ready&&!tx_ready);
    uart_handle_wifi_line("TX7332 WAKE",12,capture);assert(tx_ready&&!afe_ready&&!run&&!hv);
    uart_handle_wifi_line("BEAM START",12,capture);assert(strstr(answer,"AFE_NOT_READY")&&!run);
    uart_handle_wifi_line("AFE WAKE",12,capture);assert(afe_ready&&tx_ready&&!hv&&!run);
    uart_handle_wifi_line("CFG BEGIN 91",12,capture);assert(strstr(answer,"CFG_BEGIN"));
    uart_handle_wifi_line("TX7332 SLEEP",12,capture);assert(strstr(answer,"TX_OWNER_OR_CFG")&&tx_ready);
    uart_handle_wifi_line("CFG ABORT",12,capture);
    uart_handle_usb_line("TX7332 SLEEP",capture);assert(strstr(answer,"TX_OWNER_OR_CFG")&&tx_ready);
    cpu1_disabled=0; /* Requested warning-only policy must not add a scan/HV gate. */
    uart_handle_wifi_line("BEAM START",12,capture);assert(run&&!hv);
    uart_handle_wifi_line("HV ENABLE",12,capture);assert(hv&&run);
    uart_handle_wifi_line("HV DISABLE",12,capture);
    uart_handle_wifi_line("BEAM STOP",12,capture);assert(!run&&!hv);
    /* Explicit Wi-Fi action, including dual standby; never an imaging operation. */
    assert(cpu1_calls==0);
    cpu1_disabled=0;afe_ready=tx_ready=0;previous=writes;
    unsigned old_enables=enables,old_reports=report_requests;
    uart_handle_usb_line("CPU1 DISABLE",capture);assert(strstr(answer,"CPU1_OWNER_OR_CFG")&&cpu1_calls==0);
    assert(strstr(wifi(1001,"CPU1 DISABLE"),"CPU1_DISABLE cpu1_disabled=1"));
    assert(cpu1_calls==1&&cpu1_disabled&&report_requests==old_reports+1);
    assert(strstr(wifi(1001,"CPU1 DISABLE"),"CPU1_DISABLE"));assert(cpu1_calls==1); /* cached request */
    assert(!hv&&!run&&!afe_ready&&!tx_ready&&writes==previous&&enables==old_enables);
    cpu1_disabled=0;cpu1_failure="RESET_TIMEOUT";
    uart_handle_wifi_line("CPU1 DISABLE",12,capture);
    assert(strstr(answer,"@NACK")&&strstr(answer,"CPU1_RESET_TIMEOUT")&&!cpu1_disabled);
    cpu1_failure=NULL;afe_ready=tx_ready=1;
    uart_handle_wifi_line("CFG BEGIN 92",12,capture);assert(strstr(answer,"CFG_BEGIN"));
    unsigned calls=cpu1_calls;
    uart_handle_wifi_line("CPU1 DISABLE",12,capture);assert(strstr(answer,"CPU1_OWNER_OR_CFG")&&cpu1_calls==calls);
    uart_handle_wifi_line("CFG ABORT",12,capture);
    old_reports=report_requests;
    uart_handle_wifi_line("STATUS",12,capture);assert(strstr(answer,"cpu1_disabled=0")&&report_requests==old_reports+1);
    puts("PASS PS: framing/CRC/overflow/replay/owners/timeout/safe-END/legacy USB/AFE+TX power interlocks/bounded TX/CPU1 warning only");
    return 0;
}
