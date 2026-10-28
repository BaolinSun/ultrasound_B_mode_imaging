/* Link the ACTUAL PS bridge to the actual ESP task in the companion test. */
#define sdio_bridge_init host_bridge_init
#define sdio_bridge_poll host_bridge_poll
#define sdio_bridge_output_is_sdio host_bridge_output_is_sdio
#define sdio_bridge_power_idle host_bridge_power_idle
#define sdio_bridge_idle_pending host_bridge_idle_pending
#define sdio_bridge_source_recovery_begin host_bridge_source_recovery_begin
#include "../../build/src/ps/data_transfer/sdio_bridge.c"
#include <assert.h>
#include <stdarg.h>
#include <string.h>
extern uint8_t peer_read(unsigned);
extern void peer_write(unsigned,uint8_t);
extern void peer_send(const uint8_t *,unsigned);
extern const uint8_t *peer_image(void);
extern uint32_t peer_length(void);
extern uint64_t peer_time_ms(void);
static unsigned lease_live,dma_idle;
static uint32_t image_id;
int afe_power_allowed(void) {return 1;}
int tx_power_allowed(void) {return 1;}
int startup_recovery_blocks(void) {return 0;}
int frame_store_is_ready(void) {return 1;}
int frame_store_acquire(frame_lease_t *f,u32 watermark) {
    (void)watermark;assert(!lease_live && !f->lease);lease_live=1;dma_idle=0;
    f->data=peer_image();f->frame=++image_id;f->bytes=peer_length();f->slot=0;f->lease=image_id;
    for(f->decimation=0;BF_FRAME_BYTES_FOR_DOWN(f->decimation)!=f->bytes;f->decimation++)assert(f->decimation<4);
    return 0;
}
int frame_store_release(frame_lease_t *f) {
    if(!f->lease)return 0;
    assert(lease_live && dma_idle);lease_live=0;f->lease=0;f->data=NULL;return 0;
}
int sdio_host_quiesce(sdio_host_t *h) {(void)h;dma_idle=1;return 0;}
int sdio_host_init(sdio_host_t *h) {memset(h,0,sizeof *h);return 0;}
int sdio_read_reg(sdio_host_t *h,u32 p,u8 *v) {(void)h;*v=peer_read(p);return 0;}
int sdio_write_reg(sdio_host_t *h,u32 p,u8 v) {
    (void)h;if(p==BR_STOP_ACK)assert(!lease_live && dma_idle);
    peer_write(p,v);return 0;
}
int sdio_host_send_blocks(sdio_host_t *h,const u8 *p,u32 count) {(void)h;peer_send(p,count);return 0;}
int sdio_host_write_window_ack(sdio_host_t *h,u8 s) {h->last_grant_seq=s;peer_write(BR_WINDOW_ACK,s);return 0;}
void Xil_DCacheInvalidateRange(UINTPTR p,u32 n) {assert((const u8 *)p==peer_image() && n==peer_length());}
u32 Xil_In32(UINTPTR p) {(void)p;return 0;}
void XTime_GetTime(XTime *t) {*t=peer_time_ms();}
int xil_printf(const char *f,...) {(void)f;return 0;}
unsigned host_has_lease(void) {return lease_live;}
