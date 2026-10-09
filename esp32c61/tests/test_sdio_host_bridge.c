/* Execute the actual bare-metal bridge against a strict register/window peer.
 * This checks application sequencing, not the physical SDPS controller. */
#include "sdio_host.h"
#include "xiltimer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static u8 regs[52];
static u64 tick;
static unsigned inits,aborts,blocks,windows,complete,commits;
static u32 received,crc;
static int io_failure,dma_failure,hold_ack,ack_failure;
static int fail_operation=-1,operation;
static int injected(void) { return operation++==fail_operation; }
static void put32(unsigned p,u32 n) { for(unsigned i=0;i<4;i++)regs[p+i]=(u8)(n>>(8*i)); }
static u32 get32(unsigned p) { u32 n=0;for(unsigned i=0;i<4;i++)n|=(u32)regs[p+i]<<(8*i);return n; }
static void grant(void) {
    regs[BR_CREDIT]=(u8)br_window(BR_FRAME_BYTES-received,16);
    regs[BR_GRANT]=br_next_seq(regs[BR_GRANT]);
}
void XTime_GetTime(XTime *n) { *n=tick; }
void Xil_DCacheFlushRange(UINTPTR p,u32 n) { assert(!(p%64) && n==BR_FRAME_BYTES); }
int xil_printf(const char *fmt,...) { (void)fmt;return 0; }
int sdio_host_init(sdio_host_t *host) {
    inits++;memset(host,0,sizeof(*host));return io_failure?-1:0;
}
int sdio_read_reg(sdio_host_t *host,u32 p,u8 *n) {
    (void)host;assert(p<sizeof(regs));if(io_failure || injected())return -1;*n=regs[p];return 0;
}
int sdio_write_reg(sdio_host_t *host,u32 p,u8 n) {
    (void)host;assert(p<sizeof(regs));if(io_failure || injected())return -1;regs[p]=n;
    if(p==BR_CMD_SEQ && regs[BR_CMD]==BR_ABORT) {
        aborts++;memset(regs,0,sizeof(regs));regs[BR_PROTOCOL]=BR_SDIO_VERSION;
        regs[BR_STATUS]=BR_READY|BR_BACKPRESSURE;put32(BR_SESSION,100+aborts);
    } else if(p==BR_CMD_SEQ && regs[BR_CMD]==BR_BEGIN) {
        assert(get32(BR_SESSION_ECHO)==get32(BR_SESSION));
        assert(get32(BR_LENGTH)==BR_FRAME_BYTES && get32(BR_FRAME_ID)!=0);
        commits++;received=0;crc=0;regs[BR_RESULT]=BR_ACCEPTED;
        if(!hold_ack)regs[BR_CMD_ACK]=n;
        grant();
    }
    return 0;
}
int sdio_host_send_blocks(sdio_host_t *host,const u8 *p,u32 count) {
    (void)host;
    assert(count==regs[BR_CREDIT] && count<=16 && count>0);
    if(dma_failure)return -1;
    for(u32 i=0;i<count*512;i++) {
        u32 pos=received+i,word=(pos/4)^0xA5C30000U;
        assert(p[i]==(u8)(word>>(8*(pos%4))));
    }
    crc=br_crc32(crc,p,count*512);received+=count*512;blocks+=count;windows++;
    if(received==BR_FRAME_BYTES) {
        assert(count==2 && crc==get32(BR_CRC));complete++;regs[BR_RESULT]=BR_DONE;
    }
    return 0;
}
int sdio_host_write_window_ack(sdio_host_t *host,u8 seq) {
    if(ack_failure)return -1;
    assert(seq==regs[BR_GRANT]);host->last_grant_seq=seq;regs[BR_WINDOW_ACK]=seq;
    if(received<BR_FRAME_BYTES)grant();
    return 0;
}
static void connect(void) {
    sdio_bridge_set_output(1);sdio_bridge_poll();tick+=501;sdio_bridge_poll();
    assert(regs[BR_HOST_MODE]==1);
}
static void stream(void) { regs[BR_STATUS]=BR_READY|BR_STREAM; }
int main(void) {
    sdio_bridge_init();assert(!sdio_bridge_output_is_sdio());
    sdio_bridge_poll();assert(inits==0);
    connect();unsigned old=commits;
    for(unsigned i=0;i<10;i++)sdio_bridge_poll();assert(commits==old);
    /* Four frames cross the 255 -> 1 window-sequence boundary. */
    stream();
    for(unsigned i=0;i<600 && complete<4;i++) { tick++;sdio_bridge_poll(); }
    assert(complete==4 && blocks==4*(BR_FRAME_BYTES/512) && windows==4*97);
    sdio_bridge_poll();regs[BR_STATUS]=BR_READY|BR_STREAM|BR_BACKPRESSURE;
    old=commits;for(unsigned i=0;i<10;i++)sdio_bridge_poll();assert(commits==old);
    sdio_bridge_set_output(0);assert(!sdio_bridge_output_is_sdio() && regs[BR_HOST_MODE]==0);
    connect();stream();hold_ack=1;sdio_bridge_poll();
    old=blocks;tick+=30001;sdio_bridge_poll();assert(blocks==old);hold_ack=0;
    connect();stream();sdio_bridge_poll();dma_failure=1;sdio_bridge_poll();
    assert(blocks==old);dma_failure=0;
    /* A failed DMA is never replayed; reconnect aborts the abandoned frame. */
    connect();assert(regs[BR_HOST_MODE]==1 && !(regs[BR_STATUS]&BR_STREAM));
    io_failure=1;sdio_bridge_poll();io_failure=0;
    connect();stream();sdio_bridge_poll();
    /* A reset invalidates the active session; stale payload must not follow. */
    put32(BR_SESSION,get32(BR_SESSION)+1);regs[BR_STATUS]=BR_READY|BR_BACKPRESSURE;
    old=blocks;sdio_bridge_poll();assert(blocks==old);
    connect();stream();hold_ack=1;sdio_bridge_poll();sdio_bridge_poll();hold_ack=0;
    regs[BR_CMD_ACK]=regs[BR_CMD_SEQ];regs[BR_RESULT]=BR_REJECTED;sdio_bridge_poll();
    connect();stream();sdio_bridge_poll();regs[BR_STATUS]|=BR_BACKPRESSURE;sdio_bridge_poll();
    regs[BR_STATUS]&=(u8)~BR_BACKPRESSURE;regs[BR_GRANT]=0;sdio_bridge_poll();
    regs[BR_GRANT]=1;regs[BR_CREDIT]=0;sdio_bridge_poll();
    connect();stream();sdio_bridge_poll();regs[BR_CREDIT]=17;sdio_bridge_poll();
    connect();stream();sdio_bridge_poll();ack_failure=1;sdio_bridge_poll();ack_failure=0;
    for(unsigned err=BR_BAD_CRC;err<=BR_FAULT;err++) {
        connect();stream();sdio_bridge_poll();unsigned finished=complete;
        while(complete==finished) { tick++;sdio_bridge_poll(); }
        regs[BR_STATUS]=BR_READY; /* Single-frame STREAM closes before DONE. */
        regs[BR_RESULT]=BR_ACCEPTED;sdio_bridge_poll();
        regs[BR_RESULT]=(u8)err;sdio_bridge_poll();
    }
    connect();regs[BR_HOST_MODE]=0;sdio_bridge_poll();
    sdio_bridge_set_output(1);io_failure=1;sdio_bridge_poll();io_failure=0;
    sdio_bridge_set_output(1);sdio_bridge_poll();tick+=501;regs[BR_PROTOCOL]=0;sdio_bridge_poll();
    connect();stream();sdio_bridge_poll();unsigned finished=complete;
    while(complete==finished) { tick++;sdio_bridge_poll(); }
    fail_operation=operation;sdio_bridge_poll();fail_operation=-1;
    sdio_bridge_print_status();sdio_bridge_set_output(0);
    /* Inject a failure at each of the first 160 register-access positions.
     * Each new selection must abort any abandoned frame before more DMA. */
    for(int n=0;n<160;n++) {
        sdio_bridge_set_output(1);operation=0;fail_operation=n;
        for(unsigned i=0;i<300;i++) {
            tick+=10;
            if(regs[BR_HOST_MODE]==1)stream(); /* Simulated explicit user restart. */
            sdio_bridge_poll();
        }
        assert(operation>n);fail_operation=-1;sdio_bridge_set_output(0);
    }
    puts("PASS: Zynq fixed DDR / metadata commit / 4 frames / sequence wrap / backpressure / timeout / no DMA replay / reset / output switch");
    return 0;
}
