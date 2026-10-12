#ifndef SDIO_CMD_ENGINE_H
#define SDIO_CMD_ENGINE_H
#include <stdint.h>
/* SDHCI register adapter; no SDK wait helpers or card-detect assumptions. */
typedef struct {
    void *ctx;
    uint32_t (*read32)(void *,unsigned);
    uint16_t (*read16)(void *,unsigned);
    void (*write32)(void *,unsigned,uint32_t);
    void (*write16)(void *,unsigned,uint16_t);
    void (*write8)(void *,unsigned,uint8_t);
    uint64_t (*now_us)(void *);
} sdio_cmd_io_t;
enum { SDIO_CMD_OK=0, SDIO_CMD_ERROR=-3, SDIO_CMD_TIMEOUT=-4 };
static inline uint64_t sdio_ticks_us(uint64_t ticks,uint64_t frequency) {
    return (ticks/frequency)*1000000U+(ticks%frequency)*1000000U/frequency;
}
int sdio_cmd_execute(const sdio_cmd_io_t *io,uint32_t framed_cmd,uint32_t arg,
                     uint16_t blocks,uint16_t transfer_mode,unsigned allow_data,
                     unsigned timeout_us);
#endif
