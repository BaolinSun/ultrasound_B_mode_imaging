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
static const char *ps_afe="ready",*ps_tx="ready";
static unsigned tx_prepares,tx_sleeps,tx_wake_queries;
static unsigned power_prepares,power_sleeps;
static unsigned wake_queries;
static unsigned host_idle_reads;
static unsigned cpu1_actions,ps_cpu1;
static bool nack_cpu1;
static const char *fail_command;
static bool reset_in_delay,off_in_delay;
static unsigned trace_count;
static char wire_trace[1024][WC_COMMAND];
static int64_t wire_time[1024];
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
void vTaskDelay(unsigned ticks) {
    clock_us+=(int64_t)ticks*1000;if(drain_steps&&!--drain_steps)drained=true;
    if(cpu0_sequence_active)assert(gate);
    if(cpu0_sequence_active&&!strcmp(cpu0_power_stage,"sleep_delay")) {
        if(reset_in_delay){reset_in_delay=false;zynq_control_uart_lost();}
        if(off_in_delay){off_in_delay=false;zynq_control_job j={.client=2,.request=9999,.count=1};strcpy(j.commands[0],"HV DISABLE");assert(zynq_control_submit(&j,false,&injected_id)==ESP_OK);}
    }
}
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
    assert(strncmp(m.text,"CPU0 ",5)); /* CPU0 commands terminate at ESP, never PS. */
    assert(trace_count<1024);strcpy(wire_trace[trace_count],m.text);wire_time[trace_count++]=clock_us;
    if(cpu0_sequence_active)assert(gate);
    if(fail_command&&!strcmp(m.text,fail_command))strcpy(result,"@NACK 0 INJECTED_POWER_FAILURE");
    else if(!strcmp(m.text,"STATUS")) snprintf(result,sizeof(result),"@STATUS v=1 cfg=%u owner=2 seq=1 run=0 hv=%u down=%u afe=%s tx=%s cpu1_disabled=%u",ps_cfg,ps_hv,ps_down,ps_afe,ps_tx,ps_cpu1);
    else if(!strcmp(m.text,"CPU1 DISABLE")){cpu1_actions++;if(nack_cpu1)strcpy(result,"@NACK 0 CPU1_RESET_TIMEOUT");else{ps_cpu1=1;strcpy(result,"@ACK 0 CPU1_DISABLE cpu1_disabled=1");}}
    else if(!strcmp(m.text,"TX7332 PREPARE")){assert(gate);ps_hv=0;ps_tx="quiescing";tx_prepares++;strcpy(result,"@ACK 0 TX_PREPARE quiescing");}
    else if(!strcmp(m.text,"TX7332 SLEEP")){assert(gate&&drained&&!ps_hv);ps_tx="sleep";tx_sleeps++;strcpy(result,"@ACK 0 TX_SLEEP sleep");}
    else if(!strcmp(m.text,"TX7332 WAKE")){assert(gate&&drained);ps_tx="waking";tx_wake_queries=2;strcpy(result,"@ACK 0 TX_WAKE waking");}
    else if(!strcmp(m.text,"TX7332 STATUS")){if(tx_wake_queries&&! --tx_wake_queries)ps_tx="ready";snprintf(result,sizeof(result),"@TX state=%s flags=%08x idle=1 error=NONE",ps_tx,!strcmp(ps_tx,"sleep")?15U:9U);}
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
static unsigned find_wire(unsigned start,const char *command) {
    for(unsigned i=start;i<trace_count;i++)if(!strcmp(wire_trace[i],command))return i;
    return trace_count;
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
    id=submit(1,300,"TX7332 SLEEP",NULL);assert(gate&&!strcmp(ps_tx,"sleep")&&!strcmp(ps_afe,"ready")&&tx_sleeps==1&&tx_prepares==1);
    id=submit(1,301,"AFE SLEEP",NULL);assert(gate&&!strcmp(ps_tx,"sleep")&&!strcmp(ps_afe,"sleep"));
    id=submit(1,302,"TX7332 WAKE",NULL);assert(gate&&!strcmp(ps_tx,"ready")&&!strcmp(ps_afe,"sleep"));
    id=submit(1,303,"AFE WAKE",NULL);assert(!gate&&!strcmp(ps_tx,"ready")&&!strcmp(ps_afe,"ready"));
    id=submit(1,304,"AFE SLEEP",NULL);
    id=submit(1,305,"TX7332 SLEEP",NULL);assert(gate&&!strcmp(ps_tx,"sleep")&&!strcmp(ps_afe,"sleep"));
    id=submit(1,306,"AFE WAKE",NULL);assert(gate&&!strcmp(ps_tx,"sleep")&&!strcmp(ps_afe,"ready"));
    id=submit(1,307,"TX7332 WAKE",NULL);assert(!gate&&!ps_hv);
    unsigned dispatched=sends;tx_observe("@TX state=ready flags=0000000f idle=1 error=NONE");assert(tx_blocked);
    job.request=400;job.count=1;strcpy(job.commands[0],"HV ENABLE");assert(zynq_control_submit(&job,true,&id)==ESP_ERR_INVALID_STATE&&sends==dispatched);
    tx_observe("@TX state=ready flags=00000009 idle=1 error=NONE");assert(!tx_blocked);
    strcpy(job.commands[0],"TX7332 PREPARE");assert(zynq_control_submit(&job,false,&id)==ESP_ERR_INVALID_ARG);
    assert(cpu1_actions==0); /* No shutdown during service initialization/handshake. */
    id=submit(1,500,"AFE SLEEP",NULL);id=submit(1,501,"TX7332 SLEEP",NULL);
    assert(gate&&!strcmp(ps_afe,"sleep")&&!strcmp(ps_tx,"sleep"));
    unsigned old_prepares=power_prepares+tx_prepares,old_sleep=power_sleeps+tx_sleeps;
    id=submit(1,502,"CPU1 DISABLE","STATUS");
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"succeeded")&&result.completed==2);
    assert(cpu1_actions==1&&ps_cpu1&&gate&&!ps_hv);
    assert(old_prepares==power_prepares+tx_prepares&&old_sleep==power_sleeps+tx_sleeps);
    zynq_control_job cpu_again={.client=1,.request=502,.count=2};strcpy(cpu_again.commands[0],"CPU1 DISABLE");strcpy(cpu_again.commands[1],"STATUS");
    assert(zynq_control_submit(&cpu_again,false,&duplicate)==ESP_OK&&duplicate==id&&cpu1_actions==1);
    nack_cpu1=true;unsigned before_nack=sends;
    id=submit(1,503,"CPU1 DISABLE","STATUS");nack_cpu1=false;
    /* This deterministic harness re-enters the worker with its one startup
     * STATUS handshake; the queued trailing STATUS must not be executed. */
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&result.completed==1&&sends==before_nack+2);
    id=submit(1,504,"AFE WAKE",NULL);id=submit(1,505,"TX7332 WAKE",NULL);
    id=submit(1,506,"CFG BEGIN 50",NULL);
    job.client=2;job.request=507;job.count=1;strcpy(job.commands[0],"CPU1 DISABLE");
    assert(zynq_control_submit(&job,false,&id)==ESP_ERR_TIMEOUT);
    id=submit(1,508,"CFG ABORT",NULL);
    unsigned begin=trace_count;
    id=submit(1,600,"CPU0 SLEEP",NULL);
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"succeeded")&&!cpu0_sequence_active&&gate);
    assert(!strcmp(ps_tx,"sleep")&&!strcmp(ps_afe,"sleep")&&!ps_hv&&!strcmp(cpu0_power_stage,"sleeping"));
    unsigned tx_done=find_wire(find_wire(begin,"TX7332 SLEEP"),"TX7332 STATUS"),afe_begin=find_wire(begin,"AFE PREPARE");
    assert(tx_done<afe_begin&&wire_time[afe_begin]-wire_time[tx_done]>=1000000);
    zynq_control_job combined_again={.client=1,.request=600,.count=1};strcpy(combined_again.commands[0],"CPU0 SLEEP");
    unsigned before_again=sends;assert(zynq_control_submit(&combined_again,false,&duplicate)==ESP_OK&&duplicate==id&&sends==before_again);
    begin=trace_count;id=submit(1,601,"CPU0 WAKE",NULL);
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"succeeded")&&!gate&&!ps_hv);
    assert(!strcmp(ps_afe,"ready")&&!strcmp(ps_tx,"ready")&&!strcmp(cpu0_power_stage,"awake"));
    unsigned tx_wake=find_wire(begin,"TX7332 WAKE"),afe_done=find_wire(begin,"AFE STATUS");
    while(find_wire(afe_done+1,"AFE STATUS")<tx_wake)afe_done=find_wire(afe_done+1,"AFE STATUS");
    assert(afe_done<tx_wake&&wire_time[tx_wake]-wire_time[afe_done]>=1000000);
    /* Repeated explicit operations remain safe; no automatic imaging/HV action. */
    id=submit(1,602,"CPU0 SLEEP",NULL);id=submit(1,603,"CPU0 SLEEP",NULL);
    id=submit(1,604,"CPU0 WAKE",NULL);assert(!ps_hv&&!gate);
    job.client=1;job.request=605;job.count=2;strcpy(job.commands[0],"CPU0 SLEEP");strcpy(job.commands[1],"STATUS");
    assert(zynq_control_submit(&job,false,&id)==ESP_ERR_INVALID_ARG);
    id=submit(1,606,"CFG BEGIN 60",NULL);job.count=1;job.request=607;
    assert(zynq_control_submit(&job,false,&id)==ESP_ERR_TIMEOUT);id=submit(1,608,"CFG ABORT",NULL);
    ps_cfg=1;begin=trace_count;id=submit(1,609,"CPU0 SLEEP",NULL);ps_cfg=0;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&find_wire(begin,"TX7332 PREPARE")==trace_count);
    fail_command="TX7332 PREPARE";begin=trace_count;id=submit(1,610,"CPU0 SLEEP",NULL);fail_command=NULL;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&find_wire(begin,"AFE PREPARE")==trace_count);
    fail_command="AFE PREPARE";id=submit(1,611,"CPU0 SLEEP",NULL);fail_command=NULL;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&!strcmp(ps_tx,"sleep")&&!ps_hv);
    id=submit(1,612,"CPU0 WAKE",NULL);
    id=submit(1,620,"CPU0 SLEEP",NULL);
    fail_command="AFE WAKE";begin=trace_count;id=submit(1,621,"CPU0 WAKE",NULL);fail_command=NULL;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&find_wire(begin,"TX7332 WAKE")==trace_count);
    fail_command="TX7332 WAKE";id=submit(1,622,"CPU0 WAKE",NULL);fail_command=NULL;
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"failed")&&!strcmp(ps_afe,"ready")&&!strcmp(ps_tx,"sleep")&&!ps_hv);
    id=submit(1,623,"CPU0 WAKE",NULL);
    off_in_delay=true;begin=trace_count;id=submit(1,613,"CPU0 SLEEP",NULL);
    assert(find_wire(begin,"AFE PREPARE")==trace_count&&!ps_hv);
    assert(zynq_control_job_get(injected_id,&result)&&!strcmp(result.state,"succeeded"));
    id=submit(1,614,"CPU0 WAKE",NULL);
    reset_in_delay=true;begin=trace_count;id=submit(1,615,"CPU0 SLEEP",NULL);
    assert(zynq_control_job_get(id,&result)&&!strcmp(result.state,"unknown")&&find_wire(begin,"AFE PREPARE")==trace_count&&gate&&!cpu0_sequence_active);
    cJSON json;zynq_control_status_json(&json);
    puts("PASS ESP worker: no NVS/USB credential dependency; bounded queue/segmented+late replies/dedup/owner/drain/timeout/reset/watchdog/cleanup");
    return 0;
}
