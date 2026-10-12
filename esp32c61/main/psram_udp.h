#pragma once
#include "esp_err.h"
#include "raw_transfer.h"
#include "framed_transfer.h"

typedef raw_completion_t psram_udp_completion_cb_t;
typedef struct {
    bool busy;
    bool connected;
    size_t total_bytes;
    size_t sent_bytes;
    unsigned packets;
    unsigned retries;
    int socket_errno;
    uint64_t elapsed_us;
    raw_result_t result;
    char active_ip[16];
    uint16_t active_port;
} psram_udp_status_t;

esp_err_t psram_udp_init(void);
/* Prefer this allocation API. It registers the exact accessible buffer extent. */
void *psram_udp_alloc(size_t length);
esp_err_t psram_udp_free(void *base);
/* External owners may register a LIVE PSRAM allocation they own (max 4 buffers).
 * capacity must be its true accessible size. Register/unregister from task context.
 * The owner must not free or mutate it until completion, and must unregister
 * before freeing. No arbitrary unregistered addresses are accepted by submit. */
esp_err_t psram_udp_register_buffer(void *base, size_t capacity);
esp_err_t psram_udp_unregister_buffer(void *base);
/* Thread-safe, task context only. A non-NULL completion callback is required.
 * Accepts subranges of registered buffers.
 * ESP_OK transfers a read lease until exactly one completion callback.
 * Errors transfer no lease and produce no callback. Callback runs in UDP task,
 * outside locks; it must not block. RAW_OK means local send only, no peer ACK. */
esp_err_t psram_udp_submit(const void *data, size_t length,
                           psram_udp_completion_cb_t completion_cb, void *user_ctx);
esp_err_t psram_udp_submit_frame(const void *data, size_t length,
    const bridge_frame_t *frame, psram_udp_completion_cb_t completion_cb, void *user_ctx);
void psram_udp_stop(void);
void psram_udp_set_link(bool connected);
void psram_udp_get_status(psram_udp_status_t *status);
