#pragma once
#include "esp_err.h"
#include "raw_transfer.h"
#include "framed_transfer.h"
#include "wifi_transport_config.h"

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
    bool active_tcp, tcp_connected, tcp_failed;
    uint64_t tcp_connect_us, tcp_ack_us;
    uint32_t tcp_confirmed_frames, tcp_connect_errors, tcp_ack_errors;
    unsigned tcp_chunks, tcp_waits;
    unsigned tcp_send_buffer_actual; /* Last successfully tuned socket, 0 before first connect. */
    unsigned tcp_heap_guard_waits;
    unsigned tcp_adaptive_writes;
    unsigned tcp_cooperative_waits;
    uint64_t tcp_queue_us,tcp_idle_us; /* Last frame: submit->worker; previous ACK completion->worker. */
    int tcp_error;
    int tcp_phase;
    uint32_t tcp_metric_session,tcp_metric_frame;
    size_t tcp_metric_bytes;
    raw_result_t tcp_metric_result;
    uint64_t tcp_header_us,tcp_payload_us,tcp_write_wait_us,tcp_read_wait_us,tcp_yield_us,tcp_copy_us;
    unsigned tcp_write_calls,tcp_read_calls,tcp_partial_writes,tcp_send_waits,tcp_ack_waits,tcp_resource_waits;
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
/* Selected SDIO transport. TCP completion waits for the peer CRC ACK. */
esp_err_t psram_frame_submit(const void *data,size_t length,const bridge_frame_t *frame,
                            psram_udp_completion_cb_t completion_cb,void *user_ctx);
void psram_frame_prepare(bool continuous); /* New explicit SDIO test, only while idle. */
const char *psram_frame_transport(void);
