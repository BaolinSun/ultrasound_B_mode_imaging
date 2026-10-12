#pragma once
#include <stdint.h>
#include <stddef.h>
/* Independent bitwise IEEE oracle; production links the actual C61 ROM. */
#ifdef CRC_TEST_ORACLE
uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *p,uint32_t n);
#else
static inline uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *p,uint32_t n) {
    crc=~crc;
    while(n--) { crc^=*p++; for(unsigned i=0;i<8;i++) crc=(crc>>1)^((crc&1)?0xedb88320U:0); }
    return ~crc;
}
#endif
