/* Execute the actual bare-metal bridge against a strict register/window peer.
 * This checks application sequencing, not the physical SDPS controller. */
#include "sdio_host.h"
#include "xiltimer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../build/src/ps/imaging_config.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif
static u8 regs[52];
static u64 tick;
static unsigned inits,aborts,blocks,windows,complete,commits,flushes,invalidations;
static u8 *image_ddr;
static u8 image_seed;
static int overwrite_during_send;
static void fill_image(u8 seed) {
    image_seed=seed;
    for(u32 i=0;i<BR_FRAME_BYTES;i++)image_ddr[i]=(u8)(i*37U+(i>>9)+seed);
}
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
void Xil_DCacheFlushRange(UINTPTR p,u32 n) { (void)p;(void)n;flushes++;assert(0); }
void Xil_DCacheInvalidateRange(UINTPTR p,u32 n) {
    assert(p==DDR_RING_BASE_ADDR && n==BR_FRAME_BYTES);invalidations++;
}
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
        assert(get32(BR_CRC)==br_crc32(0,image_ddr,BR_FRAME_BYTES));
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
    assert(p==image_ddr+received); /* The actual physical image address, not a copy. */
    if(overwrite_during_send && !received)image_ddr[0]^=1U;
    for(u32 i=0;i<count*512;i++) {
        u32 pos=received+i;
        if(!overwrite_during_send || pos!=0)
            assert(p[i]==(u8)(pos*37U+(pos>>9)+image_seed));
    }
    crc=br_crc32(crc,p,count*512);received+=count*512;blocks+=count;windows++;
    if(received==BR_FRAME_BYTES) {
        assert(count==2);
        if(overwrite_during_send)assert(crc!=get32(BR_CRC));
        else assert(crc==get32(BR_CRC));
        complete++;regs[BR_RESULT]=crc==get32(BR_CRC)?BR_DONE:BR_BAD_CRC;
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
    sdio_bridge_init();sdio_bridge_poll();tick+=501;sdio_bridge_poll();
    assert(regs[BR_HOST_MODE]==1);
}
static void stream(void) { regs[BR_STATUS]=BR_READY|BR_STREAM; }
int main(void) {
#if DATA_OUTPUT_MODE == DATA_OUTPUT_USB
    /* Even repeated polls must not enumerate or allocate an SDIO test frame. */
    (void)connect;(void)stream;(void)fill_image;
    sdio_bridge_init();assert(!sdio_bridge_output_is_sdio());
    for(unsigned i=0;i<100;i++) { tick+=501;sdio_bridge_poll(); }
    assert(inits==0 && blocks==0 && commits==0 && aborts==0 && flushes==0);
    puts("PASS: compile-time USB / no SD1 access / no SDIO transfer");
#else
    /* Map fake RAM at the REAL board address. The production bridge is not
     * compiled with a pointer override or a synthetic-data implementation. */
#ifdef _WIN32
    image_ddr=VirtualAlloc((void *)(UINTPTR)DDR_RING_BASE_ADDR,DDR_RING_BYTES,
                          MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    image_ddr=mmap((void *)(UINTPTR)DDR_RING_BASE_ADDR,DDR_RING_BYTES,
                   PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
#endif
    assert(image_ddr==(void *)(UINTPTR)DDR_RING_BASE_ADDR);
    fill_image(91);
    u32 before_init=br_crc32(0,image_ddr,BR_FRAME_BYTES);
    sdio_bridge_init();assert(sdio_bridge_output_is_sdio());assert(inits==0 && flushes==0 && invalidations==0);
    assert(br_crc32(0,image_ddr,BR_FRAME_BYTES)==before_init); /* No DDR writes on init. */
    connect();unsigned old=commits;
    for(unsigned i=0;i<10;i++)sdio_bridge_poll();assert(commits==old);
    /* Four frames cross the 255 -> 1 window-sequence boundary. */
    stream();
    for(unsigned i=0;i<600 && complete<4;i++) { tick++;sdio_bridge_poll(); }
    assert(complete==4 && blocks==4*(BR_FRAME_BYTES/512) && windows==4*97);
    unsigned prior_invalidations=invalidations;
    u32 old_crc=get32(BR_CRC);
    sdio_bridge_poll(); /* Consume DONE before preparing a different image. */
    fill_image(42);
    unsigned next_complete=complete+1;
    while(complete<next_complete) { tick++;sdio_bridge_poll(); }
    assert(get32(BR_CRC)!=old_crc && invalidations==prior_invalidations+1);
    sdio_bridge_poll();
    /* Reproduces the explicitly deferred PL overwrite problem. CRC failure
     * must still be reported, not changed into an artificial success. */
    overwrite_during_send=1;next_complete=complete+1;
    while(complete<next_complete) { tick++;sdio_bridge_poll(); }
    assert(regs[BR_RESULT]==BR_BAD_CRC);sdio_bridge_poll();
    overwrite_during_send=0;fill_image(91);connect();stream();
    sdio_bridge_poll();regs[BR_STATUS]=BR_READY|BR_STREAM|BR_BACKPRESSURE;
    old=commits;for(unsigned i=0;i<10;i++)sdio_bridge_poll();assert(commits==old);
    regs[BR_HOST_MODE]=0;sdio_bridge_poll(); /* Slave withdraws a stale link. */
    assert(sdio_bridge_output_is_sdio()); /* Cannot change the compiled mode. */
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
    sdio_bridge_init();io_failure=1;sdio_bridge_poll();io_failure=0;
    sdio_bridge_init();sdio_bridge_poll();tick+=501;regs[BR_PROTOCOL]=0;sdio_bridge_poll();
    connect();stream();sdio_bridge_poll();unsigned finished=complete;
    while(complete==finished) { tick++;sdio_bridge_poll(); }
    fail_operation=operation;sdio_bridge_poll();fail_operation=-1;
    /* Inject a failure at each of the first 160 register-access positions.
     * Each host reinitialization aborts abandoned frames before more DMA. */
    for(int n=0;n<160;n++) {
        sdio_bridge_init();operation=0;fail_operation=n;
        for(unsigned i=0;i<300;i++) {
            tick+=10;
            if(regs[BR_HOST_MODE]==1)stream(); /* Simulated explicit user restart. */
            sdio_bridge_poll();
        }
        assert(operation>n);fail_operation=-1;
    }
    assert(flushes==0);
    puts("PASS: live 0x20000000 DDR / no initialization writes / per-frame CRC / overwrite CRC error / metadata / sequence wrap / backpressure / timeout / no DMA replay / reset");
#ifdef _WIN32
    assert(VirtualFree(image_ddr,0,MEM_RELEASE));
#else
    assert(!munmap(image_ddr,DDR_RING_BYTES));
#endif
#endif
    return 0;
}
