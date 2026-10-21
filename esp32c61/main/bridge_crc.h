#pragma once
#include "../protocol/bridge_protocol.h"
/* Initialize once before starting SDIO. Compare the internal-SRAM slicing table
 * with the IDF ROM implementation on this chip, and keep the faster path.
 * Both retain reflected IEEE/zlib incremental semantics. */
bool bridge_crc_init(void);
uint32_t bridge_crc32(uint32_t crc,const uint8_t *data,size_t length);
/* Non-overlapping spans, exactly length bytes. On the selected SRAM-table
 * path, consume each DMA word once for both CRC and PSRAM storage. ROM
 * selection/allocation fallback retains memcpy + ROM CRC. */
uint32_t bridge_crc32_copy(uint32_t crc,uint8_t *dest,const uint8_t *data,size_t length);
bool bridge_crc_using_table(void);
uint32_t bridge_crc_benchmark_us(bool byte_table);
