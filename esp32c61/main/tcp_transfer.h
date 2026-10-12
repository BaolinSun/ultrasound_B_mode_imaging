#pragma once
#include "framed_transfer.h"
#define TCP_FRAME_HEADER 24U
#define TCP_FRAME_ACK 20U
#define TCP_STAGE_BYTES 4096U
#define TCP_FRAME_MAGIC UINT32_C(0x55425354)
#define TCP_ACK_MAGIC UINT32_C(0x55425341)
typedef enum { TCP_ERROR_NONE, TCP_ERROR_IO, TCP_ERROR_CLOSED,
    TCP_ERROR_BAD_ACK, TCP_ERROR_REJECTED, TCP_ERROR_CONNECT,
    TCP_ERROR_SEND_TIMEOUT, TCP_ERROR_ACK_TIMEOUT } tcp_error_t;
typedef struct { size_t sent; unsigned chunks, waits; uint64_t ack_ms;
    tcp_error_t error; } tcp_metrics_t;
typedef struct {
    void *ctx;
    uint64_t (*now_ms)(void*);
    raw_result_t (*abort_reason)(void*);
    /* Positive byte count, 0 peer closed, -2 would block, -1 error. */
    int (*write)(void*,const uint8_t*,size_t);
    int (*read)(void*,uint8_t*,size_t);
    void (*wait)(void*,bool reading,unsigned ms);
    void (*yield_ms)(void*,unsigned ms);
    void (*progress)(void*,size_t,unsigned,unsigned);
} tcp_io_t;
void tcp_frame_header(uint8_t *out,const bridge_frame_t *frame);
const char *tcp_error_name(int error);
raw_result_t tcp_frame_send(const uint8_t *data,size_t length,const bridge_frame_t *frame,
    uint8_t *staging,uint64_t deadline,const tcp_io_t *io,tcp_metrics_t *metrics);
