#ifndef UART_LOG_FRAMER_H
#define UART_LOG_FRAMER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UART_LOG_LINE_BYTES 512U
#define UART_LOG_IDLE_MS 200U
typedef void (*uart_log_emit_t)(const uint8_t *,size_t,void *);
typedef struct {
    uint8_t data[UART_LOG_LINE_BYTES];
    size_t used;
    uint64_t last_rx_ms;
    bool previous_cr;
} uart_log_framer_t;
void uart_log_framer_reset(uart_log_framer_t *f);
void uart_log_framer_feed(uart_log_framer_t *f,const uint8_t *data,size_t length,
                          uint64_t now_ms,uart_log_emit_t emit,void *ctx);
void uart_log_framer_idle(uart_log_framer_t *f,uint64_t now_ms,uart_log_emit_t emit,void *ctx);
#endif
