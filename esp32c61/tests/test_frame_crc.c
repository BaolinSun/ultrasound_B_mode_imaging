#include "frame_crc.h"
#include "bridge_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void)
{
    const size_t cap=1048576;
    uint8_t *data=malloc(cap+32), *saved=malloc(cap+32);
    assert(data && saved);
    uint32_t random=0x1234abcdU;
    for(size_t i=0;i<cap+32;i++) {
        random=random*1664525U+1013904223U;data[i]=(uint8_t)(random>>24);
    }
    memcpy(saved,data,cap+32);
    assert(frame_crc32(0,NULL,0)==0);
    assert(frame_crc32(0,"123456789",9)==0xcbf43926U);
    for(size_t offset=0;offset<16;offset++) {
        for(size_t length=0;length<65;length++) {
            assert(frame_crc32(0,data+offset,length)==br_crc32(0,data+offset,length));
            assert(frame_crc32(0xdeadbeefU,data+offset,length)==br_crc32(0xdeadbeefU,data+offset,length));
        }
        for(unsigned down=0;down<5;down++) {
            size_t n=((6144U>>down)+8U)*128U;
            assert(frame_crc32(0,data+offset,n)==br_crc32(0,data+offset,n));
        }
    }
    for(size_t split=0;split<100;split++) {
        uint32_t crc=frame_crc32(0,data,split);
        assert(frame_crc32(crc,data+split,cap-split)==frame_crc32(0,data,cap));
    }
    uint32_t crc=0;
    for(size_t off=0;off<cap;off+=512)crc=frame_crc32(crc,data+off,512);
    assert(crc==br_crc32(0,data,cap));
    assert(!memcmp(data,saved,cap+32));
    /* Host timing is diagnostic only, never a board-throughput claim. */
    volatile uint32_t sink=0;
    clock_t start=clock();
    for(unsigned i=0;i<16;i++)sink^=br_crc32(i,data,197632);
    double old_ms=(double)(clock()-start)*1000.0/CLOCKS_PER_SEC;
    start=clock();
    for(unsigned i=0;i<16;i++)sink^=frame_crc32(i,data,197632);
    double new_ms=(double)(clock()-start)*1000.0/CLOCKS_PER_SEC;
    printf("PASS PS CRC: IEEE/seeds/alignment/tails/five lengths/incremental/read-only; host old=%.2f ms new=%.2f ms (not board), sink=%u\n",
           old_ms,new_ms,(unsigned)sink);
    free(saved);free(data);return 0;
}
