/* Small bounded register transactions; no vendor polled call with an
 * unbounded bus-busy/FIFO loop. Foreground owns IIC0; HV uses separate IIC1.
 * PS XADC accesses only: no fabric CDC or RTL additions. */
#include "temp_hw.h"
#include "temp.h"
#include "xparameters.h"
#include "xadcps.h"
#include "xiicps.h"
#include "xiicps_hw.h"
#include "xuartps_hw.h"
#include "xiltimer.h"
#include "sleep.h"
#include "xil_io.h"
#include "../uart/uart_log.h"
extern XIicPs iicps_0;
static XAdcPs adc;
static int adc_ready;
uint32_t temp_hw_now_ms(void) {
    XTime ticks; XTime_GetTime(&ticks);
    return (uint32_t)((ticks/COUNTS_PER_SECOND)*1000U+
                     (ticks%COUNTS_PER_SECOND)*1000U/COUNTS_PER_SECOND);
}
static int expired(uint32_t start) {
    return (uint32_t)(temp_hw_now_ms()-start)>=TEMP_IO_TIMEOUT_MS;
}
static int wait_bits(uint32_t base,uint32_t offset,uint32_t mask,uint32_t expected,uint32_t start) {
    for(;;) {
        if(expired(start)) return -1;
        if((Xil_In32(base+offset)&mask)==expected) return 0;
        usleep(10);
    }
}
static int fifo_command(uint32_t command,uint32_t *response,uint32_t start) {
    uint32_t base=adc.Config.BaseAddress;
    if(wait_bits(base,XADCPS_MSTS_OFFSET,XADCPS_MSTS_CFIFOF_MASK,0,start)) return -1;
    Xil_Out32(base+XADCPS_CMDFIFO_OFFSET,command);
    if(wait_bits(base,XADCPS_MSTS_OFFSET,XADCPS_MSTS_DFIFOE_MASK,0,start)) return -1;
    *response=Xil_In32(base+XADCPS_RDFIFO_OFFSET); return 0;
}
static int adc_read(uint32_t offset,uint32_t *value,uint32_t start) {
    uint32_t dummy;
    /* Pipelined DRP: read command -> discard prior response -> NOP -> result. */
    return fifo_command(XADCPS_JTAG_CMD_READ_MASK|(offset<<XADCPS_JTAG_ADDR_SHIFT),&dummy,start) ||
           fifo_command(0,value,start)?-1:0;
}
static void adc_abort(void) {
    uint32_t base=adc.Config.BaseAddress;
    uint32_t control=Xil_In32(base+XADCPS_MCTL_OFFSET);
    Xil_Out32(base+XADCPS_MCTL_OFFSET,control|XADCPS_MCTL_FLUSH_MASK);
    Xil_Out32(base+XADCPS_MCTL_OFFSET,control&~XADCPS_MCTL_FLUSH_MASK);
}
static int init_xadc(uint32_t start) {
    XAdcPs_Config *cfg=XAdcPs_LookupConfig(XPAR_XXADCPS_0_BASEADDR);
    adc_ready=0;
    if(!cfg || cfg->BaseAddress!=XPAR_XXADCPS_0_BASEADDR ||
       XAdcPs_CfgInitialize(&adc,cfg,cfg->BaseAddress)!=0) return -1;
    adc_abort(); /* Discard PS FIFO residue from a previous application run. */
    uint32_t cfr,dummy;
    /* Select built-in safe monitoring sequence; preserve ALL alarm bits and
     * thresholds. Never disable overtemperature protection or change limits. */
    if(adc_read(XADCPS_CFR1_OFFSET,&cfr,start) ||
       fifo_command(XADCPS_JTAG_CMD_WRITE_MASK|(XADCPS_CFR1_OFFSET<<XADCPS_JTAG_ADDR_SHIFT)|
                    (cfr&~XADCPS_CFR1_SEQ_VALID_MASK),&dummy,start)) {
        adc_abort(); return -1;
    }
    adc_ready=1; return 0;
}
int temp_hw_init_xadc(void) { return init_xadc(temp_hw_now_ms()); }
int temp_hw_xadc_raw(uint16_t *raw) {
    if(!raw) return -1;
    uint32_t value,start=temp_hw_now_ms();
    if(!adc_ready && init_xadc(start)) return -1;
    if(adc_read(XADCPS_TEMP_OFFSET,&value,start)) { adc_abort();adc_ready=0;return -1; }
    /* All-zero result is not a valid physical die temperature. */
    if(!(value&0xfff0U)) return -1;
    *raw=(uint16_t)value; return 0;
}
static uint32_t iic_error(uint32_t base) {
    return Xil_In32(base+XIICPS_ISR_OFFSET)&
        (XIICPS_IXR_NACK_MASK|XIICPS_IXR_ARB_LOST_MASK|XIICPS_IXR_TO_MASK|
         XIICPS_IXR_RX_OVR_MASK|XIICPS_IXR_RX_UNF_MASK|XIICPS_IXR_TX_OVR_MASK);
}
static int iic_complete(uint32_t base,uint32_t start) {
    for(;;) {
        if(expired(start) || iic_error(base)) return -1;
        if(Xil_In32(base+XIICPS_ISR_OFFSET)&XIICPS_IXR_COMP_MASK) return 0;
        usleep(10);
    }
}
int temp_hw_sensor_raw(uint32_t address,uint16_t *raw) {
    if(!raw || address<8 || address>0x77 ||
       iicps_0.IsReady!=XIL_COMPONENT_IS_READY ||
       iicps_0.Config.BaseAddress!=XPAR_XIICPS_0_BASEADDR) return -1;
    uint32_t base=XPAR_XIICPS_0_BASEADDR,start=temp_hw_now_ms();
    if(wait_bits(base,XIICPS_SR_OFFSET,XIICPS_SR_BA_MASK,0,start)) return -1;
    uint32_t saved=Xil_In32(base+XIICPS_CR_OFFSET);
    uint32_t masked=Xil_In32(base+XIICPS_IMR_OFFSET);
    uint32_t control=(saved&(XIICPS_CR_DIV_A_MASK|XIICPS_CR_DIV_B_MASK))|
        XIICPS_CR_MS_MASK|XIICPS_CR_NEA_MASK|XIICPS_CR_ACKEN_MASK;
    Xil_Out32(base+XIICPS_IDR_OFFSET,XIICPS_IXR_ALL_INTR_MASK);
    Xil_Out32(base+XIICPS_ISR_OFFSET,XIICPS_IXR_ALL_INTR_MASK);
    Xil_Out32(base+XIICPS_CR_OFFSET,control|XIICPS_CR_CLR_FIFO_MASK);
    Xil_Out32(base+XIICPS_DATA_OFFSET,TEMP_DATA_REGISTER);
    Xil_Out32(base+XIICPS_ADDR_OFFSET,address); /* Register pointer + STOP. */
    int result=-1;
    if(iic_complete(base,start) || wait_bits(base,XIICPS_SR_OFFSET,XIICPS_SR_BA_MASK,0,start)) goto cleanup;
    Xil_Out32(base+XIICPS_ISR_OFFSET,XIICPS_IXR_ALL_INTR_MASK);
    Xil_Out32(base+XIICPS_CR_OFFSET,control|XIICPS_CR_RD_WR_MASK|XIICPS_CR_CLR_FIFO_MASK);
    Xil_Out32(base+XIICPS_TRANS_SIZE_OFFSET,2);
    Xil_Out32(base+XIICPS_ADDR_OFFSET,address);
    uint16_t value=0;
    for(unsigned i=0;i<2;i++) {
        for(;;) {
            if(expired(start) || iic_error(base)) goto cleanup;
            if(Xil_In32(base+XIICPS_SR_OFFSET)&XIICPS_SR_RXDV_MASK) break;
            usleep(10);
        }
        value=(uint16_t)((value<<8)|(Xil_In32(base+XIICPS_DATA_OFFSET)&0xffU));
    }
    if(iic_complete(base,start) || wait_bits(base,XIICPS_SR_OFFSET,XIICPS_SR_BA_MASK,0,start)) goto cleanup;
    *raw=value; result=0;
cleanup:
    /* Abort owned transfer/FIFO, clear HOLD and status, restore divisor/options
     * and IRQ mask. Never touch IIC1, pin mux or try GPIO bus recovery. */
    if(result!=0) Xil_Out32(base+XIICPS_CR_OFFSET,XIICPS_CR_RESET_VALUE|XIICPS_CR_CLR_FIFO_MASK);
    Xil_Out32(base+XIICPS_CR_OFFSET,(saved&~XIICPS_CR_HOLD_MASK)|XIICPS_CR_CLR_FIFO_MASK);
    Xil_Out32(base+XIICPS_TRANS_SIZE_OFFSET,0);
    Xil_Out32(base+XIICPS_ISR_OFFSET,XIICPS_IXR_ALL_INTR_MASK);
    Xil_Out32(base+XIICPS_IER_OFFSET,(~masked)&XIICPS_IXR_ALL_INTR_MASK);
    return result;
}
int temp_hw_send(const char *p,size_t n) {
    static int partial;
    if(!p) return -1;
    uint32_t start=temp_hw_now_ms();
    /* A previous timeout may leave half a line. Delimit it before the next
     * periodic report rather than replaying any failed bytes. */
    if(partial) {
        const char boundary[]="\r\n";
        for(unsigned i=0;i<2;i++) {
            if(wait_bits(ZYNQ_LOG_UART_BASEADDR,XUARTPS_SR_OFFSET,XUARTPS_SR_TXFULL,0,start)) return -1;
            Xil_Out32(ZYNQ_LOG_UART_BASEADDR+XUARTPS_FIFO_OFFSET,boundary[i]);
        }
    }
    partial=1;
    for(size_t i=0;i<n;i++) {
        if(wait_bits(ZYNQ_LOG_UART_BASEADDR,XUARTPS_SR_OFFSET,XUARTPS_SR_TXFULL,0,start)) return -1;
        Xil_Out32(ZYNQ_LOG_UART_BASEADDR+XUARTPS_FIFO_OFFSET,(unsigned char)p[i]);
    }
    partial=0; return 0;
}
