#include "tcp_transfer.h"
#include <string.h>
const char *tcp_phase_name(int phase) {
    static const char *names[]={"idle","connect","header","payload","ack"};
    return phase>=0 && (unsigned)phase<sizeof(names)/sizeof(names[0])?names[phase]:"unknown";
}
const char *tcp_error_name(int error) {
    static const char *names[]={"none","socket_io","peer_closed","invalid_ack","pc_rejected",
        "connect_failed","send_timeout","ack_timeout"};
    return error>=0 && (unsigned)error<sizeof(names)/sizeof(names[0])?names[error]:"unknown";
}
static uint32_t be32(const uint8_t*p) {
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
void tcp_frame_header(uint8_t*p,const bridge_frame_t*m) {
    br_put_be32(p,TCP_FRAME_MAGIC);p[4]=1;p[5]=TCP_FRAME_HEADER;br_put_be16(p+6,0);
    br_put_be32(p+8,m->session_id);br_put_be32(p+12,m->frame_id);
    br_put_be32(p+16,m->frame_bytes);br_put_be32(p+20,m->crc32);
}
static raw_result_t check(const tcp_io_t*io,uint64_t deadline) {
    raw_result_t r=io->abort_reason(io->ctx);
    return r!=RAW_OK?r:io->now_ms(io->ctx)>=deadline?RAW_TIMEOUT:RAW_OK;
}
static raw_result_t exact(const tcp_io_t*io,uint8_t*read_dest,const uint8_t*write_src,size_t n,bool payload,size_t prefix,
                          uint64_t deadline,tcp_metrics_t*m,size_t *since_yield) {
    bool reading=read_dest!=NULL;
    while(n) {
        raw_result_t r=check(io,deadline);
        if(r!=RAW_OK) {
            if(r==RAW_TIMEOUT)m->error=reading?TCP_ERROR_ACK_TIMEOUT:TCP_ERROR_SEND_TIMEOUT;
            return r;
        }
        if(reading)m->read_calls++;else m->write_calls++;
        int got=reading?io->read(io->ctx,read_dest,n):io->write(io->ctx,write_src,n);
        if(got==-2 || got==-3) {
            m->waits++;
            if(reading)m->ack_waits++;else m->send_waits++;
            if(got==-3)m->resource_waits++;
            uint64_t start=io->now_us(io->ctx);
            io->wait(io->ctx,reading,got==-3,100);
            uint64_t spent=io->now_us(io->ctx)-start;
            /* An actual scheduler/socket wait already gave other tasks CPU.
             * Do not add a second fairness sleep for bytes preceding it.
             * Immediate readiness/spurious wakeups get no credit. */
            if(!reading && spent>=1000) {
                *since_yield=0;m->cooperative_waits++;
            }
            if(reading)m->read_wait_us+=spent;else m->write_wait_us+=spent;
            if(got==-3)m->yield_us+=spent;
            continue;
        }
        if(got<=0 || (size_t)got>n) {
            m->error=got==0?TCP_ERROR_CLOSED:TCP_ERROR_IO;return RAW_SEND_ERROR;
        }
        if(!reading && (size_t)got<n)m->partial_writes++;
        if(reading)read_dest+=got;else write_src+=got;
        n-=(size_t)got;
        if(payload) {
            size_t skipped=(size_t)got<prefix?(size_t)got:prefix;
            prefix-=skipped;m->sent+=(size_t)got-skipped;
        }
    }
    return RAW_OK;
}
raw_result_t tcp_frame_send(const uint8_t*data,size_t length,const bridge_frame_t*frame,
    uint8_t*staging,uint64_t deadline,const tcp_io_t*io,tcp_metrics_t*m) {
    memset(m,0,sizeof(*m));
    if(!data || !staging || !br_meta_valid(frame,frame?frame->session_id:0,1048576) || frame->frame_bytes!=length)
        return RAW_INVALID;
    uint8_t ack[TCP_FRAME_ACK];
    io->phase(io->ctx,TCP_PHASE_HEADER);
    uint64_t start=io->now_us(io->ctx);
    tcp_frame_header(staging,frame);
    size_t first=length;
    if(first>TCP_FIRST_WRITE_BYTES-TCP_FRAME_HEADER)first=TCP_FIRST_WRITE_BYTES-TCP_FRAME_HEADER;
    uint64_t copy_start=io->now_us(io->ctx);
    memcpy(staging+TCP_FRAME_HEADER,data,first);
    m->staged_bytes+=first;
    m->copy_us+=io->now_us(io->ctx)-copy_start;
    /* One stream write starts with header+payload. Partial writes crossing
     * the prefix must count only payload bytes, including error paths. */
    size_t since_yield=0;
    raw_result_t r=exact(io,NULL,staging,TCP_FRAME_HEADER+first,true,TCP_FRAME_HEADER,deadline,m,&since_yield);
    m->header_us=io->now_us(io->ctx)-start;
    since_yield=r==RAW_OK?first:0;
    if(r==RAW_OK) { m->chunks++;io->progress(io->ctx,m->sent,m->chunks,m->waits); }
    start=io->now_us(io->ctx);
    if(r==RAW_OK)io->phase(io->ctx,TCP_PHASE_PAYLOAD);
    while(r==RAW_OK && m->sent<length) {
        size_t n=length-m->sent;if(n>TCP_STAGE_BYTES)n=TCP_STAGE_BYTES;
        const uint8_t *source=data+m->sent;
        /* IDF 6.1 lwip_send() uses NETCONN_COPY: its accepted prefix is copied
         * synchronously into lwIP storage. No raw tcp_write(..., flags=0), no
         * Wi-Fi DMA from PSRAM. The full frame lease still lasts through ACK. */
#if !TCP_DIRECT_PSRAM
        copy_start=io->now_us(io->ctx);
        memcpy(staging,source,n);source=staging;
        m->staged_bytes+=n;
        m->copy_us+=io->now_us(io->ctx)-copy_start;
#endif
        size_t previously_sent=m->sent;
        r=exact(io,NULL,source,n,true,0,deadline,m,&since_yield);
        if(TCP_DIRECT_PSRAM)m->direct_bytes+=m->sent-previously_sent;
        if(r==RAW_OK) {
            m->chunks++;
            if(m->chunks%8==0 || m->sent==length)io->progress(io->ctx,m->sent,m->chunks,m->waits);
            since_yield+=n;
            if(since_yield>=TCP_YIELD_BYTES && m->sent<length) {
                uint64_t yield_start=io->now_us(io->ctx);
                io->yield_ms(io->ctx,1);m->yield_us+=io->now_us(io->ctx)-yield_start;
                since_yield=0;
            }
        }
    }
    m->payload_us=io->now_us(io->ctx)-start;
    if(r==RAW_OK) {
        io->phase(io->ctx,TCP_PHASE_ACK);
        uint64_t ack_start=io->now_us(io->ctx);
        r=exact(io,ack,NULL,sizeof(ack),false,0,deadline,m,&since_yield);
        m->ack_us=io->now_us(io->ctx)-ack_start;m->ack_ms=m->ack_us/1000;
        if(r==RAW_OK) {
            if(be32(ack)!=TCP_ACK_MAGIC || ack[4]!=1 || ack[5]!=TCP_FRAME_ACK ||
               be32(ack+8)!=frame->session_id || be32(ack+12)!=frame->frame_id) {
                m->error=TCP_ERROR_BAD_ACK;r=RAW_SEND_ERROR;
            } else if(ack[6] || ack[7]) { m->error=TCP_ERROR_REJECTED;r=RAW_SEND_ERROR; }
            else if(be32(ack+16)!=frame->crc32) { m->error=TCP_ERROR_BAD_ACK;r=RAW_SEND_ERROR; }
        }
    }
    io->progress(io->ctx,m->sent,m->chunks,m->waits);
    if(r==RAW_OK) {
        r=check(io,deadline);
        if(r==RAW_TIMEOUT)m->error=TCP_ERROR_ACK_TIMEOUT;
    }
    return r;
}
