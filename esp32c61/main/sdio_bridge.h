#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    bool ready, running, host_sdio, raw_busy;
    uint32_t session, received_frames, crc_errors, errors, backpressure,
             sent_frames, dropped_frames;
    uint64_t sent_bytes;
    unsigned free_slots;
} sdio_bridge_status_t;
esp_err_t sdio_bridge_init(void);
esp_err_t sdio_bridge_start(unsigned frames); /* 0 continuous, 1 single */
void sdio_bridge_stop(void);
void sdio_bridge_get_status(sdio_bridge_status_t *out);
esp_err_t sdio_bridge_selftest(void);
size_t sdio_bridge_selftest_size(void);
