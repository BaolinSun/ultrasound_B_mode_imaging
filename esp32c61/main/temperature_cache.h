#ifndef TEMPERATURE_CACHE_H
#define TEMPERATURE_CACHE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct {
    bool has_sample,stale,history_available;
    uint32_t seq;
    uint64_t age_ms;
    uint8_t mask;
    int32_t mc[3];
} temperature_status_t;
#define TEMP_HISTORY_POINTS 37U
#define TEMP_HISTORY_WINDOW_MS 360000U
#define TEMP_HISTORY_JSON_BYTES 4096U
typedef struct {
    uint64_t received_ms;
    uint32_t seq;
    int32_t mc[3];
    uint8_t mask;
} temperature_record_t;
typedef struct {
    uint64_t now_ms;
    uint32_t generation,source_gaps,parse_errors,uart_resets;
    unsigned count;
    bool available;
    temperature_record_t records[TEMP_HISTORY_POINTS];
} temperature_history_t;
bool temperature_cache_init(void);
void temperature_cache_history_get(temperature_history_t *out,uint64_t now_ms);
size_t temperature_history_json(const temperature_history_t *history,char *out,size_t capacity);
void temperature_cache_feed(const uint8_t *bytes,size_t length,uint64_t now_ms);
void temperature_cache_uart_lost(void);
void temperature_cache_get(temperature_status_t *out,uint64_t now_ms);
const char *temperature_channel_status(const temperature_status_t *s,unsigned channel);
#endif
