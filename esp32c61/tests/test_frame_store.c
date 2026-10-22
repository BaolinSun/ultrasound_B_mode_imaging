#include "frame_store.h"
#include "xiltimer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static u32 regfile[32],hold,magic=FS_MAGIC;
static u64 now;
static unsigned dma_stuck,magic_reads,fail_magic_read,clock_stuck;
static unsigned clock_writes,reset_writes,reset_reads,reset_delay,time_divider=1;
static u32 sd_control=0x000e0207U,sd_present;
static u8 sd_reset;
u8 Xil_In8(UINTPTR a) {
    /* Do not let an over-permissive mock accept byte Clock Control access. */
    assert(a==0xe010102fU);
    if(sd_reset && !dma_stuck && reset_reads++>=reset_delay)sd_reset=0;
    return sd_reset;
}
void Xil_Out8(UINTPTR a,u8 v) {
    assert(a==0xe010102fU && v==6 && (sd_control&3U)==3U);
    sd_reset=v;reset_reads=0;reset_writes++;
}
u16 Xil_In16(UINTPTR a) {
    assert(a==0xe010102cU);
    if((sd_control&1U) && !clock_stuck)sd_control|=2U;
    return (u16)sd_control;
}
void Xil_Out16(UINTPTR a,u16 v) {
    assert(a==0xe010102cU);
    assert((v&~1U)==((u16)sd_control&~1U)); /* Preserve full divider/card clock. */
    sd_control=(sd_control&0xffff0000U)|v;clock_writes++;
}
void XTime_GetTime(XTime *t) { *t=now++/time_divider; }
u32 Xil_In32(UINTPTR a) {
    if(a==0xe0101024U)return sd_present;
    if(a==0xe010102cU)return sd_control|((u32)sd_reset<<24);
    assert(a>=FS_BASE && a<FS_BASE+128);
    if(a==FS_BASE) { magic_reads++;return magic_reads==fail_magic_read?0:magic; }
    return regfile[(a-FS_BASE)/4];
}
void Xil_Out32(UINTPTR a,u32 n) {
    assert(a>=FS_BASE && a<FS_BASE+128);regfile[(a-FS_BASE)/4]=n;
    if(a==FS_BASE+FS_SEQ && !hold) {
        regfile[FS_ACK/4]=n;
        if(regfile[FS_CMD/4]==FS_INIT && !regfile[FS_RESULT/4])regfile[FS_FLAGS/4]=1;
    }
}
int main(void) {
    frame_lease_t f={0};
    assert(frame_store_pause_retained()!=0);
    assert(frame_store_acquire(&f,0)==-1);
    const volatile frame_store_init_diag_t *diag=frame_store_init_diagnostic();
    magic=0;assert(frame_store_init()==-1 && diag->stage==FS_INIT_VERSION);magic=FS_MAGIC;
    assert(!frame_store_is_ready());
    /* Cold start: internal clock off, no card clock is enabled by cleanup. */
    sd_control=0;
    assert(frame_store_init()==0 && clock_writes==1 && reset_writes==1);
    assert(diag->sd_cleanup_phase==FS_SD_DONE && diag->sd_clock_before==0);
    assert(diag->sd_clock_ready==3 && diag->sd_clock_after==3 && !diag->sd_reset_after);
    assert(frame_store_is_ready());
    regfile[FS_FLAGS/4]=9;assert(frame_store_pause()==0);
    u32 retained_seq=regfile[FS_SEQ/4];assert(frame_store_pause_retained()==0&&regfile[FS_SEQ/4]==retained_seq);
    regfile[FS_FLAGS/4]=13;assert(frame_store_pause_retained()!=0);regfile[FS_FLAGS/4]=9;
    assert(frame_store_resume()==0);assert(frame_store_pause_retained()!=0);
    /* Warm restart: preserve existing bus clock and divisor, clear old reset. */
    sd_control=0x000e0207U;sd_reset=6;
    assert(frame_store_init()==0 && clock_writes==1 && diag->sd_clock_after==0x000e0207U);
    /* 12 ms delayed completion; elapsed must not be divided by four. */
    now=0;time_divider=25000;reset_delay=300000;
    assert(frame_store_init()==0 && diag->dma_elapsed_ms>=12 && diag->dma_elapsed_ms<20);
    time_divider=1;reset_delay=0;now=0;
    unsigned resets=reset_writes;u32 submitted=regfile[FS_SEQ/4];
    sd_control=0;clock_stuck=1;
    assert(frame_store_init()==-1 && diag->stage==FS_INIT_DMA && diag->sd_cleanup_phase==FS_SD_CLOCK);
    assert(reset_writes==resets && regfile[FS_SEQ/4]==submitted && !frame_store_is_ready());
    assert(diag->dma_elapsed_ms>=20);
    assert(diag->dma_elapsed_ms<=22);
    clock_stuck=0;dma_stuck=1;
    assert(frame_store_init()==-1 && diag->stage==FS_INIT_DMA && diag->sd_cleanup_phase==FS_SD_RESET);
    assert(diag->sd_reset_after==6 && !diag->sequence && !frame_store_is_ready());
    dma_stuck=0;sd_present=7;
    assert(frame_store_init()==-1 && diag->sd_cleanup_phase==FS_SD_IDLE);
    assert(regfile[FS_SEQ/4]==submitted && !diag->sequence && !frame_store_is_ready());
    sd_present=0;
    magic_reads=0;fail_magic_read=2;
    assert(frame_store_init()==-1 && diag->stage==FS_INIT_COMMAND_VERSION);
    fail_magic_read=0;
    hold=1;assert(frame_store_init()==-1 && diag->stage==FS_INIT_ACK && diag->sequence);
    hold=0;regfile[FS_RESULT/4]=FS_INVALID;
    assert(frame_store_init()==-1 && diag->stage==FS_INIT_RESULT && diag->result==FS_INVALID);
    regfile[FS_RESULT/4]=0;
    assert(frame_store_init()==0);
    assert(diag->stage==FS_INIT_READY && diag->ack==diag->sequence && diag->version==FS_MAGIC);
    assert(frame_store_is_ready());
    regfile[FS_FLAGS/4]=0;assert(!frame_store_is_ready());regfile[FS_FLAGS/4]=1;
    magic=0;assert(!frame_store_is_ready());magic=FS_MAGIC;
    for(unsigned d=0;d<=4;d++) {
        regfile[FS_SLOT/4]=d%3;regfile[FS_FRAME/4]=d+1;
        regfile[FS_BYTES/4]=((6144>>d)+8)*128;regfile[FS_DECIMATION/4]=d;regfile[FS_LEASE/4]=77+d;
        assert(frame_store_acquire(&f,5)==0 && f.lease==77+d);
        assert((UINTPTR)f.data==0x20000000U+(d%3)*0x100000U);
        assert(regfile[FS_WATERMARK/4]==5 && frame_store_acquire(&f,0)==-1);
        assert(frame_store_release(&f)==0 && !f.lease && !f.data);
        assert(regfile[FS_RELEASE_TOKEN/4]==77+d);
    }
    regfile[FS_RESULT/4]=FS_EMPTY;assert(frame_store_acquire(&f,0)==1);
    regfile[FS_RESULT/4]=FS_BUSY;assert(frame_store_acquire(&f,0)==-1);
    regfile[FS_RESULT/4]=0;regfile[FS_BYTES/4]=1;regfile[FS_LEASE/4]=9;
    assert(frame_store_acquire(&f,0)==-1 && !f.lease);
    assert(frame_store_pause()==0 && frame_store_resume()==0);
    const volatile frame_store_pause_diag_t *pd=frame_store_pause_diagnostic();
    assert(pd->reason==FS_DRAIN_OK && pd->ack==pd->sequence);
    regfile[FS_DIAG_VERSION/4]=FS_DIAG_MAGIC_VALUE;regfile[FS_DIAG_AXI/4]=0x2e101;
    regfile[FS_FLAGS/4]=13;regfile[FS_DIAG_INPUT/4]=0x201;
    hold=1;assert(frame_store_pause()==-1);
    assert(pd->reason==FS_DRAIN_TIMEOUT&&pd->sequence!=pd->ack&&pd->axi==0x2e101&&pd->flags==13&&pd->input==0x201);
    assert(frame_store_pause()==-1 && pd->reason==FS_DRAIN_NOT_READY);
    assert(frame_store_acquire(&f,0)==-1 && frame_store_resume()==-1);
    hold=0;assert(frame_store_init()==0);
    magic=0;assert(frame_store_pause()==-1&&pd->reason==FS_DRAIN_ABI&&pd->version==0);magic=FS_MAGIC;
    regfile[FS_RESULT/4]=FS_INVALID;
    assert(frame_store_pause()==-1&&pd->reason==FS_DRAIN_RESULT&&pd->result==FS_INVALID);
    regfile[FS_RESULT/4]=0;assert(frame_store_pause()==0&&pd->reason==FS_DRAIN_OK);
    puts("PASS cold/warm SD1 clock-before-reset, delayed reset, clock/reset/busy failure, no premature PL INIT, ready gate, ABI, five lengths, leases, timeouts/reinit");
    return 0;
}
