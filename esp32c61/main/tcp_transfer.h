#pragma once
#include "framed_transfer.h"
#define TCP_FRAME_HEADER 24U
#define TCP_FRAME_ACK 20U
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef TCP_DIRECT_PSRAM
#ifdef CONFIG_PSRAM_TCP_DIRECT
#define TCP_DIRECT_PSRAM 1
#else
#define TCP_DIRECT_PSRAM 0
#endif
#endif
#if TCP_DIRECT_PSRAM != 0 && TCP_DIRECT_PSRAM != 1
#error "TCP_DIRECT_PSRAM must be 0 or 1"
#endif
#ifndef TCP_STAGE_BYTES
#ifdef ESP_PLATFORM
#define TCP_STAGE_BYTES CONFIG_PSRAM_TCP_STAGE_BYTES
#else
#define TCP_STAGE_BYTES 5760U
#endif
#endif
_Static_assert(TCP_STAGE_BYTES>=1472 && TCP_STAGE_BYTES<=16384,"invalid TCP staging size");
/* Direct mode keeps one MSS header+prefix and a shared UDP-sized allocation.
 * All later TCP offers remain bounded by TCP_STAGE_BYTES, not frame length. */
#define TCP_FIRST_WRITE_BYTES (TCP_DIRECT_PSRAM ? RAW_UDP_BYTES : TCP_STAGE_BYTES)
#define TCP_STAGING_ALLOC_BYTES (TCP_DIRECT_PSRAM ? BR_UDP_HEADER+RAW_UDP_BYTES : TCP_STAGE_BYTES)
_Static_assert(TCP_FIRST_WRITE_BYTES<=TCP_STAGING_ALLOC_BYTES,"TCP prefix exceeds staging allocation");
_Static_assert(BR_UDP_HEADER+RAW_UDP_BYTES<=TCP_STAGING_ALLOC_BYTES,"UDP staging too small");
#define TCP_YIELD_BYTES 65536U
#define TCP_FRAME_MAGIC UINT32_C(0x55425354)
#define TCP_ACK_MAGIC UINT32_C(0x55425341)
typedef enum { TCP_ERROR_NONE, TCP_ERROR_IO, TCP_ERROR_CLOSED,
    TCP_ERROR_BAD_ACK, TCP_ERROR_REJECTED, TCP_ERROR_CONNECT,
    TCP_ERROR_SEND_TIMEOUT, TCP_ERROR_ACK_TIMEOUT } tcp_error_t;
typedef struct { size_t sent; unsigned chunks, waits; uint64_t ack_ms;
    uint64_t header_us,payload_us,ack_us,write_wait_us,read_wait_us,yield_us,copy_us;
    unsigned write_calls,read_calls,partial_writes,send_waits,ack_waits,resource_waits;
    unsigned cooperative_waits; /* Socket/resource waits >=1 ms during send. */
    size_t staged_bytes,direct_bytes; /* Copied payload vs direct accepted payload. */
    tcp_error_t error; } tcp_metrics_t;

typedef enum { TCP_PHASE_IDLE, TCP_PHASE_CONNECT, TCP_PHASE_HEADER,
    TCP_PHASE_PAYLOAD, TCP_PHASE_ACK } tcp_phase_t;
const char *tcp_phase_name(int phase);
typedef struct {
    void *ctx;
    uint64_t (*now_ms)(void*);
    uint64_t (*now_us)(void*);
    raw_result_t (*abort_reason)(void*);
    /* Positive byte count, 0 peer closed, -2 would block, -3 resource shortage,
     * -1 fatal error. Resource shortage uses a bounded scheduler backoff. */
    int (*write)(void*,const uint8_t*,size_t);
    int (*read)(void*,uint8_t*,size_t);
    void (*wait)(void*,bool reading,bool resource,unsigned ms);
    void (*yield_ms)(void*,unsigned ms);
    void (*progress)(void*,size_t,unsigned,unsigned);
    void (*phase)(void*,tcp_phase_t);
} tcp_io_t;
void tcp_frame_header(uint8_t *out,const bridge_frame_t *frame);
const char *tcp_error_name(int error);
raw_result_t tcp_frame_send(const uint8_t *data,size_t length,const bridge_frame_t *frame,
    uint8_t *staging,uint64_t deadline,const tcp_io_t *io,tcp_metrics_t *metrics);
