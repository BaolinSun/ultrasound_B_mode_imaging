#include "framed_transfer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct { uint64_t now; unsigned packets,retries,fail; size_t bytes;
    raw_result_t abort; const uint8_t *expected; } mock_t;
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t clock_ms(void *p) { return ((mock_t*)p)->now; }
static raw_result_t abort_check(void *p) { return ((mock_t*)p)->abort; }
static void delay(void *p,unsigned n) { ((mock_t*)p)->now+=n; }
static void progress(void *p,size_t sent,unsigned packets,unsigned retries) {
    mock_t *m=p; assert(sent==m->bytes && packets==m->packets); m->retries=retries;
}
static int send_mock(void *p,const uint8_t *data,size_t n) {
    mock_t *m=p;
    assert(n>32 && n<=1472 && get32(data)==BR_MAGIC && data[4]==1 && data[5]==32);
    assert(get32(data+20)==m->bytes);
    assert(((unsigned)data[26]<<8|data[27])==m->packets);
    if(m->fail) { m->fail--; return 1; }
    assert(memcmp(data+32,m->expected+m->bytes,n-32)==0);
    m->packets++; m->bytes+=n-32; return 0;
}
int main(void) {
    assert(br_crc32(0,(const uint8_t*)"123456789",9)==0xcbf43926);
    uint32_t c=br_crc32(0,(const uint8_t*)"1234",4);
    assert(br_crc32(c,(const uint8_t*)"56789",5)==0xcbf43926);
    assert(br_next_seq(0)==1 && br_next_seq(254)==255 && br_next_seq(255)==1);
    assert(br_window(0,16)==0 && br_window(1,16)==1 && br_window(8192,32)==16 && br_window(1024,16)==2);
    assert(br_window(10000,0)==0);
    uint8_t data[50000],staging[1472]; raw_pattern_fill(data,sizeof(data));
    const size_t lengths[]={1,511,512,513,1439,1440,1441,2880,50000};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
        bridge_frame_t frame={1,2,(uint32_t)lengths[i],br_crc32(0,data,lengths[i])};
        mock_t m={.expected=data,.fail=2}; size_t sent;
        raw_io_t io={&m,clock_ms,abort_check,send_mock,delay,progress};
        assert(psram_framed_send(data,lengths[i],&frame,staging,30000,&io,&sent)==RAW_OK);
        assert(sent==lengths[i] && m.retries==2);
        m.bytes=0; m.packets=0; m.abort=RAW_CANCELLED;
        assert(psram_framed_send(data,lengths[i],&frame,staging,30000,&io,&sent)==RAW_CANCELLED && sent==0);
        m.abort=RAW_OK; m.now=30000;
        assert(psram_framed_send(data,lengths[i],&frame,staging,30000,&io,&sent)==RAW_TIMEOUT);
        frame.session_id=0;
        assert(psram_framed_send(data,lengths[i],&frame,staging,30000,&io,&sent)==RAW_INVALID);
    }
    assert(!br_meta_valid(NULL,1,1));
    bridge_frame_t bad={1,2,4,0}; size_t sent;
    mock_t m={.expected=data}; raw_io_t io={&m,clock_ms,abort_check,send_mock,delay,progress};
    assert(psram_framed_send(data,4,NULL,staging,30000,&io,&sent)==RAW_INVALID);
    assert(psram_framed_send(data,3,&bad,staging,30000,&io,&sent)==RAW_INVALID);
    assert(!br_meta_valid(&bad,2,4));bad.frame_id=0;assert(!br_meta_valid(&bad,1,4));
    bad.frame_id=1;bad.frame_bytes=0;assert(!br_meta_valid(&bad,1,4));
    bad.frame_bytes=5;assert(!br_meta_valid(&bad,1,4));
    puts("PASS: framed wire bytes / tail / retry / cancel / timeout / CRC / sequence");
    return 0;
}
