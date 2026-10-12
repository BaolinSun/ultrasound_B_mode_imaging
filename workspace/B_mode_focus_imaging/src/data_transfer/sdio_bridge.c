#include "sdio_bridge.h"
#include "xil_printf.h"

int sdio_bridge_output_is_sdio(void) { return DATA_OUTPUT_MODE == DATA_OUTPUT_SDIO; }

#if DATA_OUTPUT_MODE == DATA_OUTPUT_SDIO
#include "sdio_host.h"
#include "../imaging_config.h"
#include "xil_cache.h"
#include "xiltimer.h"
#include <string.h>

_Static_assert(BF_FRAME_BYTES==BR_FRAME_BYTES,"Update both protocol and imaging frame size");
/* Linker-owned DDR storage, deliberately NOT the PL's 0x20000000 window. */
static unsigned char test_frame[BR_FRAME_BYTES] __attribute__((aligned(64)));
static sdio_host_t host;
static int online, phase;
static int reset_on_connect=1;
static u8 command_seq;
static u32 session_id, frame_id, frame_crc, offset, failures, frames;
static u64 deadline, retry_at;
static u64 last_progress, next_diagnostic;
static u32 progress_offset, progress_frames;
enum { IDLE, BEGIN_WAIT, WINDOW, DONE_WAIT };
static u64 milliseconds(void) { XTime n; XTime_GetTime(&n); return (u64)n*1000U/(u64)(COUNTS_PER_SECOND); }
static int read8(unsigned p,u8 *v) { return sdio_read_reg(&host,p,v); }
static int write8(unsigned p,u8 v) { return sdio_write_reg(&host,p,v); }
static int read32(unsigned p,u32 *v) {
    u8 b; *v=0;
    for(unsigned i=0;i<4;i++) { if(read8(p+i,&b)) return -1; *v|=(u32)b<<(8*i); }
    return 0;
}
static int write32(unsigned p,u32 v) {
    for(unsigned i=0;i<4;i++) if(write8(p+i,(u8)(v>>(8*i)))) return -1;
    return 0;
}
static void fail_link(void) {
    failures++; online=0; phase=IDLE; offset=0; reset_on_connect=1; retry_at=milliseconds()+500;
    xil_printf("@SDIO ERROR failures=%u; re-enumerate, no payload replay\r\n",(unsigned)failures);
}
void sdio_bridge_init(void) {
    /* Boot/reinitialization starts a fresh handshake; never replay payload. */
    online=0; phase=IDLE; offset=0; reset_on_connect=1; retry_at=milliseconds();
    for(u32 i=0;i<BR_FRAME_BYTES;i++) {
        u32 word=(i/4U)^0xA5C30000U;
        test_frame[i]=(u8)(word>>(8U*(i%4U)));
    }
    frame_crc=br_crc32(0,test_frame,sizeof(test_frame));
    Xil_DCacheFlushRange((UINTPTR)test_frame,sizeof(test_frame));
    xil_printf("@SDIO DDR_TEST addr=%08x bytes=%u crc32=%08x compiled=SDIO\r\n",
        (unsigned)(UINTPTR)test_frame,(unsigned)sizeof(test_frame),(unsigned)frame_crc);
}
static void poll_once(void) {
    u64 now=milliseconds(); u8 state,mode,version,ack,result;
    if(!online) {
        if(now<retry_at) return;
        int init_result=sdio_host_init(&host);
        if(init_result!=SDIO_HOST_OK) {
            xil_printf("@SDIO INIT_FAILED rc=%d; retry after 500 ms\r\n",init_result);
            fail_link(); return;
        }
        if(reset_on_connect) {
            /* A host restart can leave the slave halfway through an old frame.
             * Abort once, then enumerate after the slave resets. */
            if(read8(BR_CMD_ACK,&command_seq) || write8(BR_CMD,BR_ABORT) ||
               write8(BR_CMD_SEQ,br_next_seq(command_seq))) { fail_link(); return; }
            reset_on_connect=0; retry_at=milliseconds()+500; return;
        }
        if(read8(BR_PROTOCOL,&version) || version!=BR_SDIO_VERSION ||
           read8(BR_CMD_ACK,&command_seq) || write8(BR_HOST_MODE,1)) { fail_link(); return; }
        online=1; phase=IDLE; host.last_grant_seq=0;
        xil_printf("@SDIO ONLINE SD1 4bit; waiting webpage start\r\n");
    }
    if(phase!=IDLE && now>=deadline) { fail_link(); return; }
    if(read8(BR_STATUS,&state)) { fail_link(); return; }
    u8 next_grant=0;
    if(phase==WINDOW && (state&BR_STREAM) && !(state&BR_BACKPRESSURE)) {
        /* No new credit: avoid re-reading four session bytes and metadata
         * on every tight poll. Revalidate host/session before every actual
         * DMA window, so a reset never permits stale payload replay. */
        if(read8(BR_GRANT,&next_grant)) { fail_link(); return; }
        if(!next_grant || next_grant==host.last_grant_seq) return;
    }
    if(read8(BR_HOST_MODE,&mode) || mode!=1) { fail_link(); return; }
    u32 current_session;
    if(read32(BR_SESSION,&current_session)) { fail_link(); return; }
    if(phase!=IDLE && (current_session!=session_id || !(state&BR_STREAM))) {
        /* Single frame closes STREAM after receiving the last block, so DONE
         * still needs acknowledgement even though STREAM has become zero. */
        if(phase!=DONE_WAIT || current_session!=session_id) { phase=IDLE; offset=0; }
    }
    if(phase==IDLE) {
        if((state&(BR_READY|BR_STREAM|BR_BACKPRESSURE))!=(BR_READY|BR_STREAM)) return;
        session_id=current_session; frame_id++; if(!frame_id) frame_id=1;
        if(read8(BR_CMD_ACK,&command_seq)) { fail_link(); return; }
        command_seq=br_next_seq(command_seq);
        if(write32(BR_FRAME_ID,frame_id) || write32(BR_LENGTH,BR_FRAME_BYTES) ||
           write32(BR_CRC,frame_crc) || write32(BR_SESSION_ECHO,session_id) ||
           write8(BR_CMD,BR_BEGIN) || write8(BR_CMD_SEQ,command_seq)) { fail_link(); return; }
        deadline=now+30000; offset=0; phase=BEGIN_WAIT;
        return;
    }
    if(phase==BEGIN_WAIT) {
        if(read8(BR_CMD_ACK,&ack)) { fail_link(); return; }
        if(ack!=command_seq) return;
        if(read8(BR_RESULT,&result)) { fail_link(); return; }
        if(result!=BR_ACCEPTED) { phase=IDLE; return; } /* Busy/tearing: no bytes sent. */
        phase=WINDOW;
    }
    if(phase==WINDOW) {
        u8 sequence,count;
        if(state&BR_BACKPRESSURE) return;
        sequence=next_grant;
        if((!sequence && read8(BR_GRANT,&sequence)) || read8(BR_CREDIT,&count)) { fail_link(); return; }
        if(!sequence || sequence==host.last_grant_seq) return;
        unsigned maximum=br_window(BR_FRAME_BYTES-offset,BR_WINDOW_BLOCKS);
        if(!count || count>maximum) { fail_link(); return; }
        if(sdio_host_send_blocks(&host,test_frame+offset,count)!=SDIO_HOST_OK ||
           sdio_host_write_window_ack(&host,sequence)!=SDIO_HOST_OK) { fail_link(); return; }
        offset+=(u32)count*BR_BLOCK_BYTES;
        if(offset==BR_FRAME_BYTES) phase=DONE_WAIT;
        return;
    }
    if(phase==DONE_WAIT) {
        if(read8(BR_RESULT,&result)) { fail_link(); return; }
        if(result==BR_DONE) {
            frames++; phase=IDLE;
            xil_printf("@SDIO FRAME id=%u bytes=%u elapsed_ms=%u (ESP receive, not PC ACK)\r\n",
                (unsigned)frame_id,(unsigned)offset,(unsigned)(now-(deadline-30000)));
        } else if(result==BR_BAD_CRC || result==BR_FAULT) fail_link();
    }
}
void sdio_bridge_poll(void) {
    poll_once();
    u64 now=milliseconds();
    if(offset!=progress_offset || frames!=progress_frames) {
        progress_offset=offset;progress_frames=frames;last_progress=now;
    }
    if(!online || now-last_progress<2000 || now<next_diagnostic) return;
    next_diagnostic=now+5000;
    u8 state=0,ack=0,result=0,credit=0,grant=0,window_ack=0;
    if(read8(BR_STATUS,&state) || !(state&BR_STREAM))return;
    /* Diagnostic reads never alter control state, resend data, or reset a
     * session. Sequential register samples are not an atomic snapshot. */
    if(read8(BR_CMD_ACK,&ack) || read8(BR_RESULT,&result) || read8(BR_CREDIT,&credit) ||
       read8(BR_GRANT,&grant) || read8(BR_WINDOW_ACK,&window_ack))return;
    xil_printf("@SDIO WAIT phase=%u frame=%u offset=%u session=%08x state=%02x cmd=%u ack=%u result=%u grant=%u last=%u win_ack=%u credit=%u idle_ms=%u\r\n",
        (unsigned)phase,(unsigned)frame_id,(unsigned)offset,(unsigned)session_id,
        (unsigned)state,(unsigned)command_seq,(unsigned)ack,(unsigned)result,
        (unsigned)grant,(unsigned)host.last_grant_seq,(unsigned)window_ack,(unsigned)credit,
        (unsigned)(now-last_progress));
}
#else
/* USB build: no SDIO DDR frame allocation or SD1 register/DMA access. */
void sdio_bridge_init(void) { xil_printf("@OUTPUT compiled=USB\r\n"); }
void sdio_bridge_poll(void) { }
#endif
