#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    bool ready, running, host_sdio, raw_busy, stopping;
    uint32_t reset_count;
    const char *reset_reason; /* static diagnostic string; no allocation */
    uint32_t session, received_frames, crc_errors, errors, backpressure,
             sent_frames, dropped_frames;
    uint64_t sent_bytes;
    uint64_t last_receive_us, last_crc_us;
    /* Inclusive copy+CRC wall time; last_crc_us retained as legacy alias.
     * This overlaps receive time and may include task preemption. */
    uint64_t last_copy_crc_us;
    bool crc_sram_table;
    uint32_t crc_rom_bench_us, crc_table_bench_us;
    unsigned free_slots;
    uint32_t receive_offset, current_frame;
    uint32_t frame_bytes, source_drops, source_errors, source_latest, source_published;
    /* Complete CRC-verified frame only; frame_bytes above is current BEGIN. */
    uint32_t last_received_bytes, last_received_frame;
    uint8_t source_flags, source_down, source_slot, source_occupancy;
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
void sdio_bridge_control_gate(bool blocked);
bool sdio_bridge_control_drained(void);
