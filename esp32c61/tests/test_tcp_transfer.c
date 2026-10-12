#include "tcp_transfer.h"
#include "wifi_transport_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t data[787456],wire[787456+24],stage[4096];
typedef struct {
    uint64_t clock,final_clock; size_t written,read; uint8_t ack[20];
    unsigned part,blocked,read_blocked,yields,progress;
    size_t fail_at; int failure; raw_result_t abort;
} fixture_t;
static uint64_t now(void*p) { return ((fixture_t*)p)->clock; }
static raw_result_t aborting(void*p) { return ((fixture_t*)p)->abort; }
static int write_bytes(void*p,const uint8_t*b,size_t n) {
    fixture_t*f=p;
    if(f->blocked) { f->blocked--;return -2; }
    if(f->written>=f->fail_at)return f->failure;
    if(n>f->part)n=f->part;
    assert(f->written+n<=sizeof(wire));memcpy(wire+f->written,b,n);f->written+=n;
    return (int)n;
}
static int read_bytes(void*p,uint8_t*b,size_t n) {
    fixture_t*f=p;
    if(f->read_blocked) { f->read_blocked--;return -2; }
    if(f->written>=f->fail_at)return f->failure;
    if(n>f->part)n=f->part;
    assert(f->read+n<=20);memcpy(b,f->ack+f->read,n);f->read+=n;
    if(f->read==20 && f->final_clock)f->clock=f->final_clock;
    return (int)n;
}
static void wait_io(void*p,bool reading,unsigned ms) {
    fixture_t*f=p;assert(ms<=100);f->clock++;
    /* Must keep SENDING ownership throughout header/body/ACK waits. */
    if(reading)assert(f->written>=25);
}
static void yield_io(void*p,unsigned ms) { fixture_t*f=p;assert(ms>=1);f->clock+=ms;f->yields++; }
static void progress_io(void*p,size_t sent,unsigned chunks,unsigned waits) {
    fixture_t*f=p;(void)chunks;(void)waits;assert(sent<=sizeof(data));f->progress++;
}
static bridge_frame_t meta={123,456,sizeof(data),0x3aa5aebf};
static fixture_t fresh(void) {
    fixture_t f={.part=7,.fail_at=SIZE_MAX};
    br_put_be32(f.ack,TCP_ACK_MAGIC);f.ack[4]=1;f.ack[5]=20;
    br_put_be32(f.ack+8,meta.session_id);br_put_be32(f.ack+12,meta.frame_id);
    br_put_be32(f.ack+16,meta.crc32);return f;
}
static raw_result_t run(fixture_t*f,tcp_metrics_t*m,uint64_t deadline) {
    tcp_io_t io={f,now,aborting,write_bytes,read_bytes,wait_io,yield_io,progress_io};
    return tcp_frame_send(data,meta.frame_bytes,&meta,stage,deadline,&io,m);
}
int main(void) {
    raw_pattern_fill(data,sizeof(data));assert(br_crc32(0,data,sizeof(data))==meta.crc32);
    tcp_metrics_t m;fixture_t f=fresh();f.blocked=150;f.read_blocked=150;
    assert(run(&f,&m,10000)==RAW_OK);assert(m.waits==300 && m.sent==sizeof(data));
    assert(f.read==20 && f.written==sizeof(data)+24 && m.chunks==193 && f.yields==12);
    assert(!memcmp(wire+24,data,sizeof(data)) && !memcmp(wire,"UBST\1\30\0\0",8));
    uint8_t header[24];tcp_frame_header(header,&meta);assert(!memcmp(header,wire,24));
    assert(m.ack_ms==150 && m.error==TCP_ERROR_NONE && f.progress>1);
    for(unsigned n=1;n<=8193;n++) {
        if(n>4 && n!=4095 && n!=4096 && n!=4097 && n!=8193)continue;
        meta.frame_bytes=n;f=fresh();f.part=4096;
        assert(run(&f,&m,10000)==RAW_OK && m.sent==n && m.chunks==(n+4095)/4096);
        assert(!memcmp(wire+24,data,n));
    }
    meta.frame_bytes=sizeof(data);
    for(unsigned pos=0;pos<20;pos++) {
        f=fresh();f.ack[pos]^=1;
        assert(run(&f,&m,10000)==RAW_SEND_ERROR);
        assert(m.error==((pos==6 || pos==7)?TCP_ERROR_REJECTED:TCP_ERROR_BAD_ACK));
    }
    f=fresh();f.blocked=10000;assert(run(&f,&m,30)==RAW_TIMEOUT && m.error==TCP_ERROR_SEND_TIMEOUT && !m.sent);
    f=fresh();f.read_blocked=10000;assert(run(&f,&m,30)==RAW_TIMEOUT && m.error==TCP_ERROR_ACK_TIMEOUT && m.sent==sizeof(data));
    f=fresh();f.abort=RAW_CANCELLED;assert(run(&f,&m,10000)==RAW_CANCELLED && !f.written);
    f=fresh();f.abort=RAW_LINK_DOWN;assert(run(&f,&m,10000)==RAW_LINK_DOWN && !f.written);
    f=fresh();f.fail_at=5000;f.failure=-1;assert(run(&f,&m,10000)==RAW_SEND_ERROR && m.error==TCP_ERROR_IO && m.sent<sizeof(data));
    f=fresh();f.fail_at=sizeof(data)+24;f.failure=0;assert(run(&f,&m,10000)==RAW_SEND_ERROR && m.error==TCP_ERROR_CLOSED);
    f=fresh();f.fail_at=0;f.failure=1000000;assert(run(&f,&m,10000)==RAW_SEND_ERROR && m.error==TCP_ERROR_IO);
    f=fresh();f.final_clock=10000;assert(run(&f,&m,10000)==RAW_TIMEOUT && m.error==TCP_ERROR_ACK_TIMEOUT);
    f=fresh();meta.frame_bytes=0;assert(run(&f,&m,10000)==RAW_INVALID);meta.frame_bytes=sizeof(data);
    tcp_io_t io={&f,now,aborting,write_bytes,read_bytes,wait_io,yield_io,progress_io};
    assert(tcp_frame_send(NULL,sizeof(data),&meta,stage,10000,&io,&m)==RAW_INVALID);
    assert(tcp_frame_send(data,sizeof(data),NULL,stage,10000,&io,&m)==RAW_INVALID);
    assert(tcp_frame_send(data,sizeof(data),&meta,NULL,10000,&io,&m)==RAW_INVALID);
    assert(tcp_frame_send(data,1,&meta,stage,10000,&io,&m)==RAW_INVALID);
    assert(!strcmp(tcp_error_name(-1),"unknown") && !strcmp(tcp_error_name(TCP_ERROR_ACK_TIMEOUT),"ack_timeout"));
    puts("PASS: TCP exact writes/reads, raw payload, 4KiB tails, >100 waits, ACK validation, stop/link/timeout/error");
}
