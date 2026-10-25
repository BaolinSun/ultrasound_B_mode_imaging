#include "frame_store.h"
#include "../imaging_config.h"
#include "xil_io.h"
#include "xiltimer.h"
#if defined(__arm__)
#define FRAME_BARRIER() __asm__ volatile("dmb sy" ::: "memory")
#else
#define FRAME_BARRIER() __asm__ volatile("" ::: "memory")
#endif
static u32 sequence;
static u32 retained_pause_seq;
static int initialized;
static volatile frame_store_init_diag_t init_diag;
static volatile frame_store_pause_diag_t pause_diag;
static u32 command_reason,command_version;
const volatile frame_store_pause_diag_t *frame_store_pause_diagnostic(void) { return &pause_diag; }
const volatile frame_store_init_diag_t *frame_store_init_diagnostic(void) {
    return &init_diag;
}
static int command(u32 op, u32 timeout_ms) {
    /* Any operation invalidates a prior pause certificate, except a failed
     * no-op read performed by pause_retained(). New PAUSE creates a new one. */
    retained_pause_seq=0;
    u32 version=Xil_In32(FS_BASE+FS_VERSION);
    command_version=version;command_reason=FS_DRAIN_OK;
    if(op==FS_INIT) { init_diag.stage=FS_INIT_COMMAND_VERSION; init_diag.version=version; }
    if(version!=FS_MAGIC) { command_reason=FS_DRAIN_ABI;return -1; }
    u32 seq=++sequence; if(!seq) seq=++sequence;
    if(op==FS_INIT) { init_diag.stage=FS_INIT_ACK; init_diag.sequence=seq; }
    Xil_Out32(FS_BASE+FS_CMD,op);
    FRAME_BARRIER();
    Xil_Out32(FS_BASE+FS_SEQ,seq);
    XTime start,now; XTime_GetTime(&start);
    for(unsigned polls=0;polls<10000000U;polls++) {
        if(Xil_In32(FS_BASE+FS_ACK)==seq) {
            int result=(int)Xil_In32(FS_BASE+FS_RESULT);
            if(result!=FS_OK) command_reason=FS_DRAIN_RESULT;
            if(op==FS_INIT) init_diag.stage=FS_INIT_RESULT;
            return result;
        }
        XTime_GetTime(&now);
        if((now-start)>=((u64)(COUNTS_PER_SECOND)*timeout_ms)/1000U) break;
    }
    command_reason=FS_DRAIN_TIMEOUT;initialized=0; return -1;
}
/* CPU-only reset/JTAG reload may leave SD1 ADMA reading an old lease.
 * Stop the LOCAL controller before INIT can reclaim a previous CPU's slot.
 * No card enumeration or ESP32 dependency; SD1 base is fixed by the board. */
