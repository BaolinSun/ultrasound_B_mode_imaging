/* Exercise real PS MMIO control, not a mocked Beamform_Resume implementation. */
#define __HV_H
#include <assert.h>
#include <stdio.h>
#include "xbram.h"
void hv_enable(void);
void hv_disable(void);
#include "../../build/src/ps/bram/bram.c"

u32 fdemod_cos_array[8192],fdemod_sin_array[8192],log_table_hex[1024];
u32 x_element_hex[64],hamming_hex[64],dfilter_hex[64],T_timing_hex[10];
u32 sin_beta_hex[64],delay_profile_reg[32],tx7332_config_data[128],delay_profile[16384];
static struct { u32 address,value; } mmio[16];
static unsigned count,reads,hv,waits;
void XBram_WriteReg(u32 base,u32 offset,u32 value) {
    assert(count<16);mmio[count].address=base+offset;mmio[count++].value=value;
}
u32 XBram_ReadReg(u32 base,u32 offset) {
    /* Timing RAM has no readback: status/busy logic must never read it. */
    assert(base+offset==BRAM_TX_CFG_RDDATA_ADDR);reads++;return 0x12345678U;
}
int usleep(unsigned usec) { assert(usec==100||usec==5000||usec==10000);waits++;return 0; }
unsigned sleep(unsigned seconds) { assert(seconds==1);return 0; }
void hv_enable(void) { hv=1; }
void hv_disable(void) { hv=0; }
static void check_resume(void) {
    count=waits=0;
    Beamform_Resume();
    assert(count==6&&waits==2);
    for(unsigned i=0;i<6;i++) {
        assert(mmio[i].address==(i%2?BRAM_SPI_START_ADDR:BRAM_BEAMFORM_CTRL_ADDR));
        assert(mmio[i].value==(i>=3?1U:0U));
    }
    assert(Beamform_GetRunRequest()==1&&Beamform_GetSpiStartRequest()==1&&!hv&&!reads);
}
int main(void) {
    assert(!Beamform_GetRunRequest());
    Beamform_Halt();check_resume();
    u32 data=0xdeadbeefU;count=0;
    assert(Tx7332_ReadRegister(1,&data)==BRAM_CFG_STATUS_BUSY&&data==0xdeadbeefU&&!count&&!reads);
    Beamform_Pause();assert(!Beamform_GetRunRequest()&&Beamform_GetSpiStartRequest()==1);
    check_resume();Beamform_Halt();assert(!Beamform_GetRunRequest()&&!Beamform_GetSpiStartRequest());
    count=0;assert(Tx7332_ReadRegister(1,&data)==BRAM_CFG_STATUS_OK&&data==0x12345678U&&reads==1);
    assert(count==1&&mmio[0].address==BRAM_TX_CFG_RD_REQ_ADDR);
    Beamform_Start();assert(hv&&Beamform_GetRunRequest()); /* Legacy startup compatibility. */
    Beamform_Stop();assert(!hv&&!Beamform_GetRunRequest()&&!Beamform_GetSpiStartRequest());
    puts("PASS actual PS MMIO: clean SPI edge/resume/pause/halt/run shadow/no write-only reads/TX busy/legacy start-stop");
    return 0;
}
