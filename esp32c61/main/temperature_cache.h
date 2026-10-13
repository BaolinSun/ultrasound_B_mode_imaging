#ifndef TEMPERATURE_CACHE_H
#define TEMPERATURE_CACHE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct {
    bool has_sample,stale;
    uint32_t seq;
    uint64_t age_ms;
    uint8_t mask;
    int32_t mc[3];
} temperature_status_t;
void temperature_cache_feed(const uint8_t *bytes,size_t length,uint64_t now_ms);
void temperature_cache_uart_lost(void);
void temperature_cache_get(temperature_status_t *out,uint64_t now_ms);
const char *temperature_channel_status(const temperature_status_t *s,unsigned channel);
#endif
