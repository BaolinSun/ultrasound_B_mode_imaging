#include "framed_transfer.h"
#include <string.h>
/* Adapt the existing retry/deadline/yield engine; payload counters exclude
 * the wire header. A retry uses the same offset and packet index. */
typedef struct { const raw_io_t *io; const bridge_frame_t *frame; uint8_t *staging; size_t offset; } adapter_t;
static uint64_t clock_ms(void *p) { adapter_t *a=p; return a->io->now_ms(a->io->ctx); }
static raw_result_t abort_job(void *p) { adapter_t *a=p; return a->io->abort_reason(a->io->ctx); }
static void delay(void *p,unsigned ms) { adapter_t *a=p; a->io->delay_ms(a->io->ctx,ms); }
static void progress(void *p,size_t sent,unsigned packets,unsigned retries) {
    adapter_t *a=p; a->offset=sent; a->io->progress(a->io->ctx,sent,packets,retries);
}
static int send_packet(void *p,const uint8_t *data,size_t bytes) {
    adapter_t *a=p; (void)data;
    br_udp_header(a->staging,a->frame,(uint32_t)a->offset,(unsigned)bytes);
    return a->io->send(a->io->ctx,a->staging,bytes+BR_UDP_HEADER);
}
raw_result_t psram_framed_send(const uint8_t *data,size_t length,
    const bridge_frame_t *frame,uint8_t *staging,uint64_t deadline,
    const raw_io_t *io,size_t *sent) {
    *sent=0;
    if(!br_meta_valid(frame,frame ? frame->session_id : 0,1048576) || frame->frame_bytes!=length)
        return RAW_INVALID;
    adapter_t a={io,frame,staging,0};
    raw_io_t wrapped={&a,clock_ms,abort_job,send_packet,delay,progress};
    return psram_raw_send(data,length,staging+BR_UDP_HEADER,deadline,&wrapped,sent);
}
