#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RAW_UDP_BYTES 1440U
#define RAW_RETRY_COUNT 100U
#define RAW_BURST_PACKETS 32U

typedef enum {
    RAW_OK = 0, RAW_INVALID, RAW_BUSY, RAW_NOT_READY,
    RAW_CANCELLED, RAW_LINK_DOWN, RAW_TIMEOUT, RAW_SEND_ERROR
} raw_result_t;
typedef void (*raw_completion_t)(raw_result_t result, size_t sent, void *ctx);
typedef struct {
    bool busy;
    const uint8_t *data;
    size_t length;
    raw_completion_t completion;
    void *ctx;
} raw_session_t;

/* Reserve/finish must be serialized by the platform. Callback runs outside lock. */
raw_result_t raw_reserve(raw_session_t *s, const void *data, size_t length,
                         size_t maximum, bool valid_span, bool connected,
                         bool target_configured, raw_completion_t done, void *ctx);
bool raw_finish(raw_session_t *s, raw_completion_t *done, void **ctx);
bool raw_span_inside(uintptr_t start, size_t length, uintptr_t base, size_t capacity);
void raw_pattern_fill(uint8_t *data, size_t length);
const char *raw_result_name(raw_result_t result);

typedef struct {
    void *ctx;
    uint64_t (*now_ms)(void *ctx);
    raw_result_t (*abort_reason)(void *ctx);
    /* 0 success, 1 transient failure, -1 permanent/short send. */
    int (*send)(void *ctx, const uint8_t *data, size_t length);
    void (*delay_ms)(void *ctx, unsigned ms);
    void (*progress)(void *ctx, size_t sent, unsigned packets, unsigned retries);
} raw_io_t;
raw_result_t psram_raw_send(const uint8_t *data, size_t length, uint8_t *staging,
                       uint64_t deadline_ms, const raw_io_t *io, size_t *sent);
