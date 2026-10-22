/* Run the real worker with deterministic scheduling, no ESP/board/network. */
#include "fake_control_sdk.h"
#include "wifi_control_protocol.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../main/zynq_control.c"
static jmp_buf idle;
static wc_message queue_items[2];
static unsigned queue_count,notifications,sends,ps_cfg,ps_hv,held,deleted,drain_steps,off_sends;
static bool gate,drained=true,drop_reply,nack_afe,reset_during_reply,stale_reply;
static int64_t clock_us;
static const char *fault="";
static bool inject_off_when_wait;
static uint32_t injected_id;
static unsigned ps_down,down_writes;
static bool nack_down;
static const char *ps_afe="ready";
static unsigned power_prepares,power_sleeps;
static unsigned wake_queries;
static unsigned host_idle_reads;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return !strcmp(fault,"mutex")?NULL:(void*)1; }
int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait) { (void)wait;assert(s&&!held);held=1;return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { assert(s&&held);held=0;return 1; }
void vSemaphoreDelete(SemaphoreHandle_t s) { assert(s);deleted++; }
QueueHandle_t xQueueCreate(unsigned n,unsigned bytes) { assert(n==2&&bytes==sizeof(wc_message));return !strcmp(fault,"queue")?NULL:(void*)2; }
int xQueueSend(QueueHandle_t q,const void *v,unsigned wait) { (void)wait;assert(q);if(queue_count==2)return 0;queue_items[queue_count++]=*(const wc_message*)v;return 1; }
int xQueueReceive(QueueHandle_t q,void *v,unsigned wait) {
    assert(q);
    if(!queue_count){
        if(inject_off_when_wait) {
            inject_off_when_wait=false;
            zynq_control_job stop={.client=3,.request=14,.count=1};strcpy(stop.commands[0],"HV DISABLE");
            assert(zynq_control_submit(&stop,false,&injected_id)==ESP_OK);
        }
        clock_us+=(int64_t)wait*1000;return 0;
    }
    *(wc_message*)v=queue_items[0];queue_count--;queue_items[0]=queue_items[1];return 1;
}
int xQueueReset(QueueHandle_t q) { assert(q);queue_count=0;return 1; }
void vQueueDelete(QueueHandle_t q) { assert(q);deleted++; }
int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void *ctx,unsigned priority,TaskHandle_t *t) {
    (void)fn;(void)ctx;assert(!strcmp(name,"zynq_control")&&stack==4096&&priority==3);
    *t=(void*)3;return strcmp(fault,"task")?1:0;
}
void xTaskNotifyGive(TaskHandle_t t) { assert(t);notifications++; }
unsigned ulTaskNotifyTake(int clear,unsigned wait) { (void)clear;(void)wait;longjmp(idle,1);return 0; }
void vTaskDelay(unsigned ticks) { clock_us+=(int64_t)ticks*1000;if(drain_steps&&!--drain_steps)drained=true; }
int64_t esp_timer_get_time(void) { return clock_us; }
uint32_t esp_random(void) { static uint32_t next=1;return ++next; }
bool zynq_uart_bridge_available(void) { return true; }
void sdio_bridge_control_gate(bool b) { gate=b; }
bool sdio_bridge_control_drained(void) { return drained; }
cJSON *cJSON_AddObjectToObject(cJSON *j,const char *name) { assert(!strcmp(name,"zynq_control"));return j; }
void cJSON_AddBoolToObject(cJSON *j,const char *k,bool v) { (void)j;(void)k;(void)v; }
void cJSON_AddNumberToObject(cJSON *j,const char *k,double v) { (void)j;(void)k;(void)v; }
void cJSON_AddStringToObject(cJSON *j,const char *key,const char *v) { (void)j;(void)key;(void)v; }
esp_err_t zynq_uart_bridge_send(const char *data,size_t length) {
    (void)length;sends++;
    wc_message m;assert(wc_decode(data,false,&m));char result[128];
    if(!strcmp(m.text,"STATUS")) snprintf(result,sizeof(result),"@STATUS v=1 cfg=%u owner=2 seq=1 run=0 hv=%u down=%u afe=%s",ps_cfg,ps_hv,ps_down,ps_afe);
    else if(!strcmp(m.text,"AFE PREPARE")) {assert(gate);ps_hv=0;ps_afe="quiescing";power_prepares++;strcpy(result,"@ACK 0 AFE_PREPARE quiescing");}
    else if(!strcmp(m.text,"AFE SLEEP")) {assert(gate&&drained&&!ps_hv&&!host_idle_reads&&!strcmp(ps_afe,"quiescing"));ps_afe="sleep";power_sleeps++;strcpy(result,"@ACK 0 AFE_SLEEP sleep");}
    else if(!strcmp(m.text,"AFE WAKE")) {assert(gate&&drained);ps_afe="waking";wake_queries=2;strcpy(result,"@ACK 0 AFE_WAKE waking");}
    else if(!strcmp(m.text,"AFE STATUS")) {if(wake_queries && !--wake_queries)ps_afe="ready";unsigned idle_host=host_idle_reads?--host_idle_reads==0:1;snprintf(result,sizeof(result),"@AFE state=%s flags=0000001f idle=%u error=NONE",ps_afe,idle_host);}
    else if(!strncmp(m.text,"CFG BEGIN ",10)) { assert(gate&&drained);ps_cfg=1;ps_hv=0;strcpy(result,"@ACK 1 CFG_BEGIN"); }
    else if(!strcmp(m.text,"CFG END")||!strcmp(m.text,"CFG ABORT")) { ps_cfg=0;ps_hv=0;strcpy(result,"@ACK 1 CFG_END"); }
    else if(!strncmp(m.text,"AFE",3)&&nack_afe) strcpy(result,"@NACK 1 AFE_WR_FAIL");
    else if(!strncmp(m.text,"BRAM WR t_timing 9 1 ",21)) {
        assert(gate&&drained&&lease&&ps_cfg&&!ps_hv);
        if(nack_down) strcpy(result,"@NACK 1 BRAM_RANGE");
        else { assert(sscanf(m.text,"BRAM WR t_timing 9 1 %u",&ps_down)==1&&ps_down<=4);down_writes++;strcpy(result,"@ACK 1 BRAM_WR"); }
    }
    else { if(!strcmp(m.text,"HV ENABLE"))ps_hv=1;if(!strcmp(m.text,"HV DISABLE")){ps_hv=0;off_sends++;}strcpy(result,"@ACK 1 OK"); }
    if(drop_reply && strcmp(m.text,"STATUS") && strcmp(m.text,"HV DISABLE")) return ESP_OK;
    if(reset_during_reply && strcmp(m.text,"STATUS")) { zynq_control_uart_lost();return ESP_OK; }
    char line[256];int n;
    if(stale_reply) {
        n=wc_encode(line,sizeof(line),true,m.session,m.request+100,result);
        zynq_control_feed((const uint8_t*)line,(size_t)n);
    }
    n=wc_encode(line,sizeof(line),true,m.session,m.request,result);
    for(int i=0;i<n;i++) zynq_control_feed((const uint8_t*)line+i,1);
    return ESP_OK;
}
static void run_worker(void) { if(setjmp(idle)==0)control_task(NULL);assert(!held); }
static uint32_t submit(uint32_t client,uint32_t request,const char *a,const char *b) {
    zynq_control_job job={.client=client,.request=request,.count=b?2:1};strcpy(job.commands[0],a);if(b)strcpy(job.commands[1],b);
    uint32_t id;assert(zynq_control_submit(&job,true,&id)==ESP_OK);run_worker();return id;
}
int main(int argc,char **argv) {
    if(argc>1) { fault=argv[1];assert(zynq_control_start()!=ESP_OK);assert(!ready);return 0; }
    assert(!zynq_control_ready());
    assert(zynq_control_start()==ESP_OK&&zynq_control_ready());
    assert(zynq_control_start()==ESP_OK); /* Idempotent; no NVS or USB credential dependency. */
    stale_reply=true;uint32_t id=submit(1,1,"STATUS",NULL);stale_reply=false;
    zynq_control_job result;assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"succeeded"));
    zynq_control_job job={.client=1,.request=1,.count=1};strcpy(job.commands[0],"STATUS");uint32_t duplicate;
    unsigned before=sends;assert(zynq_control_submit(&job,false,&duplicate)==ESP_OK&&duplicate==id&&sends==before);
    strcpy(job.commands[0],"HV ENABLE");assert(zynq_control_submit(&job,false,&duplicate)==ESP_ERR_INVALID_ARG);
    drained=false;drain_steps=3;id=submit(1,2,"CFG BEGIN 1",NULL);assert(gate&&lease&&ps_cfg);
    job.client=2;job.request=2;strcpy(job.commands[0],"GAIN SET 2");assert(zynq_control_submit(&job,false,&duplicate)==ESP_ERR_TIMEOUT);
    id=submit(1,3,"AFE WR ADC 1 2","CFG END");assert(!gate&&!lease&&!ps_cfg&&!ps_hv);
    id=submit(1,4,"CFG BEGIN 2",NULL);nack_afe=true;
    id=submit(1,5,"AFE WR ADC 1 2","HV ENABLE");nack_afe=false;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&result.completed==1&&!lease&&!gate&&!ps_hv);
    drop_reply=true;id=submit(1,6,"GAIN SET 3",NULL);drop_reply=false;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"unknown"));
    (void)submit(1,700,"STATUS",NULL);
    reset_during_reply=true;id=submit(1,7,"GAIN SET 4",NULL);reset_during_reply=false;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"unknown"));
    (void)submit(1,701,"STATUS",NULL);
    id=submit(1,8,"CFG BEGIN 3",NULL);clock_us+=60000001;run_worker();assert(!lease&&!gate&&!ps_cfg);
    drained=false;id=submit(1,9,"CFG BEGIN 4",NULL);assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&gate);
    drained=true;id=submit(1,10,"STATUS",NULL);assert(!gate&&!lease);
    job.client=1;job.request=11;strcpy(job.commands[0],"HV ENABLE");
    assert(zynq_control_submit(&job,true,&id)==ESP_OK);
    job.client=2;job.request=12;strcpy(job.commands[0],"HV DISABLE");uint32_t urgent_id;
    assert(zynq_control_submit(&job,false,&urgent_id)==ESP_OK);
    assert(zynq_control_job_get(urgent_id,&result)&&result.busy&&!strcmp(result.state,"queued"));
    run_worker();assert(off_sends==1&&!ps_hv);
    assert(zynq_control_job_get(urgent_id,&result)&&!strcmp(result.state,"succeeded"));
    job.client=1;job.request=13;strcpy(job.commands[0],"GAIN SET 2");
    assert(zynq_control_submit(&job,false,&id)==ESP_OK);
    drop_reply=inject_off_when_wait=true;ps_hv=1;int64_t before_stop=clock_us;
    run_worker();drop_reply=false;
    assert(off_sends==2&&!ps_hv&&clock_us-before_stop<1000000);
    assert(zynq_control_job_get(injected_id,&result)&&!strcmp(result.state,"succeeded"));
    for(unsigned d=0;d<=4;d++) {
        memset(&job,0,sizeof(job));job.client=1;job.request=100+d;job.count=4;
        strcpy(job.commands[0],"CFG BEGIN 40");snprintf(job.commands[1],WC_COMMAND,"BRAM WR t_timing 9 1 %u",d);
        strcpy(job.commands[2],"CFG END");strcpy(job.commands[3],"STATUS");
        ps_hv=1;drained=false;drain_steps=2;
        assert(zynq_control_submit(&job,false,&id)==ESP_OK);run_worker();
        assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"succeeded")&&result.completed==4);
        assert(ps_down==d&&down_writes==d+1&&!ps_cfg&&!ps_hv&&!gate&&!lease);
    }
    job.request=110;drained=false;unsigned old_writes=down_writes;
    assert(zynq_control_submit(&job,false,&id)==ESP_OK);run_worker();
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&result.completed==1);
    assert(down_writes==old_writes&&gate);drained=true;
    (void)submit(1,111,"STATUS",NULL);assert(!gate);
    job.request=112;nack_down=true;
    assert(zynq_control_submit(&job,false,&id)==ESP_OK);run_worker();nack_down=false;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&result.completed==2);
    assert(down_writes==old_writes&&!ps_cfg&&!ps_hv&&!gate);
    drained=false;drain_steps=3;host_idle_reads=3;id=submit(1,200,"AFE SLEEP",NULL);
    assert(power_prepares==1&&power_sleeps==1&&gate&&!ps_hv&&!strcmp(ps_afe,"sleep"));
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"succeeded"));
    zynq_control_job again={.client=1,.request=200,.count=1};strcpy(again.commands[0],"AFE SLEEP");
    assert(zynq_control_submit(&again,false,&duplicate)==ESP_OK&&duplicate==id&&power_sleeps==1);
    memset(&job,0,sizeof(job));job.client=1;job.request=201;job.count=1;strcpy(job.commands[0],"HV ENABLE");
    assert(zynq_control_submit(&job,true,&id)==ESP_ERR_INVALID_STATE);
    id=submit(1,202,"AFE STATUS",NULL);assert(gate&&!strcmp(ps_afe,"sleep"));
    id=submit(1,203,"AFE WAKE",NULL);assert(!gate&&!ps_hv&&!strcmp(ps_afe,"ready"));
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"succeeded"));
    unsigned slept=power_sleeps;drained=false;drain_steps=0;
    id=submit(1,204,"AFE SLEEP",NULL);
    assert(power_sleeps==slept&&gate&&!ps_hv&&!strcmp(afe_state,"unknown"));
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"unknown"));
    assert(strstr(afe_error,"no SLEEP commit"));
    unsigned before_wake=sends;
    id=submit(1,205,"AFE WAKE",NULL);
    assert(sends==before_wake+1); /* handshake STATUS only, not a WAKE */
    assert(gate&&strstr(afe_error,"no WAKE sent")&&!strcmp(ps_afe,"quiescing"));
    drained=true;id=submit(1,206,"AFE WAKE",NULL);assert(!gate&&!ps_hv&&!strcmp(ps_afe,"ready"));
    cJSON json;zynq_control_status_json(&json);
    puts("PASS ESP worker: no NVS/USB credential dependency; bounded queue/segmented+late replies/dedup/owner/drain/timeout/reset/watchdog/cleanup");
    return 0;
}
