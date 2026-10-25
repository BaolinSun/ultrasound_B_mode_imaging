#include "uart_log.h"
#include "xuartps.h"
#include "uart.h"
#include "xscugic.h"
#include "xinterrupt_wrap.h"
#include "xiltimer.h"
#include "xil_exception.h"
#include "../../../../common/wifi_control_protocol.h"

static XUartPs log_uart;
extern XScuGic xInterruptController;
static volatile unsigned wr,rd,queued,lost;
static volatile char lines[4][WC_LINE];
static char partial[WC_LINE];
static unsigned used;
static int dropping,command_ready;
static uint32_t peer,highest,last_request;
static char saved_command[WC_COMMAND],saved_response[WC_RESPONSE];
static char response[WC_RESPONSE];

int zynq_uart_log_init(void)
{
    XUartPs_Config *cfg=XUartPs_LookupConfig(ZYNQ_LOG_UART_BASEADDR);
    XUartPsFormat format={
        .BaudRate=ZYNQ_LOG_UART_BAUD,
        .DataBits=XUARTPS_FORMAT_8_BITS,
        .Parity=XUARTPS_FORMAT_NO_PARITY,
        .StopBits=XUARTPS_FORMAT_1_STOP_BIT
    };
    if(!cfg || cfg->BaseAddress!=ZYNQ_LOG_UART_BASEADDR) return XST_FAILURE;
    if(XUartPs_CfgInitialize(&log_uart,cfg,cfg->BaseAddress)!=XST_SUCCESS ||
       XUartPs_SetBaudRate(&log_uart,ZYNQ_LOG_UART_BAUD)!=XST_SUCCESS ||
       XUartPs_SetDataFormat(&log_uart,&format)!=XST_SUCCESS) return XST_FAILURE;
    XUartPs_SetOperMode(&log_uart,XUARTPS_OPER_MODE_NORMAL);
    XUartPs_SetInterruptMask(&log_uart,0U); /* Enable RX only after GIC init. */
    return XST_SUCCESS;
}
static void rx_byte(unsigned char c)
{
    if(c=='\r'||c=='\n') {
        if(!dropping && used) {
            if(queued<4) {
                for(unsigned i=0;i<=used;i++) lines[wr][i]=i==used?0:partial[i];
                wr=(wr+1)%4; queued++;
            } else lost++;
        }
        used=0; dropping=0; return;
    }
    if(dropping) return;
    if(c<32 || c>126 || used>=WC_LINE-1) { used=0; dropping=1; lost++; return; }
    partial[used++]=(char)c;
}
static void receive_isr(void *ctx)
{
    (void)ctx;
    u32 status=XUartPs_ReadReg(ZYNQ_LOG_UART_BASEADDR,XUARTPS_ISR_OFFSET);
    if(status&(XUARTPS_IXR_OVER|XUARTPS_IXR_FRAMING|XUARTPS_IXR_PARITY)) { lost++; used=0; dropping=1; }
    while(XUartPs_IsReceiveData(ZYNQ_LOG_UART_BASEADDR))
        rx_byte((u8)XUartPs_ReadReg(ZYNQ_LOG_UART_BASEADDR,XUARTPS_FIFO_OFFSET));
    XUartPs_WriteReg(ZYNQ_LOG_UART_BASEADDR,XUARTPS_ISR_OFFSET,status);
}
int zynq_uart_command_init(void)
{
    u32 irq=(u32)(XGet_IntrId(log_uart.Config.IntrId)+XGet_IntrOffset(log_uart.Config.IntrId));
    if(XScuGic_Connect(&xInterruptController,irq,receive_isr,NULL)!=XST_SUCCESS) return XST_FAILURE;
    XUartPs_SetFifoThreshold(&log_uart,16); XUartPs_SetRecvTimeout(&log_uart,4);
    XScuGic_InterruptMaptoCpu(&xInterruptController,0U,irq);
    XUartPs_SetInterruptMask(&log_uart,XUARTPS_IXR_RXOVR|XUARTPS_IXR_TOUT|
        XUARTPS_IXR_OVER|XUARTPS_IXR_FRAMING|XUARTPS_IXR_PARITY);
    XScuGic_Enable(&xInterruptController,irq); command_ready=1;
    return XST_SUCCESS;
}
static void capture_response(const char *text) { snprintf(response,sizeof(response),"%s",text); }
static void send_response(uint32_t session,uint32_t request,const char *text)
{
    char wire[WC_LINE]; int n=wc_encode(wire,sizeof(wire),true,session,request,text);
    XTime start,now; XTime_GetTime(&start);
    unsigned polls=0;
    for(int i=0;i<n;i++) {
        while(XUartPs_IsTransmitFull(ZYNQ_LOG_UART_BASEADDR)) {
            XTime_GetTime(&now); if(now-start>COUNTS_PER_SECOND/50U || ++polls>=1000000U) return;
        }
        XUartPs_WriteReg(ZYNQ_LOG_UART_BASEADDR,XUARTPS_FIFO_OFFSET,(u8)wire[i]);
    }
}
void zynq_uart_command_poll(void)
{
    if(!command_ready) return;
    char line[WC_LINE]; int have=0;
    /* UART IMR alone cannot exclude an interrupt already pending at the GIC. */
    Xil_ExceptionDisableMask(XIL_EXCEPTION_IRQ);
    u32 mask=XUartPs_ReadReg(ZYNQ_LOG_UART_BASEADDR,XUARTPS_IMR_OFFSET);
    XUartPs_SetInterruptMask(&log_uart,0);
    unsigned overflow=lost;
    if(lost) { lost=0; queued=0; rd=wr; } /* Never forget replay high-water on RX loss. */
    if(queued) {
        for(unsigned i=0;i<WC_LINE;i++) { line[i]=lines[rd][i]; if(!line[i]) break; }
        rd=(rd+1)%4; queued--; have=1;
    }
    XUartPs_SetInterruptMask(&log_uart,mask);
    Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
    if(overflow) xil_printf("@WIFI_RX overflow/error; queued commands discarded\r\n");
    if(!have) return;
    wc_message m;
    if(!wc_decode(line,false,&m)) { xil_printf("@WIFI_RX invalid envelope/CRC\r\n"); return; }
    if(m.session!=peer) {
        if(strcmp(m.text,"STATUS")) { send_response(m.session,m.request,"@NACK 0 HANDSHAKE_REQUIRED"); return; }
        peer=m.session; highest=last_request=0;
    }
    if(m.request==last_request) {
        send_response(peer,m.request,!strcmp(m.text,saved_command)?saved_response:"@NACK 0 REQUEST_CONFLICT"); return;
    }
    if(m.request<=highest) { send_response(peer,m.request,"@NACK 0 STALE_REQUEST"); return; }
    highest=m.request; last_request=m.request;
    strcpy(saved_command,m.text); strcpy(response,"@NACK 0 NO_RESPONSE");
    uart_handle_wifi_line(m.text,m.session,capture_response);
    strcpy(saved_response,response); send_response(peer,m.request,response);
}
