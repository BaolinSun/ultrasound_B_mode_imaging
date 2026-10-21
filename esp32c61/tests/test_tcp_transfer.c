#include "tcp_transfer.h"
#include "wifi_transport_config.h"
#include "tcp_memory_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t data[787456],wire[787456+24],stage[TCP_STAGE_BYTES];
typedef struct {
    uint64_t clock,final_clock; size_t written,read; uint8_t ack[20];
    unsigned part,blocked,read_blocked,resource_blocked,yields,progress,phases;
    size_t heap_available;
    unsigned adaptive;
    tcp_phase_t phase;
    size_t fail_at; int failure; raw_result_t abort;
} fixture_t;
static uint64_t now(void*p) { return ((fixture_t*)p)->clock; }
static uint64_t now_us(void*p) { return ((fixture_t*)p)->clock*1000; }
static void phase_io(void*p,tcp_phase_t phase) { fixture_t*f=p;f->phase=phase;f->phases++; }
static raw_result_t aborting(void*p) { return ((fixture_t*)p)->abort; }
static int write_bytes(void*p,const uint8_t*b,size_t n) {
    fixture_t*f=p;
    if(f->blocked) { f->blocked--;return -2; }
    if(f->resource_blocked) { f->resource_blocked--;return -3; }
    if(f->written>=f->fail_at)return f->failure;
    if(n>f->part)n=f->part;
    if(f->heap_available) {
        size_t limited=tcp_memory_write_limit(f->heap_available,n,16384,1440);
        if(!limited)return -3;
        assert(tcp_memory_headroom(f->heap_available,limited,16384));
        if(limited<n)f->adaptive++;
        n=limited;
    }
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
static void wait_io(void*p,bool reading,bool resource,unsigned ms) {
    fixture_t*f=p;assert(ms<=100);f->clock++;
    if(resource)assert(!reading);
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
    tcp_io_t io={.ctx=f,.now_ms=now,.now_us=now_us,.abort_reason=aborting,
        .write=write_bytes,.read=read_bytes,.wait=wait_io,.yield_ms=yield_io,.progress=progress_io,.phase=phase_io};
    return tcp_frame_send(data,meta.frame_bytes,&meta,stage,deadline,&io,m);
}
int main(void) {
    assert(!tcp_memory_headroom(16383,1,16384));
    assert(!tcp_memory_headroom(16384+2*5760-1,5760,16384));
    assert(tcp_memory_headroom(16384+2*5760,5760,16384));
    assert(tcp_memory_headroom(SIZE_MAX,1,16384));
    assert(!tcp_memory_headroom(SIZE_MAX,SIZE_MAX,0));
    assert(tcp_memory_headroom(0,0,0));
    assert(!tcp_memory_write_limit(16384,5760,16384,1440));
    assert(!tcp_memory_write_limit(16384+2879,5760,16384,1440));
    assert(tcp_memory_write_limit(16384+2880,5760,16384,1440)==1440);
    assert(tcp_memory_write_limit(16384+6000,5760,16384,1440)==2880);
    assert(tcp_memory_write_limit(16384+11520,5760,16384,1440)==5760);
    assert(tcp_memory_write_limit(16384+200,100,16384,1440)==100);
    assert(!tcp_memory_write_limit(16384+198,100,16384,1440));
    assert(!tcp_memory_write_limit(SIZE_MAX,5760,0,0));
    assert(!tcp_memory_write_limit(SIZE_MAX,0,0,1440));
    for(size_t available=0;available<40000;available+=71) {
        for(size_t requested=1;requested<=16384;requested+=137) {
            size_t limited=tcp_memory_write_limit(available,requested,16384,1440);
            assert(limited<=requested);
            if(limited) {
                assert(tcp_memory_headroom(available,limited,16384));
                assert(limited==requested || (limited>=1440 && limited%1440==0));
            }
        }
    }
    raw_pattern_fill(data,sizeof(data));assert(br_crc32(0,data,sizeof(data))==meta.crc32);
    tcp_metrics_t m;fixture_t f=fresh();f.blocked=150;f.read_blocked=150;
    assert(run(&f,&m,10000)==RAW_OK);assert(m.waits==300 && m.sent==sizeof(data));
    unsigned chunks=(sizeof(data)+TCP_FRAME_HEADER+TCP_STAGE_BYTES-1)/TCP_STAGE_BYTES;
    unsigned yield_chunks=(TCP_YIELD_BYTES+TCP_STAGE_BYTES-1)/TCP_STAGE_BYTES;
    unsigned first_yield=(TCP_YIELD_BYTES+TCP_FRAME_HEADER+TCP_STAGE_BYTES-1)/TCP_STAGE_BYTES;
    unsigned yields=chunks>first_yield?1+(chunks-1-first_yield)/yield_chunks:0;
    assert(f.read==20 && f.written==sizeof(data)+24 && m.chunks==chunks && f.yields==yields);
    assert(f.phases==3 && f.phase==TCP_PHASE_ACK && m.send_waits==150 && m.ack_waits==150);
    assert(m.header_us==150000 && m.write_wait_us==150000 && m.read_wait_us==150000);
    assert(m.yield_us==f.yields*1000 && m.partial_writes>0 && m.resource_waits==0);
    assert(!memcmp(wire+24,data,sizeof(data)) && !memcmp(wire,"UBST\1\30\0\0",8));
    uint8_t header[24];tcp_frame_header(header,&meta);assert(!memcmp(header,wire,24));
    assert(m.ack_ms==150 && m.error==TCP_ERROR_NONE && f.progress>1);
    /* Full stages cannot fit the old gate; bounded MSS writes still progress.
     * Production exact() must retain prefix/tail accounting and one final ACK. */
    f=fresh();f.part=TCP_STAGE_BYTES;f.heap_available=16384+2*1440;
    assert(run(&f,&m,10000)==RAW_OK && f.adaptive && m.partial_writes);
    assert(!memcmp(wire+24,data,sizeof(data)) && f.read==20 && m.sent==sizeof(data));
    f=fresh();f.part=TCP_STAGE_BYTES;f.heap_available=16384+2*1440-1;
    assert(run(&f,&m,30)==RAW_TIMEOUT && !m.sent && !f.written && m.resource_waits);
    for(unsigned n=1;n<=TCP_STAGE_BYTES*2+1;n++) {
        if(n>4 && n!=TCP_STAGE_BYTES-TCP_FRAME_HEADER && n!=TCP_STAGE_BYTES-TCP_FRAME_HEADER+1 &&
           n!=TCP_STAGE_BYTES-1 && n!=TCP_STAGE_BYTES && n!=TCP_STAGE_BYTES+1 && n!=TCP_STAGE_BYTES*2+1)continue;
        meta.frame_bytes=n;f=fresh();f.part=TCP_STAGE_BYTES;
        assert(run(&f,&m,10000)==RAW_OK && m.sent==n && m.chunks==(n+TCP_FRAME_HEADER+TCP_STAGE_BYTES-1)/TCP_STAGE_BYTES);
        assert(!memcmp(wire+24,data,n));
    }
    meta.frame_bytes=sizeof(data);
    for(unsigned pos=0;pos<40;pos++) {
        f=fresh();f.part=1;f.fail_at=pos;f.failure=-1;
        assert(run(&f,&m,10000)==RAW_SEND_ERROR);
        assert(m.sent==(pos>TCP_FRAME_HEADER?pos-TCP_FRAME_HEADER:0));
    }
    f=fresh();f.resource_blocked=120;
    assert(run(&f,&m,10000)==RAW_OK && m.resource_waits==120 && m.send_waits==120);
    assert(m.yield_us==120000+f.yields*1000);
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
    tcp_io_t io={.ctx=&f,.now_ms=now,.now_us=now_us,.abort_reason=aborting,
        .write=write_bytes,.read=read_bytes,.wait=wait_io,.yield_ms=yield_io,.progress=progress_io,.phase=phase_io};
    assert(tcp_frame_send(NULL,sizeof(data),&meta,stage,10000,&io,&m)==RAW_INVALID);
    assert(tcp_frame_send(data,sizeof(data),NULL,stage,10000,&io,&m)==RAW_INVALID);
    assert(tcp_frame_send(data,sizeof(data),&meta,NULL,10000,&io,&m)==RAW_INVALID);
    assert(tcp_frame_send(data,1,&meta,stage,10000,&io,&m)==RAW_INVALID);
    assert(!strcmp(tcp_error_name(-1),"unknown") && !strcmp(tcp_error_name(TCP_ERROR_ACK_TIMEOUT),"ack_timeout"));
    assert(!strcmp(tcp_phase_name(TCP_PHASE_PAYLOAD),"payload") && !strcmp(tcp_phase_name(-1),"unknown"));
    puts("PASS: TCP exact IO/staging tails, resource vs readiness wait, phase metrics, fairness, ACK/stop/link/timeout");
}
