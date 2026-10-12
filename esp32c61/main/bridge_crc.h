#pragma once
#include "../protocol/bridge_protocol.h"
/* Initialize once before starting SDIO. Compare the internal-SRAM slicing table
 * with the IDF ROM implementation on this chip, and keep the faster path.
 * Both retain reflected IEEE/zlib incremental semantics. */
bool bridge_crc_init(void);
uint32_t bridge_crc32(uint32_t crc,const uint8_t *data,size_t length);
bool bridge_crc_using_table(void);
uint32_t bridge_crc_benchmark_us(bool byte_table);
