#ifndef UB_CPU_STATUS_CACHE_H
#define UB_CPU_STATUS_CACHE_H
#include "cpu_status_protocol.h"
typedef struct {
    bool has_sample,stale;
    uint64_t age_ms;
    cpu_status_message_t message;
} cpu_status_snapshot_t;
void cpu_status_cache_feed(const uint8_t *p,size_t n,uint64_t now_ms);
void cpu_status_cache_uart_lost(void);
void cpu_status_cache_get(cpu_status_snapshot_t *out,uint64_t now_ms);
struct cJSON;
void cpu_status_add_json(struct cJSON *parent,uint64_t now_ms);
#endif
