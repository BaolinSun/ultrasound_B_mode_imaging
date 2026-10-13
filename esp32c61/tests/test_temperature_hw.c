/* Execute the actual bounded register adapter against a deterministic bus.
 * Register offsets/masks come from the current 2026.1 BSP, not copied stubs. */
#include "temp_hw.h"
#include "temp.h"
#include "xadcps.h"
#include "xiicps.h"
#include "xuartps_hw.h"
#include "xparameters.h"
#include "xiltimer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
XIicPs iicps_0={{XPAR_XIICPS_0_BASEADDR},XIL_COMPONENT_IS_READY};
static XAdcPs_Config config={XPAR_XXADCPS_0_BASEADDR};
static uint64_t time_us;
static uint32_t cr=0x450e,isr,imr=0x2fc,transfer;
static unsigned rx_index,rx_count,addr_writes,flushes;
static int cfg_failure,no_cfg,wrong_cfg;
enum { GOOD,BUS_BUSY,NACK,TX_TIMEOUT,RX_TIMEOUT,ARB_LOST,RX_NACK,RX_COMPLETE_TIMEOUT,TX_STUCK,RX_STUCK };
static int iic_fault;
static int adc_full,adc_empty,uart_full,uart_fail_after=-1;
static uint32_t cfr=0xa55a,pending_response,response;
static uint16_t adc_raw=0x9770;
static int response_ready;
static unsigned writes_cfr;
static char uart_bytes[256];
static unsigned uart_count;
void XTime_GetTime(XTime *t) { *t=time_us; }
int usleep(unsigned us) { time_us+=us;return 0; }
XAdcPs_Config *XAdcPs_LookupConfig(UINTPTR a) {
    assert(a==XPAR_XXADCPS_0_BASEADDR);
    config.BaseAddress=wrong_cfg?a+4:a;return no_cfg?NULL:&config;
}
int XAdcPs_CfgInitialize(XAdcPs *p,XAdcPs_Config *c,u32 base) {
    p->Config=*c;assert(base==XPAR_XXADCPS_0_BASEADDR);return cfg_failure?-1:0;
}
u32 Xil_In32(UINTPTR a) {
    if(a>=XPAR_XIICPS_0_BASEADDR && a<XPAR_XIICPS_0_BASEADDR+0x100) {
        switch(a-XPAR_XIICPS_0_BASEADDR) {
        case XIICPS_CR_OFFSET:return cr;
        case XIICPS_IMR_OFFSET:return imr;
        case XIICPS_ISR_OFFSET:return isr;
        case XIICPS_SR_OFFSET:return (iic_fault==BUS_BUSY || (iic_fault==TX_STUCK && addr_writes) ||
             (iic_fault==RX_STUCK && (cr&XIICPS_CR_RD_WR_MASK)))?XIICPS_SR_BA_MASK:
             rx_index<rx_count?XIICPS_SR_RXDV_MASK:0;
        case XIICPS_DATA_OFFSET:assert(rx_index<rx_count);return rx_index++?0xe0:0xf5;
        default:assert(0);
        }
    }
    if(a>=XPAR_XXADCPS_0_BASEADDR && a<XPAR_XXADCPS_0_BASEADDR+0x100) {
        switch(a-XPAR_XXADCPS_0_BASEADDR) {
        case XADCPS_MSTS_OFFSET:return (adc_full?XADCPS_MSTS_CFIFOF_MASK:0)|
            ((!response_ready||adc_empty)?XADCPS_MSTS_DFIFOE_MASK:0);
        case XADCPS_RDFIFO_OFFSET:assert(response_ready);response_ready=0;return response;
        case XADCPS_MCTL_OFFSET:return 0;
        default:assert(0);
        }
    }
    assert(a==0xe0000000U+XUARTPS_SR_OFFSET);
    return uart_full||(uart_fail_after>=0 && uart_count>=(unsigned)uart_fail_after)?XUARTPS_SR_TXFULL:0;
}
void Xil_Out32(UINTPTR a,u32 v) {
    if(a>=XPAR_XIICPS_0_BASEADDR && a<XPAR_XIICPS_0_BASEADDR+0x100) {
        switch(a-XPAR_XIICPS_0_BASEADDR) {
        case XIICPS_CR_OFFSET:
            cr=v&~XIICPS_CR_CLR_FIFO_MASK;
            if(v&XIICPS_CR_CLR_FIFO_MASK)rx_count=rx_index=0;
            break;
        case XIICPS_ISR_OFFSET:isr&=~v;break;
        case XIICPS_IDR_OFFSET:imr|=v;break;
        case XIICPS_IER_OFFSET:imr&=~v;break;
        case XIICPS_DATA_OFFSET:assert(v==0);break;
        case XIICPS_TRANS_SIZE_OFFSET:transfer=v;break;
        case XIICPS_ADDR_OFFSET:
            assert(v==0x48 || v==0x49);addr_writes++;
            if(iic_fault==NACK)isr|=XIICPS_IXR_NACK_MASK;
            else if(iic_fault==ARB_LOST)isr|=XIICPS_IXR_ARB_LOST_MASK;
            else if(!(cr&XIICPS_CR_RD_WR_MASK)) {
                if(iic_fault!=TX_TIMEOUT)isr|=XIICPS_IXR_COMP_MASK;
            } else if(iic_fault!=RX_TIMEOUT) {
                assert(transfer==2);rx_count=2;rx_index=0;
                if(iic_fault==RX_NACK)isr|=XIICPS_IXR_NACK_MASK;
                else if(iic_fault!=RX_COMPLETE_TIMEOUT)isr|=XIICPS_IXR_COMP_MASK;
            }
            break;
        default:assert(0);
        }
        return;
    }
    if(a>=XPAR_XXADCPS_0_BASEADDR && a<XPAR_XXADCPS_0_BASEADDR+0x100) {
        switch(a-XPAR_XXADCPS_0_BASEADDR) {
        case XADCPS_CMDFIFO_OFFSET:{
            unsigned reg=(v&XADCPS_JTAG_ADDR_MASK)>>XADCPS_JTAG_ADDR_SHIFT;
            response=pending_response;response_ready=1;
            if((v&XADCPS_JTAG_CMD_READ_MASK)==XADCPS_JTAG_CMD_READ_MASK) {
                assert(reg==XADCPS_CFR1_OFFSET || reg==XADCPS_TEMP_OFFSET);
                pending_response=reg==XADCPS_CFR1_OFFSET?cfr:adc_raw;
            } else if(v&XADCPS_JTAG_CMD_WRITE_MASK) {
                assert(reg==XADCPS_CFR1_OFFSET);cfr=v&0xffffU;writes_cfr++;pending_response=0;
            } else { assert(v==0);pending_response=0; }
            break;
        }
        case XADCPS_MCTL_OFFSET:
            if(v&XADCPS_MCTL_FLUSH_MASK){flushes++;response_ready=0;pending_response=0;}
            break;
        default:assert(0);
        }
        return;
    }
    assert(a==0xe0000000U+XUARTPS_FIFO_OFFSET && uart_count<sizeof(uart_bytes));
    uart_bytes[uart_count++]=(char)v;
}
static void bounded_failure(int kind) {
    iic_fault=kind;isr=0;rx_count=rx_index=0;addr_writes=0;
    uint64_t start=time_us;uint16_t value=0x1234;
    uint32_t saved_cr=cr,saved_mask=imr;
    assert(temp_hw_sensor_raw(0x48,&value)!=0 && value==0x1234);
    assert(time_us-start<=20000);
    assert(cr==(saved_cr&~XIICPS_CR_HOLD_MASK) && imr==saved_mask);
    iic_fault=GOOD;isr=0;
    assert(temp_hw_sensor_raw(0x49,&value)==0 && value==0xf5e0);
}
int main(void) {
    uint16_t raw;
    assert(temp_hw_sensor_raw(0x48,NULL)!=0);
    assert(temp_hw_sensor_raw(0,&raw)!=0);
    assert(temp_hw_sensor_raw(0x78,&raw)!=0);
    iicps_0.IsReady=0;assert(temp_hw_sensor_raw(0x48,&raw)!=0);
    iicps_0.IsReady=XIL_COMPONENT_IS_READY;
    iicps_0.Config.BaseAddress++;assert(temp_hw_sensor_raw(0x48,&raw)!=0);
    iicps_0.Config.BaseAddress=XPAR_XIICPS_0_BASEADDR;
    uint32_t saved=cr,mask=imr;
    assert(temp_hw_sensor_raw(0x48,&raw)==0 && raw==0xf5e0);
    assert(addr_writes==2 && cr==saved && imr==mask && transfer==0);
    for(int kind=BUS_BUSY;kind<=RX_STUCK;kind++)bounded_failure(kind);
    no_cfg=1;assert(temp_hw_init_xadc()!=0);no_cfg=0;
    wrong_cfg=1;assert(temp_hw_init_xadc()!=0);wrong_cfg=0;
    cfg_failure=1;assert(temp_hw_init_xadc()!=0);cfg_failure=0;
    uint32_t alarm_bits=cfr&~XADCPS_CFR1_SEQ_VALID_MASK;
    assert(temp_hw_init_xadc()==0 && cfr==alarm_bits && writes_cfr==1);
    unsigned flush_base=flushes;
    assert(temp_hw_xadc_raw(&raw)==0 && raw==0x9770);
    assert(temp_hw_xadc_raw(NULL)!=0);
    adc_full=1;uint64_t start=time_us;
    assert(temp_hw_xadc_raw(&raw)!=0 && time_us-start==20000 && flushes==flush_base+1);
    adc_full=0;adc_empty=1;start=time_us;
    assert(temp_hw_xadc_raw(&raw)!=0 && time_us-start==20000 && flushes==flush_base+3);
    adc_empty=0;
    assert(temp_hw_xadc_raw(&raw)==0 && raw==0x9770); // FIFO recovery + reinit.
    adc_raw=0;assert(temp_hw_xadc_raw(&raw)!=0);adc_raw=0x9770;
    start=time_us;uart_full=1;
    assert(temp_hw_send("abc",3)!=0 && time_us-start==20000);
    start=time_us;assert(temp_hw_send("ignored",7)!=0 && time_us-start==20000);
    uart_full=0;assert(temp_hw_send("abc",3)==0);
    assert(uart_count==5 && !memcmp(uart_bytes,"\r\nabc",5));
    uart_fail_after=(int)uart_count+1;start=time_us;
    assert(temp_hw_send("XYZ",3)!=0 && time_us-start==20000);
    uart_fail_after=-1;assert(temp_hw_send("ok\r\n",4)==0);
    assert(!memcmp(uart_bytes+6,"\r\nok\r\n",6));
    assert(temp_hw_send(NULL,1)!=0);
    time_us=(uint64_t)UINT32_MAX*1000+1000;
    assert(temp_hw_now_ms()==0); // Real tick conversion wraps at 2^32 ms.
    puts("PASS: real BSP register masks, IIC busy/NACK/arbitration/timeouts/recovery, XADC FIFO/alarm preservation and bounded UART resync");
    return 0;
}
