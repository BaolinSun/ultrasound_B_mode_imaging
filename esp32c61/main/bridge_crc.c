#include "bridge_crc.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
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
static uint32_t IRAM_ATTR table_crc(uint32_t crc,const uint8_t *p,size_t n) {
    crc=~crc;
    while(n>=4) {
        /* Explicit byte loads also support unaligned DMA/subspan pointers. */
        crc^=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        crc=table[3][crc&255U]^table[2][(crc>>8)&255U]^
            table[1][(crc>>16)&255U]^table[0][crc>>24];
        p+=4;n-=4;
    }
    while(n--) crc=(crc>>8)^table[0][(crc^*p++)&255U];
    return ~crc;
}
uint32_t bridge_crc32(uint32_t crc,const uint8_t *p,size_t n) {
    return use_table ? table_crc(crc,p,n) : esp_rom_crc32_le(crc,p,(uint32_t)n);
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
    benchmark_rom_us=(uint32_t)rom_us;
    benchmark_table_us=table?(uint32_t)table_us:0;
#if defined(ESP_PLATFORM) || defined(CRC_TEST_ALLOC)
    if(!use_table) { free(table);table=NULL; }
#endif
    ESP_LOGI("sdio_crc","32 KiB CRC benchmark ROM=%d us SRAM-slice4=%d us selected=%s",
        (int)rom_us,(int)table_us,use_table?"SRAM-table":"ROM");
    return valid;
}
