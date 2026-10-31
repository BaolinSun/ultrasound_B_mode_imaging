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
static bool afe_blocked=true;
static bool tx_blocked=true;
static bool cpu0_sequence_active;
static const char *cpu0_power_stage="idle";
/* Call with control mutex held. Also hold upload admission between stages. */
static bool power_blocked(void) {return afe_blocked||tx_blocked||lease||cpu0_sequence_active;}
static char tx_state[16]="unknown",tx_error[80]="Waiting for PS status";
static uint32_t tx_flags;
static int64_t tx_at;
static bool tx_power_op(const char *s) {return !strcmp(s,"TX7332 SLEEP")||!strcmp(s,"TX7332 WAKE");}
static char afe_state[16]="unknown",afe_error[80]="Waiting for PS status";
static uint32_t afe_flags;
static int64_t afe_at;
static int64_t operation_deadline;
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
static void afe_observe(const char *answer) {
    const char *s=!strncmp(answer,"@AFE state=",11)?answer+11:strstr(answer," afe=");
    if(!s) return;
    if(!strncmp(s," afe=",5)) s+=5;
    char value[16];size_t n=strcspn(s," ");
    if(!n || n>=sizeof(value)) return;
    memcpy(value,s,n);value[n]=0;
    const char *proof=strstr(answer," flags=");
    uint32_t proof_flags=0;char proof_hex[9];bool proof_valid=false;
    if(proof && strlen(proof)>=16 && proof[15]==' ') {
        memcpy(proof_hex,proof+7,8);proof_hex[8]=0;proof_valid=wc_hex8(proof_hex,&proof_flags);
    }
    bool invalid_ready=!strncmp(answer,"@AFE ",5) && !strcmp(value,"ready") &&
        (!proof_valid || proof_flags!=31U);
    if(invalid_ready) strcpy(value,"unknown");
    if(strcmp(value,"ready")&&strcmp(value,"sleep")&&strcmp(value,"fault")&&strcmp(value,"waking")&&strcmp(value,"quiescing")&&strcmp(value,"unknown")) return;
    take();strcpy(afe_state,value);afe_blocked=strcmp(value,"ready")!=0;afe_at=esp_timer_get_time();
    const char *e=strstr(answer," error=");if(e) snprintf(afe_error,sizeof(afe_error),"%s",e+7);
    else if(!afe_blocked) strcpy(afe_error,"NONE");
    if(invalid_ready) strcpy(afe_error,"AFE ready flags not verified");
    const char *f=strstr(answer," flags=");if(f) afe_flags=(uint32_t)strtoul(f+7,NULL,16);
    bool blocked=power_blocked();give();sdio_bridge_control_gate(blocked);
}
static void tx_observe(const char *answer) {
    const char *s=!strncmp(answer,"@TX state=",10)?answer+10:strstr(answer," tx=");
    if(!s)return;
    if(!strncmp(s," tx=",4))s+=4;
    char value[16];size_t n=strcspn(s," ");if(!n||n>=sizeof(value))return;
    memcpy(value,s,n);value[n]=0;
    if(strcmp(value,"ready")&&strcmp(value,"sleep")&&strcmp(value,"quiescing")&&strcmp(value,"waking")&&strcmp(value,"fault")&&strcmp(value,"unknown"))return;
    const char *f=strstr(answer," flags=");uint32_t flags=0;char hex[9];bool valid=false;
    if(f && strlen(f)>=16 && f[15]==' '){memcpy(hex,f+7,8);hex[8]=0;valid=wc_hex8(hex,&flags);}
    bool bad=!strncmp(answer,"@TX ",4)&&!strcmp(value,"ready")&&(!valid||(flags&62U)!=8U);
    if(bad)strcpy(value,"unknown");
    take();strcpy(tx_state,value);tx_blocked=strcmp(value,"ready")!=0;tx_at=esp_timer_get_time();
    if(valid)tx_flags=flags;
    const char *e=strstr(answer," error=");if(e)snprintf(tx_error,sizeof(tx_error),"%s",e+7);else if(!tx_blocked)strcpy(tx_error,"NONE");
    if(bad)strcpy(tx_error,"TX ready flags not verified");
    bool blocked=power_blocked();give();sdio_bridge_control_gate(blocked);
}
void zynq_control_uart_lost(void) {
    if(!ready) return;
    take(); synced=false; epoch++; ps_status[0]=0; status_at=0;
    afe_blocked=true;strcpy(afe_state,"unknown");strcpy(afe_error,"UART lost/reset");afe_at=0;give();
    take();tx_blocked=true;strcpy(tx_state,"unknown");strcpy(tx_error,"UART lost/reset");tx_at=0;give();
    take();cpu0_power_stage="unknown";give();
    sdio_bridge_control_gate(true);
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
    if(operation_deadline && until>operation_deadline) until=operation_deadline;
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
        afe_observe(response);tx_observe(response);
        if(!strncmp(response,"@STATUS ",8)) {
            take(); snprintf(ps_status,sizeof(ps_status),"%s",response); status_at=esp_timer_get_time(); give();
        }
        return true;
    }
    snprintf(response,WC_RESPONSE,"UART response timeout; outcome unknown"); return false;
}
static void release_lease(void) {
    take(); lease=false; lease_client=0; bool blocked=power_blocked();give();
    sdio_bridge_control_gate(blocked);
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
    if(operation_deadline && deadline>operation_deadline) deadline=operation_deadline;
    while(!sdio_bridge_control_drained()) {
        if(off_pending() || esp_timer_get_time()>=deadline) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}
static bool afe_operation_inner(const char *command,char *answer) {
    take();afe_blocked=true;strcpy(afe_state,!strcmp(command,"AFE SLEEP")?"quiescing":"waking");
    strcpy(afe_error,"NONE");give();sdio_bridge_control_gate(true);
    if(!strcmp(command,"AFE SLEEP")) {
        /* PREPARE closes HV and scans on PS BEFORE the 90s network drain. */
        if(!exchange("AFE PREPARE",answer) || strncmp(answer,"@ACK ",5)) return false;
        if(!drain()) {snprintf(answer,WC_RESPONSE,"AFE drain timeout/interrupted; no SLEEP commit sent");return false;}
        /* ESP free slots do not prove that the last host DMA/USB callback has
         * retired. Poll a read-only PS observation before committing SLEEP. */
        for(;;) {
            if(!exchange("AFE STATUS",answer) || strncmp(answer,"@AFE ",5)) return false;
            if(strstr(answer,"state=sleep ")) return true;
            if(!strstr(answer,"state=quiescing ")) return false;
            if(strstr(answer," idle=1 ")) break;
            if(esp_timer_get_time()>=operation_deadline) return false;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else if(!drain()) {
        /* An explicit retry after failed SLEEP may still own a Wi-Fi buffer.
         * Do not report a stopped READY state while that callback is pending. */
        snprintf(answer,WC_RESPONSE,"AFE transfer busy/interrupted; no WAKE sent");return false;
    }
    if(!exchange(command,answer) || strncmp(answer,"@ACK ",5)) return false;
    int64_t deadline=esp_timer_get_time()+6000000;
    do {
        if(!exchange("AFE STATUS",answer) || strncmp(answer,"@AFE ",5)) return false;
        if(!strcmp(command,"AFE SLEEP")) return strstr(answer,"state=sleep ")!=NULL;
        if(strstr(answer,"state=ready ")) {
            take();bool verified=!afe_blocked;give();return verified;
        }
        if(!strstr(answer,"state=waking ")) return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    } while(esp_timer_get_time()<deadline);
    snprintf(answer,WC_RESPONSE,"AFE wake outcome unknown; query explicitly");return false;
}
static bool afe_operation(const char *command,char *answer) {
    operation_deadline=esp_timer_get_time()+(!strcmp(command,"AFE SLEEP")?90000000:7000000);
    bool ok=afe_operation_inner(command,answer);operation_deadline=0;return ok;
}
static bool tx_operation(const char *command,char *answer) {
    bool sleep=!strcmp(command,"TX7332 SLEEP");
    operation_deadline=esp_timer_get_time()+(sleep?90000000:7000000);
    take();tx_blocked=true;strcpy(tx_state,sleep?"quiescing":"waking");strcpy(tx_error,"NONE");give();
    sdio_bridge_control_gate(true);bool ok=false;
    if(sleep) {
        if(!exchange("TX7332 PREPARE",answer)||strncmp(answer,"@ACK ",5))goto done;
        if(!drain()){snprintf(answer,WC_RESPONSE,"TX drain timeout/interrupted; no SLEEP commit sent");goto done;}
        for(;;) {
            if(!exchange("TX7332 STATUS",answer)||strncmp(answer,"@TX ",4))goto done;
            if(strstr(answer,"state=sleep ")){ok=true;goto done;}
            if(!strstr(answer,"state=quiescing "))goto done;
            if(strstr(answer," idle=1 "))break;
            if(esp_timer_get_time()>=operation_deadline)goto done;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else if(!drain()){snprintf(answer,WC_RESPONSE,"TX transfer busy/interrupted; no WAKE sent");goto done;}
    if(!exchange(command,answer)||strncmp(answer,"@ACK ",5))goto done;
    while(esp_timer_get_time()<operation_deadline) {
        if(!exchange("TX7332 STATUS",answer)||strncmp(answer,"@TX ",4))goto done;
        if(sleep&&strstr(answer,"state=sleep ")){ok=true;goto done;}
        if(!sleep&&strstr(answer,"state=ready ")){take();ok=!tx_blocked;give();goto done;}
        if(!strstr(answer,sleep?"state=quiescing ":"state=waking "))goto done;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    snprintf(answer,WC_RESPONSE,"TX operation timeout; outcome unknown; query explicitly");
 done:
    operation_deadline=0;return ok;
}
static void cpu0_stage(const char *stage) {take();cpu0_power_stage=stage;give();}
static bool cpu0_operation(const char *command,char *answer) {
    bool sleep=!strcmp(command,"CPU0 SLEEP");
    take();cpu0_sequence_active=true;uint32_t generation=epoch;give();
    sdio_bridge_control_gate(true);
    /* Check PS-owned USB/UART configuration too, not only the ESP lease. */
    if(!exchange("STATUS",answer))goto fail;
    if(!strstr(answer," cfg=0 ")){snprintf(answer,WC_RESPONSE,"@NACK 0 CPU0_CFG_ACTIVE");goto fail;}
    for(unsigned step=0;step<2;step++) {
        bool tx=sleep?(step==0):(step==1);
        cpu0_stage(sleep?(tx?"tx_sleep":"afe_sleep"):(tx?"tx_wake":"afe_wake"));
        const char *op=sleep?(tx?"TX7332 SLEEP":"AFE SLEEP"):(tx?"TX7332 WAKE":"AFE WAKE");
        if(!(tx?tx_operation(op,answer):afe_operation(op,answer))) {
            take();
            if(tx){tx_blocked=true;snprintf(tx_error,sizeof tx_error,"%.79s",answer);if(strncmp(answer,"@TX ",4))strcpy(tx_state,"unknown");}
            else {afe_blocked=true;snprintf(afe_error,sizeof afe_error,"%.79s",answer);if(strncmp(answer,"@AFE ",5))strcpy(afe_state,"unknown");}
            give();goto fail;
        }
        if(step==0) {
            cpu0_stage(sleep?"sleep_delay":"wake_delay");
            int64_t until=esp_timer_get_time()+1000000;
            /* Worker remains exclusive even if browser closes. Check OFF/reset
             * every <=20 ms; never continue stage two after interruption. */
            while(esp_timer_get_time()<until) {
                take();bool lost=generation!=epoch;give();
                if(lost||off_pending()){snprintf(answer,WC_RESPONSE,"CPU0 sequence interrupted; second stage not sent");goto fail;}
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            take();bool lost=generation!=epoch;give();
            if(lost||off_pending()){snprintf(answer,WC_RESPONSE,"CPU0 sequence interrupted; second stage not sent");goto fail;}
        }
    }
    cpu0_stage(sleep?"sleeping":"awake");
    snprintf(answer,WC_RESPONSE,"@ACK 0 CPU0_%s %s",sleep?"SLEEP":"WAKE",sleep?"sleep":"ready");
    return true;
fail:
    cpu0_stage("failed");return false;
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
            bool tx_power=tx_power_op(command);
            bool combined=zynq_cpu0_power_op(command);
            bool power=combined||tx_power||!strcmp(command,"AFE SLEEP")||!strcmp(command,"AFE WAKE");
            if(!failure && power) {
                if(!(combined?cpu0_operation(command,answer):tx_power?tx_operation(command,answer):afe_operation(command,answer))) {
                    failure=true;unknown=strncmp(answer,"@NACK ",6)!=0 && strncmp(answer,"@TX ",4)!=0 && strncmp(answer,"@AFE ",5)!=0;
                }
            } else if(!failure && !exchange(command,answer)) { failure=true; unknown=true; }
            else if(!failure && strncmp(answer,"@ACK ",5) && strncmp(answer,"@FOCUS ",7) && strncmp(answer,"@RD ",4) && strncmp(answer,"@STATUS ",8) && strncmp(answer,"@TX ",4) && strncmp(answer,"@AFE ",5)) failure=true;
            if(failure && power && !combined) {
                take();
                if(tx_power){tx_blocked=true;snprintf(tx_error,sizeof(tx_error),"%.79s",answer);if(strncmp(answer,"@TX ",4))strcpy(tx_state,"unknown");}
                else {afe_blocked=true;snprintf(afe_error,sizeof(afe_error),"%.79s",answer);if(strncmp(answer,"@AFE ",5))strcpy(afe_state,"unknown");}
                give();
            }
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
        if(unknown) { tx_blocked=true;strcpy(tx_state,"unknown");strcpy(tx_error,"UART outcome unknown");synced=false; ps_status[0]=0; status_at=0; afe_blocked=true;strcpy(afe_state,"unknown");snprintf(afe_error,sizeof(afe_error),"%.79s",answer); }
        cpu0_sequence_active=false;
        bool gate_blocked=power_blocked();
        active.busy=false; finished=active; give();sdio_bridge_control_gate(gate_blocked);
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
    sdio_bridge_control_gate(true);
    ESP_LOGW("zynq_control","HTTP control has no authentication; use a trusted LAN only");
    return ESP_OK;
}
esp_err_t zynq_control_submit(const zynq_control_job *input,bool allow_hv,uint32_t *id) {
    if(!ready) return ESP_ERR_INVALID_STATE;
    if(!input->client || !input->request || !input->count || input->count>WC_BATCH) return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<input->count;i++)
        if(zynq_control_validate(input->commands[i]) || !strcmp(input->commands[i],"AFE PREPARE") || !strcmp(input->commands[i],"TX7332 PREPARE") ||
           ((zynq_cpu0_power_op(input->commands[i])||tx_power_op(input->commands[i])||(!strcmp(input->commands[i],"AFE SLEEP"))||(!strcmp(input->commands[i],"AFE WAKE")))&&input->count!=1) ||
           (!strcmp(input->commands[i],"HV ENABLE")&&!allow_hv)) return ESP_ERR_INVALID_ARG;
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
    if(lease && input->count==1 && (zynq_cpu0_power_op(input->commands[0]) || tx_power_op(input->commands[0]) || !strcmp(input->commands[0],"AFE SLEEP") || !strcmp(input->commands[0],"AFE WAKE"))) {give();return ESP_ERR_TIMEOUT;}
    if(lease && lease_client!=input->client) {
        for(unsigned i=0;i<input->count;i++) if(!emergency(input->commands[i]) && strcmp(input->commands[i],"STATUS") && strcmp(input->commands[i],"FOCUS STATUS") && strcmp(input->commands[i],"AFE STATUS") && strcmp(input->commands[i],"TX7332 STATUS")) {
            give(); return ESP_ERR_TIMEOUT;
        }
    }
    if(afe_blocked||tx_blocked) for(unsigned i=0;i<input->count;i++) {
        const char *c=input->commands[i];
        if(!zynq_cpu0_power_op(c)&&strcmp(c,"CPU1 DISABLE")&&strcmp(c,"FOCUS STATUS")&&strcmp(c,"STATUS")&&strcmp(c,"TX7332 STATUS")&&!tx_power_op(c)&&strcmp(c,"AFE STATUS")&&strcmp(c,"AFE SLEEP")&&strcmp(c,"AFE WAKE")&&!emergency(c)) {give();return ESP_ERR_INVALID_STATE;}
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
    cJSON_AddStringToObject(j,"cpu0_power_stage",cpu0_power_stage);
    cJSON_AddBoolToObject(j,"cpu0_power_supported",true);
    cJSON_AddStringToObject(j,"ps_status",ps_status);
    cJSON_AddNumberToObject(j,"status_age_ms",status_at?(esp_timer_get_time()-status_at)/1000:-1);
    cJSON_AddStringToObject(j,"tx_state",tx_state);
    cJSON_AddStringToObject(j,"tx_error",tx_error);
    cJSON_AddNumberToObject(j,"tx_flags",tx_flags);
    cJSON_AddNumberToObject(j,"tx_age_ms",tx_at?(esp_timer_get_time()-tx_at)/1000:-1);
    cJSON_AddStringToObject(j,"afe_state",afe_state);
    cJSON_AddStringToObject(j,"afe_error",afe_error);
    cJSON_AddNumberToObject(j,"afe_flags",afe_flags);
    cJSON_AddNumberToObject(j,"afe_age_ms",afe_at?(esp_timer_get_time()-afe_at)/1000:-1);
    give();
}
