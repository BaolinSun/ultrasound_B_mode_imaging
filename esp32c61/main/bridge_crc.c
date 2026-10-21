#include "bridge_crc.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM) || defined(CRC_TEST_ALLOC)
#include "esp_heap_caps.h"
#endif
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

/* Four-byte slicing, 4 KiB internal SRAM only if faster than ROM. No new
 * frame buffer. Initialize before RX starts; never allocate per block. */
static uint32_t (*table)[256];
#if !defined(ESP_PLATFORM) && !defined(CRC_TEST_ALLOC)
static uint32_t host_table[4][256];
#endif
static bool use_table;
static uint32_t benchmark_rom_us, benchmark_table_us;
bool bridge_crc_using_table(void) { return use_table; }
uint32_t bridge_crc_benchmark_us(bool byte_table) { return byte_table?benchmark_table_us:benchmark_rom_us; }
static inline __attribute__((always_inline)) uint32_t load_le32(const uint8_t *p) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if(((uintptr_t)p&3U)==0) {
        uint32_t word;
        /* Known alignment enables a single RV32 LW without aliasing UB or
         * an unaligned trap; never read beyond the supplied span. */
        memcpy(&word,__builtin_assume_aligned(p,4),sizeof(word));
        return word;
    }
#endif
    /* Do not let GCC fold this fallback back into an unaligned LW. */
    const volatile uint8_t *bytes=p;
    return (uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|
           (uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
}
static inline __attribute__((always_inline)) uint32_t slice_word(uint32_t crc,uint32_t word) {
    crc^=word;
    return table[3][crc&255U]^table[2][(crc>>8)&255U]^
           table[1][(crc>>16)&255U]^table[0][crc>>24];
}
static uint32_t IRAM_ATTR table_crc(uint32_t crc,const uint8_t *p,size_t n) {
    crc=~crc;
    while(n && ((uintptr_t)p&3U)) {
        crc=(crc>>8)^table[0][(crc^*p++)&255U];n--;
    }
    while(n>=4) {
        crc=slice_word(crc,load_le32(__builtin_assume_aligned(p,4)));
        p+=4;n-=4;
    }
    while(n--) crc=(crc>>8)^table[0][(crc^*p++)&255U];
    return ~crc;
}
uint32_t bridge_crc32(uint32_t crc,const uint8_t *p,size_t n) {
    return use_table ? table_crc(crc,p,n) : esp_rom_crc32_le(crc,p,(uint32_t)n);
}
uint32_t IRAM_ATTR bridge_crc32_copy(uint32_t crc,uint8_t *dest,const uint8_t *p,size_t n) {
    if(!use_table) {
        memcpy(dest,p,n);
        return esp_rom_crc32_le(crc,p,(uint32_t)n);
    }
    crc=~crc;
    /* DMA buffers and frame slots are naturally aligned. Check once, not
     * on every word. Unusual external subspans use the exact byte path. */
    if((((uintptr_t)dest|(uintptr_t)p)&3U)==0) {
        while(n>=4) {
            uint32_t word=load_le32(__builtin_assume_aligned(p,4));
            crc=slice_word(crc,word);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            memcpy(__builtin_assume_aligned(dest,4),&word,sizeof(word));
#else
            volatile uint8_t *bytes=dest;
            bytes[0]=(uint8_t)word;bytes[1]=(uint8_t)(word>>8);
            bytes[2]=(uint8_t)(word>>16);bytes[3]=(uint8_t)(word>>24);
#endif
            p+=4;dest+=4;n-=4;
        }
    }
    while(n--) {
        uint8_t byte=*p++;
        crc=(crc>>8)^table[0][(crc^byte)&255U];*dest++=byte;
    }
    return ~crc;
}
bool bridge_crc_init(void) {
#if defined(ESP_PLATFORM) || defined(CRC_TEST_ALLOC)
    free(table);table=heap_caps_malloc(4U*256U*sizeof(uint32_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
#else
    table=host_table;
#endif
    use_table=false;
    benchmark_table_us=0;
    if(table) {
        for(unsigned i=0;i<256;i++) {
            uint32_t c=i;
            for(unsigned j=0;j<8;j++) c=(c>>1)^((c&1U)?0xedb88320U:0);
            table[0][i]=c;
        }
        for(unsigned i=0;i<256;i++) {
            uint32_t c=table[0][i];
            for(unsigned slice=1;slice<4;slice++) {
                c=(c>>8)^table[0][c&255U];table[slice][i]=c;
            }
        }
    }
    uint8_t sample[512];
    for(unsigned i=0;i<sizeof(sample);i++) sample[i]=(uint8_t)i;
    uint32_t reference=0,rom=0,fast=0;
    for(unsigned i=0;i<64;i++) reference=br_crc32(reference,sample,sizeof(sample));
    int64_t start=esp_timer_get_time();
    for(unsigned i=0;i<64;i++) rom=esp_rom_crc32_le(rom,sample,sizeof(sample));
    int64_t rom_us=esp_timer_get_time()-start;
    start=esp_timer_get_time();
    if(table)for(unsigned i=0;i<64;i++) fast=table_crc(fast,sample,sizeof(sample));
    int64_t table_us=esp_timer_get_time()-start;
    bool valid=rom==reference && (!table || fast==reference);
    use_table=valid && table && table_us<=rom_us;
    /* Boot-time check of the selected copy kernel before any DMA descriptor
     * is accepted. Stack scratch only, no persistent/third frame allocation. */
    if(valid) {
        uint8_t copied[sizeof(sample)+4];
        for(unsigned off=0;off<4;off++) {
            size_t n=sizeof(sample)-off;
            memset(copied,0x5a,sizeof(copied));
            uint32_t actual=bridge_crc32_copy(42,copied+off,sample+off,n);
            if(actual!=br_crc32(42,sample+off,n) || memcmp(copied+off,sample+off,n) ||
               copied[off+n]!=0x5a)valid=false;
        }
        if(!valid)use_table=false;
    }
    benchmark_rom_us=(uint32_t)rom_us;
    benchmark_table_us=table?(uint32_t)table_us:0;
#if defined(ESP_PLATFORM) || defined(CRC_TEST_ALLOC)
    if(!use_table) { free(table);table=NULL; }
#endif
    ESP_LOGI("sdio_crc","32 KiB CRC benchmark ROM=%d us SRAM-slice4=%d us selected=%s",
        (int)rom_us,(int)table_us,use_table?"SRAM-table":"ROM");
    return valid;
}
