#pragma once
#include "esp_err.h"
#include "wifi_control_protocol.h"
#include "cJSON.h"
/* ESP-only orchestration commands: never forward these to PS UART grammar. */
static inline bool zynq_cpu0_power_op(const char *s) {
    return !strcmp(s,"CPU0 SLEEP") || !strcmp(s,"CPU0 WAKE");
}
static inline const char *zynq_control_validate(const char *s) {
    return s && zynq_cpu0_power_op(s)?NULL:wc_validate(s);
}
typedef struct {
    uint32_t client, request, id;
    unsigned count, completed;
    bool busy;
    char state[16], error[96];
    char commands[WC_BATCH][WC_COMMAND], responses[WC_BATCH][WC_RESPONSE];
} zynq_control_job;
esp_err_t zynq_control_start(void);
bool zynq_control_ready(void);
esp_err_t zynq_control_submit(const zynq_control_job *input,bool allow_hv,uint32_t *id);
bool zynq_control_job_get(uint32_t id,zynq_control_job *out);
void zynq_control_feed(const uint8_t *bytes,size_t count);
void zynq_control_uart_lost(void);
void zynq_control_status_json(cJSON *parent);
