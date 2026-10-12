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
    uint64_t last_receive_us, last_crc_us;
    bool crc_sram_table;
    uint32_t crc_rom_bench_us, crc_table_bench_us;
    unsigned free_slots;
    uint32_t receive_offset, current_frame;
    unsigned window_blocks, window_received;
    uint8_t grant, window_ack, cmd_seq, cmd_ack, command, result, register_status;
    int receiving_slot;
    uint64_t progress_idle_us;
} sdio_bridge_status_t;
esp_err_t sdio_bridge_init(void);
esp_err_t sdio_bridge_start(unsigned frames); /* 0 continuous, 1 single */
void sdio_bridge_stop(void);
void sdio_bridge_get_status(sdio_bridge_status_t *out);
esp_err_t sdio_bridge_selftest(void);
size_t sdio_bridge_selftest_size(void);
