#ifndef SDIO_HOST_H
#define SDIO_HOST_H

#include "sdio_bridge.h"
#define US_SDIO_BLOCK_BYTES BR_BLOCK_BYTES
#include "xsdps.h"

#define SDIO_HOST_OK                  0
#define SDIO_HOST_ERR_LOOKUP         -1
#define SDIO_HOST_ERR_CONFIG         -2
#define SDIO_HOST_ERR_CMD            -3
#define SDIO_HOST_ERR_TIMEOUT        -4
#define SDIO_HOST_ERR_DEVICE_ID      -5
#define SDIO_HOST_ERR_TRANSFER       -6

#define SDIO_HOST_WINDOW_TIMEOUT_MS  5000u
#define SDIO_HOST_MAX_WINDOW_PACKETS 16u
#define SDIO_HOST_MAX_BATCH_BLOCKS   16u

typedef struct {
    XSdPs sd;
    u32 rca;
    u8 function_num;
    u16 block_size;
    u32 data_port_addr;
    u8 last_grant_seq;
} sdio_host_t;

typedef struct {
    u32 cmd52_ms;
    u32 pio_ms;
    u32 dma_pre_idle_ms;
    u32 dma_setup_ms;
    u32 dma_cmd_ms;
    u32 dma_tc_ms;
} sdio_host_timing_t;

int sdio_host_init(sdio_host_t *host);
int sdio_host_wait_window(sdio_host_t *host, u32 timeout_ms, u8 *grant_seq, u8 *grant_packets);
int sdio_host_send_block(sdio_host_t *host, const u8 block[US_SDIO_BLOCK_BYTES]);
int sdio_host_send_blocks(sdio_host_t *host, const u8 *blocks, u32 block_count);
int sdio_host_write_window_ack(sdio_host_t *host, u8 grant_seq);
void sdio_host_print_stats(const char *tag);
void sdio_host_take_timing(sdio_host_timing_t *timing);
u8 sdio_host_read_status(sdio_host_t *host);

int sdio_write_reg(sdio_host_t *host,u32 pos,u8 value);
int sdio_read_reg(sdio_host_t *host,u32 pos,u8 *value);
#endif
