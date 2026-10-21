#ifndef UB_FRAME_CRC_H
#define UB_FRAME_CRC_H
#include <stddef.h>
#include <stdint.h>
/* Read-only IEEE CRC32, zlib incremental convention. Caller holds the DDR lease.
 * No cache-policy change, DMA access, allocation, or data mutation. */
uint32_t frame_crc32(uint32_t seed, const void *data, size_t length);
#endif
