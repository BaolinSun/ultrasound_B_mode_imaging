#include "raw_transfer.h"
#include <string.h>

bool raw_span_inside(uintptr_t start, size_t length, uintptr_t base, size_t capacity)
{
    return length > 0 && capacity > 0 && start >= base &&
           capacity <= UINTPTR_MAX - base && start - base < capacity &&
           length <= capacity - (start - base);
}

raw_result_t raw_reserve(raw_session_t *s, const void *data, size_t length,
                         size_t maximum, bool valid_span, bool connected,
                         bool target_configured, raw_completion_t done, void *ctx)
{
    if (!data || !length || length > maximum || !valid_span || !done) return RAW_INVALID;
    if (s->busy) return RAW_BUSY;
    if (!connected || !target_configured) return RAW_NOT_READY;
    *s = (raw_session_t){true, data, length, done, ctx};
    return RAW_OK;
}

bool raw_finish(raw_session_t *s, raw_completion_t *done, void **ctx)
{
    if (!s->busy) return false;
    *done = s->completion;
    *ctx = s->ctx;
    *s = (raw_session_t){0};
    return true;
}

void raw_pattern_fill(uint8_t *data, size_t length)
{
    for (size_t offset = 0; offset < length; ++offset) {
        uint32_t word = (uint32_t)(offset / 4U) ^ UINT32_C(0xA5C30000);
        data[offset] = (uint8_t)(word >> (8U * (offset % 4U)));
    }
}

const char *raw_result_name(raw_result_t r)
{
    static const char *names[] = {"sent_local", "invalid", "busy", "not_ready",
        "cancelled", "link_down", "timeout", "send_error"};
    return (unsigned)r < sizeof(names) / sizeof(names[0]) ? names[r] : "unknown";
}

static raw_result_t check_abort(uint64_t deadline, const raw_io_t *io)
{
    raw_result_t r = io->abort_reason(io->ctx);
    if (r != RAW_OK) return r;
    return io->now_ms(io->ctx) >= deadline ? RAW_TIMEOUT : RAW_OK;
}

raw_result_t psram_raw_send(const uint8_t *data, size_t length, uint8_t *staging,
                       uint64_t deadline, const raw_io_t *io, size_t *sent)
{
    unsigned packets = 0, retries = 0;
    *sent = 0;
    while (*sent < length) {
        raw_result_t result = check_abort(deadline, io);
        if (result != RAW_OK) return result;
        size_t bytes = length - *sent;
        if (bytes > RAW_UDP_BYTES) bytes = RAW_UDP_BYTES;
        memcpy(staging, data + *sent, bytes);
        for (unsigned attempt = 0;; ++attempt) {
            result = check_abort(deadline, io);
            if (result != RAW_OK) return result;
            int ret = io->send(io->ctx, staging, bytes);
            if (ret == 0) break;
            if (ret < 0 || attempt == RAW_RETRY_COUNT) return RAW_SEND_ERROR;
            ++retries;
            io->progress(io->ctx, *sent, packets, retries);
            io->delay_ms(io->ctx, 1);
        }
        *sent += bytes;
        ++packets;
        io->progress(io->ctx, *sent, packets, retries);
        if (*sent < length && packets % RAW_BURST_PACKETS == 0)
            io->delay_ms(io->ctx, 1);
    }
    return check_abort(deadline, io);
}
