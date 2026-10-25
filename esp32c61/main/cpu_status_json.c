#include "cpu_status_cache.h"
#include "cJSON.h"
void cpu_status_add_json(cJSON *parent,uint64_t now) {
    cpu_status_snapshot_t s;cpu_status_cache_get(&s,now);
    bool valid=s.has_sample&&!s.stale;
    cJSON *j=cJSON_AddObjectToObject(parent,"cpus");
    cJSON_AddStringToObject(j,"status",s.stale?"stale":s.has_sample?"ok":"waiting");
    cJSON_AddNumberToObject(j,"period_ms",CPU_STATUS_PERIOD_MS);
    cJSON_AddNumberToObject(j,"age_ms",s.has_sample?(double)s.age_ms:-1);
    if(s.has_sample)cJSON_AddNumberToObject(j,"seq",s.message.seq);else cJSON_AddNullToObject(j,"seq");
    cJSON *a=cJSON_AddObjectToObject(j,"cpu0"),*b=cJSON_AddObjectToObject(j,"cpu1");
    cJSON_AddStringToObject(a,"mode",valid?cpu_status_mode_name(s.message.mode):"unknown");
    cJSON_AddStringToObject(a,"reason",valid?cpu_status_reason_name(s.message.reason):"unknown");
    if(valid) {
        cJSON_AddBoolToObject(a,"wfi_enabled",(s.message.flags&CPU_STATUS_WFI_ENABLED)!=0);
        cJSON_AddNumberToObject(a,"entries",s.message.entries);
        cJSON_AddNumberToObject(a,"wait_ms",(double)s.message.wait_us/1000.0);
        cJSON_AddNumberToObject(a,"timer_irqs",s.message.timer_irqs);
        uint32_t r=s.message.cpu_control;bool reset=(r&2U)!=0,stopped=(r&32U)!=0;
        cJSON_AddBoolToObject(b,"reset",reset);cJSON_AddBoolToObject(b,"clock_stopped",stopped);
        cJSON_AddBoolToObject(b,"disabled",reset&&stopped);cJSON_AddNumberToObject(b,"control",r);
    } else {
        cJSON_AddNullToObject(a,"wfi_enabled");cJSON_AddNullToObject(a,"entries");
        cJSON_AddNullToObject(a,"wait_ms");cJSON_AddNullToObject(a,"timer_irqs");
        cJSON_AddNullToObject(b,"reset");cJSON_AddNullToObject(b,"clock_stopped");
        cJSON_AddNullToObject(b,"disabled");cJSON_AddNullToObject(b,"control");
    }
}
