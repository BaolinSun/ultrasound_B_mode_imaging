/* Exercise real PS MMIO control, not a mocked Beamform_Resume implementation. */
#define __HV_H
#include <assert.h>
#include <stdio.h>
#include "xbram.h"
void hv_enable(void);
void hv_disable(void);
static int resume_failure,boot_wait;
int afe_power_allowed(void) { return 1; }
int tx_power_allowed(void) { return 1; }
int frame_store_resume(void) { return resume_failure?-1:0; }
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
    assert(Beamform_Resume()==0);
    assert(count==6&&waits==2);
    for(unsigned i=0;i<6;i++) {
        assert(mmio[i].address==(i%2?BRAM_SPI_START_ADDR:BRAM_BEAMFORM_CTRL_ADDR));
        assert(mmio[i].value==(i>=3?1U:0U));
    }
    assert(Beamform_GetRunRequest()==1&&Beamform_GetSpiStartRequest()==1&&!hv&&!reads);
}
int main(void) {
    assert(!Beamform_GetRunRequest());
    boot_wait=1;count=0;
    assert(Beamform_Resume()==-1 && Beamform_Start()==-1 && !count && !hv);
    Beamform_SetControl(1,1);assert(!count);
    boot_wait=0;
    Beamform_Halt();check_resume();
    u32 data=0xdeadbeefU;count=0;
    assert(Tx7332_ReadRegister(1,&data)==BRAM_CFG_STATUS_BUSY&&data==0xdeadbeefU&&!count&&!reads);
    Beamform_Pause();assert(!Beamform_GetRunRequest()&&Beamform_GetSpiStartRequest()==1);
    check_resume();Beamform_Halt();assert(!Beamform_GetRunRequest()&&!Beamform_GetSpiStartRequest());
    count=0;assert(Tx7332_ReadRegister(1,&data)==BRAM_CFG_STATUS_OK&&data==0x12345678U&&reads==1);
    assert(count==1&&mmio[0].address==BRAM_TX_CFG_RD_REQ_ADDR);
    Beamform_Start();assert(hv&&Beamform_GetRunRequest()); /* Legacy startup compatibility. */
    Beamform_Stop();assert(!hv&&!Beamform_GetRunRequest()&&!Beamform_GetSpiStartRequest());
    resume_failure=1;hv=1;count=waits=0;
    assert(Beamform_Resume()==-1 && !hv && !Beamform_GetRunRequest() && !Beamform_GetSpiStartRequest());
    assert(count==2 && waits==0);
    /* Successful timing writes update only their software shadow. Never
     * pretend the write-only parameter aperture supports readback. */
    for(unsigned i=0;i<10;i++) T_timing_hex[i]=100+i;
    T_timing_hex[0]=6144;T_timing_hex[9]=0;
    for(u32 down=0;down<=4;down++) {
        count=0;
        assert(Bram_WriteSectionWords("t_timing",9,&down,1)==BRAM_CFG_STATUS_OK);
        assert(count==1&&mmio[0].address==BRAM_SCAN_ADDR+9*4&&mmio[0].value==down);
        assert(Beamform_GetDownsample()==down);
        for(unsigned i=1;i<9;i++) assert(T_timing_hex[i]==100+i);
        assert(T_timing_hex[0]==6144&&reads==1);
    }
    u32 invalid=5,pair[2]={999,5};count=0;
    assert(Bram_WriteSectionWords("t_timing",9,&invalid,1)==BRAM_CFG_STATUS_RANGE);
    assert(Bram_WriteSectionWords("t_timing",8,pair,2)==BRAM_CFG_STATUS_RANGE);
    assert(!count&&Beamform_GetDownsample()==4&&T_timing_hex[8]==108);
    invalid=0;assert(Bram_WriteSectionWords("t_timing",10,&invalid,1)==BRAM_CFG_STATUS_RANGE);
    hv=1;count=0;
    assert(Beamform_Start()==-1 && !hv && !Beamform_GetRunRequest() && !Beamform_GetSpiStartRequest());
    assert(count==2 && waits==0);
    puts("PASS actual PS MMIO: clean SPI edge/resume/pause/halt/run shadow/no write-only reads/TX busy/legacy start-stop");
    return 0;
}

int startup_recovery_blocks(void) { return boot_wait; }
