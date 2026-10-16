#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "xstatus.h"
#include "xiltimer.h"
typedef unsigned u32;
#ifndef XST_INVALID_PARAM
#define XST_INVALID_PARAM 2
#endif
#define _AFE5832_SPI_H
#define BASE_ADDRESS 0U
#define AFE5832_CTRL_PS_S00_AXI_SLV_REG0_OFFSET 0U
#define AFE5832_CTRL_PS_S00_AXI_SLV_REG1_OFFSET 4U
#define AFE5832_CTRL_PS_S00_AXI_SLV_REG3_OFFSET 12U
#define AFE5832_CTRL_PS_S00_AXI_SLV_REG4_OFFSET 16U
static unsigned ready,writes,frozen;
static uint64_t ticks;
void XTime_GetTime(XTime *t) { if(!frozen)ticks+=1000;*t=ticks; }
static unsigned read_register(unsigned base,unsigned off) { (void)base;return off==16?0xbeefU:ready?0x00400800U:0; }
static void write_register(unsigned base,unsigned off,unsigned value) { (void)base;(void)off;(void)value;writes++; }
#define AFE5832_CTRL_PS_mReadReg(b,o) read_register(b,o)
#define AFE5832_CTRL_PS_mWriteReg(b,o,v) write_register(b,o,v)
#include "../../build/src/ps/AFE5832/afe5832_spi.c"
int main(void) {
    ready=1;assert(afe5832_spi_write_byte(1,2)==XST_SUCCESS);
    unsigned data=0;assert(afe5832_spi_read_byte(1,&data)==XST_SUCCESS&&data==0xbeef);
    ready=0;ticks=0;assert(afe5832_spi_write_byte(1,2)==XST_FAILURE&&ticks<=23000);
    ticks=0;data=0x1234;assert(afe5832_spi_read_byte(1,&data)==XST_FAILURE&&data==0x1234&&ticks<=23000);
    assert(afe5832_spi_read_byte(1,NULL)==XST_INVALID_PARAM);
    frozen=1;assert(afe5832_spi_write_byte(1,2)==XST_FAILURE);
    puts("PASS AFE SPI: actual register loops/success/failure/20ms deadline/no fake zero read");return 0;
}
