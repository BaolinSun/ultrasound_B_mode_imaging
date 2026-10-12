#include "tcp_transfer.h"
#include <string.h>
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
static raw_result_t exact(const tcp_io_t*io,uint8_t*p,size_t n,bool reading,bool payload,
                          uint64_t deadline,tcp_metrics_t*m) {
    while(n) {
        raw_result_t r=check(io,deadline);
        if(r!=RAW_OK) {
            if(r==RAW_TIMEOUT)m->error=reading?TCP_ERROR_ACK_TIMEOUT:TCP_ERROR_SEND_TIMEOUT;
            return r;
        }
        int got=reading?io->read(io->ctx,p,n):io->write(io->ctx,p,n);
        if(got==-2) { m->waits++;io->wait(io->ctx,reading,100);continue; }
        if(got<=0 || (size_t)got>n) {
            m->error=got==0?TCP_ERROR_CLOSED:TCP_ERROR_IO;return RAW_SEND_ERROR;
        }
        p+=got;n-=(size_t)got;
        if(payload)m->sent+=(size_t)got;
    }
    return RAW_OK;
}
raw_result_t tcp_frame_send(const uint8_t*data,size_t length,const bridge_frame_t*frame,
    uint8_t*staging,uint64_t deadline,const tcp_io_t*io,tcp_metrics_t*m) {
    memset(m,0,sizeof(*m));
    if(!data || !staging || !br_meta_valid(frame,frame?frame->session_id:0,1048576) || frame->frame_bytes!=length)
        return RAW_INVALID;
    uint8_t header[TCP_FRAME_HEADER],ack[TCP_FRAME_ACK];tcp_frame_header(header,frame);
    raw_result_t r=exact(io,header,sizeof(header),false,false,deadline,m);
    while(r==RAW_OK && m->sent<length) {
        size_t n=length-m->sent;if(n>TCP_STAGE_BYTES)n=TCP_STAGE_BYTES;
        memcpy(staging,data+m->sent,n);
        r=exact(io,staging,n,false,true,deadline,m);
        if(r==RAW_OK) {
            m->chunks++;
            if(m->chunks%8==0 || m->sent==length)io->progress(io->ctx,m->sent,m->chunks,m->waits);
            if(m->chunks%16==0 && m->sent<length)io->yield_ms(io->ctx,1);
        }
    }
    if(r==RAW_OK) {
        uint64_t start=io->now_ms(io->ctx);
        r=exact(io,ack,sizeof(ack),true,false,deadline,m);
        m->ack_ms=io->now_ms(io->ctx)-start;
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