static int stop_inherited_dma(void) {
    const UINTPTR sd1=0xE0101000U;
    int rc=-1;
    init_diag.sd_clock_before=Xil_In32(sd1+0x2cU);
    init_diag.sd_present_before=Xil_In32(sd1+0x24U);
    XTime start,now; XTime_GetTime(&start);
    /* CMD/DAT reset needs a running internal controller clock on cold boot.
     * Clock Control is a 16-bit register (AMD XSdPs_EnableClock); only the
     * reset register at 0x2f uses byte access. Preserve
     * divider/bus-clock settings; do not enable a previously stopped card
     * clock, issue commands, touch SD0 or release any PL slot yet. */
    init_diag.sd_cleanup_phase=FS_SD_CLOCK;
    u16 clock=Xil_In16(sd1+0x2cU);
    if(!(clock&1U)) Xil_Out16(sd1+0x2cU,(u16)(clock|1U));
    /* The time deadline is authoritative. The generous iteration guard is
     * only a fail-safe for a broken timebase, not the former ~5 ms limit. */
    for(unsigned polls=0;polls<10000000U;polls++) {
        if((Xil_In16(sd1+0x2cU)&3U)==3U) goto clock_ready;
        XTime_GetTime(&now);
        if(now-start>=((u64)(COUNTS_PER_SECOND)*20U)/1000U) break;
    }
    goto finished;
clock_ready:
    init_diag.sd_clock_ready=Xil_In32(sd1+0x2cU);
    init_diag.sd_cleanup_phase=FS_SD_RESET;
    Xil_Out8(sd1+0x2fU,6U); /* XSdPs CMD and DAT software reset bits. */
    for(unsigned polls=0;polls<10000000U;polls++) {
        if(!(Xil_In8(sd1+0x2fU)&6U)) {
            init_diag.sd_cleanup_phase=FS_SD_IDLE;
            if(!(Xil_In32(sd1+0x24U)&7U)) {
                FRAME_BARRIER(); rc=0; init_diag.sd_cleanup_phase=FS_SD_DONE; break;
            }
        }
        XTime_GetTime(&now);
        if(now-start>=((u64)(COUNTS_PER_SECOND)*20U)/1000U) break;
    }
finished:
    XTime_GetTime(&now);
    /* BSP expands COUNTS_PER_SECOND as CPU_FREQ/2, without parentheses.
     * /COUNTS_PER_SECOND would divide by CPU_FREQ then by 2: 20 ms -> 5 ms. */
    init_diag.dma_elapsed_ms=(u32)((now-start)*1000U/(u64)(COUNTS_PER_SECOND));
    init_diag.sd_clock_after=Xil_In32(sd1+0x2cU);
    init_diag.sd_present_after=Xil_In32(sd1+0x24U);
    init_diag.sd_reset_after=Xil_In8(sd1+0x2fU);
    return rc;
}
int frame_store_init(void) {
    initialized=0;
    init_diag=(frame_store_init_diag_t){0};
    init_diag.stage=FS_INIT_VERSION;
    init_diag.version=Xil_In32(FS_BASE+FS_VERSION);
    if(init_diag.version!=FS_MAGIC) return -1;
    init_diag.stage=FS_INIT_DMA;
    if(stop_inherited_dma()!=0) return -1;
    sequence=Xil_In32(FS_BASE+FS_ACK);
    int rc=command(FS_INIT,3000);
    init_diag.ack=Xil_In32(FS_BASE+FS_ACK);
    init_diag.result=Xil_In32(FS_BASE+FS_RESULT);
    init_diag.flags=Xil_In32(FS_BASE+FS_FLAGS);
    if(rc!=FS_OK) return -1;
    init_diag.stage=FS_INIT_READY;
    initialized=1; return 0;
}
int frame_store_is_ready(void) {
    return initialized && Xil_In32(FS_BASE+FS_VERSION)==FS_MAGIC &&
        (Xil_In32(FS_BASE+FS_FLAGS)&1U)!=0;
}
int frame_store_acquire(frame_lease_t *f,u32 watermark) {
    if(!initialized || !f || f->lease) return -1;
    Xil_Out32(FS_BASE+FS_WATERMARK,watermark);
    int r=command(FS_ACQUIRE,100);
    if(r==FS_EMPTY) return 1;
    if(r!=FS_OK) return -1;
    f->slot=Xil_In32(FS_BASE+FS_SLOT); f->frame=Xil_In32(FS_BASE+FS_FRAME);
    f->bytes=Xil_In32(FS_BASE+FS_BYTES); f->decimation=Xil_In32(FS_BASE+FS_DECIMATION);
    f->lease=Xil_In32(FS_BASE+FS_LEASE);
    if(f->slot>=3 || !f->frame || !f->lease || f->decimation>4 ||
       f->bytes!=BF_FRAME_BYTES_FOR_DOWN(f->decimation)) {
        (void)frame_store_release(f); return -1;
    }
    f->data=(const u8 *)(UINTPTR)(DDR_FRAME_BASE_ADDR+f->slot*DDR_FRAME_SLOT_BYTES);
    FRAME_BARRIER();
    return 0;
}
int frame_store_release(frame_lease_t *f) {
    if(!f || !f->lease) return 0;
    FRAME_BARRIER();
    Xil_Out32(FS_BASE+FS_RELEASE_TOKEN,f->lease);
    if(command(FS_RELEASE,100)!=FS_OK) return -1;
    f->lease=0; f->data=0; return 0;
}
int frame_store_pause(void) {
    pause_diag=(frame_store_pause_diag_t){0};
    if(!initialized) { pause_diag.reason=FS_DRAIN_NOT_READY;return -1; }
    int rc=command(FS_PAUSE,3000);
    pause_diag.reason=command_reason;pause_diag.version=command_version;pause_diag.sequence=sequence;
    if(command_version==FS_MAGIC) {
        pause_diag.ack=Xil_In32(FS_BASE+FS_ACK);pause_diag.result=Xil_In32(FS_BASE+FS_RESULT);
        pause_diag.flags=Xil_In32(FS_BASE+FS_FLAGS);
        if(Xil_In32(FS_BASE+FS_DIAG_VERSION)==FS_DIAG_MAGIC_VALUE) {
            pause_diag.axi=Xil_In32(FS_BASE+FS_DIAG_AXI);
            pause_diag.input=Xil_In32(FS_BASE+FS_DIAG_INPUT);
            pause_diag.parser=Xil_In32(FS_BASE+FS_DIAG_PARSER);
            pause_diag.samples=Xil_In32(FS_BASE+FS_DIAG_SAMPLES);
            pause_diag.age=Xil_In32(FS_BASE+FS_DIAG_AGE);
        }
    }
    if(rc==FS_OK)retained_pause_seq=sequence;
    return rc==FS_OK ? 0:-1;
}
int frame_store_pause_retained(void) {
    /* Read-only proof from this PS boot, invalidated by every FS command.
     * FS_SEQ/FS_CMD are write-only: their PL read decode returns zero.
     * Use the versioned diagnostic mirror, not the command write aperture.
     * flags initialized=1, active=0, input_busy=0, paused=1. Never issue a
     * new PAUSE/flush here: the AFE/FIFO source clock may already be off. */
    FRAME_BARRIER();
    u32 proof=retained_pause_seq;
    if(!initialized || !proof) return -1;
    if(Xil_In32(FS_BASE+FS_VERSION)!=FS_MAGIC ||
       Xil_In32(FS_BASE+FS_DIAG_VERSION)!=FS_DIAG_MAGIC_VALUE ||
       Xil_In32(FS_BASE+FS_ACK)!=proof ||
       Xil_In32(FS_BASE+FS_DIAG_SEQUENCE)!=proof ||
       Xil_In32(FS_BASE+FS_DIAG_COMMAND)!=FS_PAUSE ||
       Xil_In32(FS_BASE+FS_RESULT)!=FS_OK || (Xil_In32(FS_BASE+FS_FLAGS)&15U)!=9U) return -1;
    FRAME_BARRIER();
    /* Reject a new command/reset observed while taking the multi-register
     * snapshot, including one submitted but not yet acknowledged. */
    return Xil_In32(FS_BASE+FS_DIAG_SEQUENCE)==proof && Xil_In32(FS_BASE+FS_ACK)==proof ? 0:-1;
}
int frame_store_resume(void) { return initialized && command(FS_RESUME,100)==FS_OK ? 0:-1; }
