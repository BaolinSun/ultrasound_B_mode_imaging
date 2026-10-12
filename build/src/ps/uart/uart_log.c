#include "uart_log.h"
#include "xuartps.h"

int zynq_uart_log_init(void)
{
    static XUartPs log_uart;
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
    XUartPs_SetInterruptMask(&log_uart,0U); /* No UART0 RX command handling. */
    return XST_SUCCESS;
}
