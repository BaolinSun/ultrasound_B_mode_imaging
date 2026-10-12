#include "bridge_crc.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

/* 1 KiB internal SRAM, initialized only before the RX task starts. Keeping
 * the byte table off flash avoids instruction/data-cache contention with
 * PSRAM copies and Wi-Fi. This is software CRC, not a hardware accelerator. */
static uint32_t table[256];
static bool use_table;
static uint32_t benchmark_rom_us, benchmark_table_us;
bool bridge_crc_using_table(void) { return use_table; }
uint32_t bridge_crc_benchmark_us(bool byte_table) { return byte_table?benchmark_table_us:benchmark_rom_us; }
static uint32_t IRAM_ATTR table_crc(uint32_t crc,const uint8_t *p,size_t n) {
    crc=~crc;
    while(n--) crc=(crc>>8)^table[(crc^*p++)&255U];
    return ~crc;
}
uint32_t bridge_crc32(uint32_t crc,const uint8_t *p,size_t n) {
    return use_table ? table_crc(crc,p,n) : esp_rom_crc32_le(crc,p,(uint32_t)n);
}
bool bridge_crc_init(void) {
    for(unsigned i=0;i<256;i++) {
        uint32_t c=i;
        for(unsigned j=0;j<8;j++) c=(c>>1)^((c&1U)?0xedb88320U:0);
        table[i]=c;
    }
    uint8_t sample[512];
    for(unsigned i=0;i<sizeof(sample);i++) sample[i]=(uint8_t)i;
    uint32_t reference=0,rom=0,fast=0;
    for(unsigned i=0;i<64;i++) reference=br_crc32(reference,sample,sizeof(sample));
    int64_t start=esp_timer_get_time();
    for(unsigned i=0;i<64;i++) rom=esp_rom_crc32_le(rom,sample,sizeof(sample));
    int64_t rom_us=esp_timer_get_time()-start;
    start=esp_timer_get_time();
    for(unsigned i=0;i<64;i++) fast=table_crc(fast,sample,sizeof(sample));
    int64_t table_us=esp_timer_get_time()-start;
    if(rom!=reference || fast!=reference) return false;
    use_table=table_us<=rom_us;
    benchmark_rom_us=(uint32_t)rom_us;
    benchmark_table_us=(uint32_t)table_us;
    ESP_LOGI("sdio_crc","32 KiB CRC benchmark ROM=%d us SRAM-table=%d us selected=%s",
        (int)rom_us,(int)table_us,use_table?"SRAM-table":"ROM");
    return true;
}
