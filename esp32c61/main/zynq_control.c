#include "zynq_control.h"
#include "zynq_uart_bridge.h"
#include "sdio_bridge.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <inttypes.h>

static SemaphoreHandle_t lock;
static QueueHandle_t replies;
static TaskHandle_t worker;
static zynq_control_job active,finished;
static char partial[WC_LINE],ps_status[WC_RESPONSE];
static size_t used;
static bool dropping,ready,synced,lease;
static uint32_t session,sequence,epoch,lease_client;
static int64_t lease_activity,status_at;
/* One bounded OFF latch, not a second normal batch queue. */
static struct { bool pending; uint32_t client,request,id; } urgent;
static uint32_t job_sequence;
static void take(void) { xSemaphoreTake(lock,portMAX_DELAY); }
static void give(void) { xSemaphoreGive(lock); }
static bool emergency(const char *s) { return !strcmp(s,"HV DISABLE") || !strcmp(s,"BEAM STOP"); }
static bool off_pending(void) { take(); bool pending=urgent.pending; give(); return pending; }
static uint32_t new_job_id(void) { if(!++job_sequence) ++job_sequence; return job_sequence; }

bool zynq_control_ready(void) { return ready; }
void zynq_control_uart_lost(void) {
    if(!ready) return;
    take(); synced=false; epoch++; ps_status[0]=0; status_at=0; give();
    used=0; dropping=true;
}
void zynq_control_feed(const uint8_t *bytes,size_t count) {
    if(!ready) return;
    for(size_t i=0;i<count;i++) {
        unsigned char c=bytes[i];
        if(c=='\r'||c=='\n') {
            partial[used]=0;
            if(!dropping && used) {
                if(!strncmp(partial,"@UART_LOG",9)) zynq_control_uart_lost();
                wc_message m;
                if(wc_decode(partial,true,&m)) (void)xQueueSend(replies,&m,0);
            }
            used=0; dropping=false;
        } else if(!dropping) {
            if(used>=sizeof(partial)-1) { used=0; dropping=true; }
            else partial[used++]=(char)c;
        }
    }
}
/* Never send automatically again after an ambiguous send/ACK outcome. */
static bool exchange(const char *command,char *response) {
    if(off_pending() && strcmp(command,"HV DISABLE") && strcmp(command,"STATUS")) {
        snprintf(response,WC_RESPONSE,"priority OFF; command not dispatched"); return false;
    }
    char wire[WC_LINE];
    take(); uint32_t generation=epoch;
    uint32_t req=sequence==UINT32_MAX?0:++sequence; give();
    if(!req) { snprintf(response,WC_RESPONSE,"request counter exhausted; reboot ESP32"); return false; }
    xQueueReset(replies);
    int len=wc_encode(wire,sizeof(wire),false,session,req,command);
    if(len<0 || zynq_uart_bridge_send(wire,(size_t)len)!=ESP_OK) {
        snprintf(response,WC_RESPONSE,"UART TX outcome unknown"); return false;
    }
    int64_t until=esp_timer_get_time()+20000000;
    while(esp_timer_get_time()<until) {
        if(off_pending() && strcmp(command,"HV DISABLE")) {
            snprintf(response,WC_RESPONSE,"priority OFF; prior command outcome unknown"); return false;
        }
        take(); bool lost=epoch!=generation; give();
        if(lost) { snprintf(response,WC_RESPONSE,"UART lost/reset; outcome unknown"); return false; }
        wc_message m;
        if(xQueueReceive(replies,&m,pdMS_TO_TICKS(20))!=pdTRUE) continue;
        if(m.session!=session || m.request!=req) continue;
        snprintf(response,WC_RESPONSE,"%.127s",m.text);
        if(!strncmp(response,"@STATUS ",8)) {
            take(); snprintf(ps_status,sizeof(ps_status),"%s",response); status_at=esp_timer_get_time(); give();
        }
        return true;
    }
    snprintf(response,WC_RESPONSE,"UART response timeout; outcome unknown"); return false;
}
static void release_lease(void) {
    take(); lease=false; lease_client=0; give();
    sdio_bridge_control_gate(false);
}
static bool handshake(char *answer) {
    if(!exchange("STATUS",answer) || strncmp(answer,"@STATUS v=1 ",12)) return false;
    take(); synced=true; bool owned=lease; give();
    if(owned && strstr(answer," cfg=0 ") && sdio_bridge_control_drained()) release_lease();
    return true;
}
static bool drain(void) {
    sdio_bridge_control_gate(true);
    int64_t deadline=esp_timer_get_time()+90000000;
    while(!sdio_bridge_control_drained()) {
        if(off_pending() || esp_timer_get_time()>=deadline) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}
static void control_task(void *arg) {
    (void)arg;
    char answer[WC_RESPONSE],command[WC_COMMAND];
    vTaskDelay(1); /* Let start() publish readiness before any UART response. */
    /* One startup query only. Page polling never wakes the UART. */
    (void)handshake(answer);
    for(;;) {
        take();
        if(urgent.pending) {
            if(active.busy) {
                active.busy=false; strcpy(active.state,"unknown");
                strcpy(active.error,"preempted by priority HV DISABLE; do not replay"); finished=active;
            }
            memset(&active,0,sizeof(active));
            active.client=urgent.client;active.request=urgent.request;active.id=urgent.id;
            active.count=1;active.busy=true;strcpy(active.state,"running");strcpy(active.commands[0],"HV DISABLE");
            urgent.pending=false;
        }
        give();
        take(); bool working=active.busy;
        bool expired=lease && esp_timer_get_time()-lease_activity>=60000000;
        give();
        if(!working) {
            if(expired) {
                if(exchange("CFG ABORT",answer) && !strncmp(answer,"@ACK ",5) && sdio_bridge_control_drained()) release_lease();
                else { take(); lease_activity=esp_timer_get_time(); synced=false; give(); }
            }
            (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(100)); continue;
        }
        take(); bool have_sync=synced; give();
        bool failure=false,unknown=false;
        take(); bool off=active.count==1 && !strcmp(active.commands[0],"HV DISABLE"); give();
        /* OFF is never delayed by a fresh STATUS timeout. The PS still checks
         * wire/session validity. A missing peer cannot be remotely guaranteed off. */
        if(off && !have_sync) {
            /* Prime the session in wire order, without waiting 20s for STATUS.
             * Only a read-only query is pipelined; OFF itself is still sent once. */
            char prime[WC_LINE];take();uint32_t query=sequence==UINT32_MAX?0:++sequence;give();
            int n=wc_encode(prime,sizeof(prime),false,session,query,"STATUS");
            if(n>0) (void)zynq_uart_bridge_send(prime,(size_t)n);
        }
        if(!off && !have_sync && !handshake(answer)) { failure=true; unknown=true; }
        for(unsigned i=0;!failure;i++) {
            take(); unsigned count=active.count; uint32_t client=active.client;
            if(i<count) strcpy(command,active.commands[i]);
            give(); if(i>=count) break;
            bool begin=!strncmp(command,"CFG BEGIN ",10);
            if(begin) {
                take(); lease=true; lease_client=client; lease_activity=esp_timer_get_time(); give();
                if(!drain()) {
                    strcpy(answer,off_pending()?"Priority OFF; configuration BEGIN not sent":"DRAIN_TIMEOUT; no configuration command sent");
                    failure=true;
                    /* Keep the gate until slots really drain; STATUS can recover. */
                }
            }
            if(!failure && !exchange(command,answer)) { failure=true; unknown=true; }
            else if(!failure && strncmp(answer,"@ACK ",5) && strncmp(answer,"@RD ",4) && strncmp(answer,"@STATUS ",8)) failure=true;
            take(); snprintf(active.responses[i],WC_RESPONSE,"%s",answer); active.completed=i+1;
            if(lease_client==client) lease_activity=esp_timer_get_time();
            give();
            if(!failure && (!strcmp(command,"CFG END")||!strcmp(command,"CFG ABORT"))) release_lease();
        }
        if(failure) {
            take(); bool owned=lease; give();
            if(owned && !unknown && !off_pending()) {
                char cleanup[WC_RESPONSE];
                if(exchange("CFG ABORT",cleanup) && !strncmp(cleanup,"@ACK ",5) && sdio_bridge_control_drained()) release_lease();
            }
        }
        if(!failure && off && lease) {
            char cleanup[WC_RESPONSE];
            if(exchange("CFG ABORT",cleanup) && !strncmp(cleanup,"@ACK ",5) && sdio_bridge_control_drained()) release_lease();
        }
        if(!failure && !off_pending()) {
            char observed[WC_RESPONSE];
            if(!handshake(observed)) {
                take(); synced=false; ps_status[0]=0; status_at=0; give();
            }
        }
        take();
        strcpy(active.state,unknown?"unknown":failure?"failed":"succeeded");
        if(failure) snprintf(active.error,sizeof(active.error),"%.95s",answer);
        if(failure) synced=false;
        if(unknown) { synced=false; ps_status[0]=0; status_at=0; }
        active.busy=false; finished=active; give();
    }
}
esp_err_t zynq_control_start(void) {
    if(ready) return ESP_OK;
    if(!zynq_uart_bridge_available()) return ESP_ERR_INVALID_STATE;
    lock=xSemaphoreCreateMutex(); replies=xQueueCreate(2,sizeof(wc_message));
    if(!lock || !replies) {
        if(lock) vSemaphoreDelete(lock);
        if(replies) vQueueDelete(replies);
        lock=NULL; replies=NULL; return ESP_ERR_NO_MEM;
    }
    session=esp_random(); if(!session) session=1;
    job_sequence=esp_random();
    if(xTaskCreate(control_task,"zynq_control",4096,NULL,3,&worker)!=pdPASS) {
        vSemaphoreDelete(lock); vQueueDelete(replies); lock=NULL; replies=NULL; return ESP_ERR_NO_MEM;
    }
    ready=true;
    ESP_LOGW("zynq_control","HTTP control has no authentication; use a trusted LAN only");
    return ESP_OK;
}
esp_err_t zynq_control_submit(const zynq_control_job *input,bool allow_hv,uint32_t *id) {
    if(!ready) return ESP_ERR_INVALID_STATE;
    if(!input->client || !input->request || !input->count || input->count>WC_BATCH) return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<input->count;i++)
        if(wc_validate(input->commands[i]) || (!strcmp(input->commands[i],"HV ENABLE")&&!allow_hv)) return ESP_ERR_INVALID_ARG;
    take();
    if(urgent.pending && urgent.client==input->client && urgent.request==input->request) {
        bool same=input->count==1 && !strcmp(input->commands[0],"HV DISABLE");
        *id=urgent.id;give();return same?ESP_OK:ESP_ERR_INVALID_ARG;
    }
    zynq_control_job *prior=NULL;
    if(active.id && active.client==input->client && active.request==input->request) prior=&active;
    else if(finished.id && finished.client==input->client && finished.request==input->request) prior=&finished;
    if(prior) {
        bool same=prior->count==input->count && !memcmp(prior->commands,input->commands,sizeof(prior->commands));
        *id=prior->id; give(); return same?ESP_OK:ESP_ERR_INVALID_ARG;
    }
    if(active.busy || urgent.pending) {
        if(input->count==1 && !strcmp(input->commands[0],"HV DISABLE") && !urgent.pending) {
            urgent.pending=true;urgent.client=input->client;urgent.request=input->request;
            urgent.id=new_job_id();*id=urgent.id;give();xTaskNotifyGive(worker);return ESP_OK;
        }
        give(); return ESP_ERR_TIMEOUT;
    }
    if(lease && lease_client!=input->client) {
        for(unsigned i=0;i<input->count;i++) if(!emergency(input->commands[i]) && strcmp(input->commands[i],"STATUS")) {
            give(); return ESP_ERR_TIMEOUT;
        }
    }
    if(lease && esp_timer_get_time()-lease_activity>=60000000 &&
       !(input->count==1 && !strcmp(input->commands[0],"HV DISABLE"))) { give(); return ESP_ERR_TIMEOUT; }
    active=*input; active.id=new_job_id();
    active.busy=true; active.completed=0; strcpy(active.state,"running"); active.error[0]=0;
    memset(active.responses,0,sizeof(active.responses)); *id=active.id; give();
    xTaskNotifyGive(worker); return ESP_OK;
}
bool zynq_control_job_get(uint32_t id,zynq_control_job *out) {
    if(!lock) return false;
    take(); bool found=true;
    if(urgent.pending && urgent.id==id) {
        memset(out,0,sizeof(*out));out->client=urgent.client;out->request=urgent.request;
        out->id=id;out->count=1;out->busy=true;strcpy(out->state,"queued");strcpy(out->commands[0],"HV DISABLE");
    }
    else if(active.id==id) *out=active;
    else if(finished.id==id) *out=finished;
    else found=false;
    give(); return found;
}
void zynq_control_status_json(cJSON *parent) {
    cJSON *j=cJSON_AddObjectToObject(parent,"zynq_control"); if(!j) return;
    cJSON_AddBoolToObject(j,"ready",ready);
    if(!lock) return;
    take();
    cJSON_AddBoolToObject(j,"synced",synced);
    cJSON_AddBoolToObject(j,"busy",active.busy||urgent.pending);
    cJSON_AddBoolToObject(j,"config_owned",lease);
    cJSON_AddStringToObject(j,"ps_status",ps_status);
    cJSON_AddNumberToObject(j,"status_age_ms",status_at?(esp_timer_get_time()-status_at)/1000:-1);
    give();
}
