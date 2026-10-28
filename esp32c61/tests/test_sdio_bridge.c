/* Execute the real ESP bridge task one scheduler iteration at a time with
 * strict descriptor-ownership and async UDP test doubles. No radio emulation. */
#include "sdio_bridge.h"
#include "psram_udp.h"
#include "config_store.h"
#include "fake_sdk.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static jmp_buf scheduler;
static void (*entry)(void*);
static bool locked, connected=true, udp_busy;
static uint32_t notifications, random_id=10;
static int64_t clock_us;
static uint8_t regs[64],reference[BR_FRAME_BYTES];
typedef struct { uint8_t *data; bool loaded,filled; } descriptor_t;
static descriptor_t desc[BR_WINDOW_BLOCKS];
static unsigned descriptors, resets;
static size_t jobs;
static const uint8_t *job_data;
static size_t job_size;
static raw_completion_t completion;
static void *completion_ctx;
static const char *fault="";
static bool target_configured=true;
static unsigned wait_inject, wait_delivered;
static bool auto_stop_ack=true, stop_in_wait;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return !strcmp(fault,"mutex")?NULL:(void*)1; }
int xSemaphoreTake(SemaphoreHandle_t p,uint32_t t) { (void)p;(void)t;assert(!locked);locked=true;return 1; }
int xSemaphoreGive(SemaphoreHandle_t p) { (void)p;assert(locked);locked=false;return 1; }
int xTaskCreate(void(*fn)(void*),const char*n,unsigned s,void*c,unsigned p,TaskHandle_t*h) {
    (void)n;(void)s;(void)c;(void)p;if(!strcmp(fault,"task") || !strcmp(fault,"task-init"))return 0;entry=fn;*h=(void*)1;return 1;
}
int xTaskNotify(TaskHandle_t t,uint32_t bits,int op) { (void)t;(void)op;notifications|=bits;return 1; }
int xTaskNotifyWait(uint32_t a,uint32_t b,uint32_t*v,uint32_t t) {
    (void)a;(void)b;(void)t;*v=notifications;notifications=0;return *v!=0;
}
void vTaskDelay(unsigned n) { clock_us+=(int64_t)n*1000;assert(!locked);longjmp(scheduler,1); }
int64_t esp_timer_get_time(void) { return clock_us; }
uint32_t esp_random(void) { return ++random_id; }
void *heap_caps_malloc(size_t n,unsigned caps) { (void)caps;return !strcmp(fault,"dma")?NULL:malloc(n); }
void fake_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
esp_err_t sdio_slave_initialize(sdio_slave_config_t *c) { assert(c->recv_buffer_size==512);memset(regs,0,64);return (!strcmp(fault,"init") || !strcmp(fault,"task-init"))?ESP_ERR_NO_MEM:ESP_OK; }
esp_err_t sdio_slave_start(void) { return !strcmp(fault,"start")?ESP_ERR_INVALID_STATE:ESP_OK; }
void sdio_slave_stop(void) {}
void sdio_slave_deinit(void) { descriptors=0;memset(desc,0,sizeof(desc));resets++; }
sdio_slave_buf_handle_t sdio_slave_recv_register_buf(uint8_t*p) {
    if(!strcmp(fault,"register"))return NULL;
    assert(descriptors<BR_WINDOW_BLOCKS);descriptor_t*d=&desc[descriptors++];d->data=p;return d;
}
esp_err_t sdio_slave_recv_load_buf(sdio_slave_buf_handle_t h) {
    if(!strcmp(fault,"load"))return ESP_ERR_INVALID_STATE;
    descriptor_t*d=h;assert(!d->loaded);d->loaded=true;d->filled=false;return ESP_OK;
}
esp_err_t sdio_slave_recv(sdio_slave_buf_handle_t*h,uint8_t**p,size_t*n,uint32_t t) {
    if(t) assert(!locked); /* Event wait must never hold the control mutex. */
    if(!strcmp(fault,"recv"))return ESP_ERR_INVALID_STATE;
    for(unsigned i=0;i<descriptors;i++) if(desc[i].filled) {
        *h=&desc[i];*p=desc[i].data;*n=!strcmp(fault,"short")?511:512;desc[i].loaded=false;desc[i].filled=false;return ESP_OK;
    }
    if(t && wait_inject) {
        wait_inject=0; wait_delivered=512;
        if(stop_in_wait) {stop_in_wait=false;sdio_bridge_stop();}
        descriptor_t*d=&desc[0]; assert(d->loaded);
        memcpy(d->data,reference,512);d->loaded=false;
        *h=d;*p=d->data;*n=512;return ESP_OK;
    }
    if(t) { clock_us+=(int64_t)t*1000;longjmp(scheduler,1); }
    return ESP_ERR_TIMEOUT;
}
uint8_t sdio_slave_read_reg(unsigned p) { assert(p<64);return regs[p]; }
esp_err_t sdio_slave_write_reg(unsigned p,uint8_t v) { assert(p<64 && !(p>=28 && p<=31));regs[p]=v;if(auto_stop_ack && p==BR_STATUS && (v&BR_STOPPING))regs[BR_STOP_ACK]=regs[BR_STOP_REQ];return !strcmp(fault,"write")?ESP_ERR_INVALID_STATE:ESP_OK; }
void config_store_get(app_config_t*c) { memset(c,0,sizeof(*c));if(target_configured)strcpy(c->target_ip,"192.168.3.31");c->target_port=50001; }
void *psram_udp_alloc(size_t n) {
    static unsigned allocations;
    bool fail=!strcmp(fault,"pool") || (!strcmp(fault,"partial-pool") && allocations==0);
    allocations++;return fail?NULL:malloc(n);
}
void psram_udp_get_status(psram_udp_status_t*u) { memset(u,0,sizeof(*u));u->busy=udp_busy;u->connected=connected; }
void psram_udp_stop(void) {} /* Completion remains asynchronous until finish(). */
esp_err_t psram_udp_submit(const void*p,size_t n,raw_completion_t cb,void*ctx) {
    if(!strcmp(fault,"submit") || !connected)return ESP_ERR_INVALID_STATE;
    assert(!udp_busy);job_data=p;job_size=n;completion=cb;completion_ctx=ctx;udp_busy=true;jobs++;return ESP_OK;
}
esp_err_t psram_udp_submit_frame(const void*p,size_t n,const bridge_frame_t*m,raw_completion_t cb,void*ctx) {
    assert(br_crc32(0,p,n)==m->crc32);return psram_udp_submit(p,n,cb,ctx);
}
esp_err_t psram_frame_submit(const void*p,size_t n,const bridge_frame_t*m,raw_completion_t cb,void*ctx) {
    return psram_udp_submit_frame(p,n,m,cb,ctx);
}
void psram_frame_prepare(bool continuous) { (void)continuous; assert(!udp_busy); }
static void step(void) { if(!setjmp(scheduler)) entry(NULL); }
static sdio_bridge_status_t status(void) { sdio_bridge_status_t s;sdio_bridge_get_status(&s);return s; }
static void put32(unsigned p,uint32_t n) { for(unsigned i=0;i<4;i++)regs[p+i]=(uint8_t)(n>>(8*i)); }
static void finish(raw_result_t r) { assert(udp_busy);udp_busy=false;completion(r,r==RAW_OK?job_size:0,completion_ctx);step(); }
static void select_host(void) { regs[BR_HOST_MODE]=1;step(); }
static void begin(uint32_t frame,uint32_t checksum) {
    put32(BR_SESSION_ECHO,status().session);put32(BR_FRAME_ID,frame);
    put32(BR_LENGTH,BR_FRAME_BYTES);put32(BR_CRC,checksum);regs[BR_CMD]=BR_BEGIN;
    regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();
    assert(regs[BR_RESULT]==BR_ACCEPTED && regs[BR_CMD_SEQ]==regs[BR_CMD_ACK]);
}
static void batch(size_t offset,unsigned count,bool acknowledge,bool corrupt) {
    for(unsigned i=0;i<count;i++) {
        assert(desc[i].loaded && !desc[i].filled);
        memcpy(desc[i].data,reference+offset+i*512,512);
        if(corrupt && i==0) desc[i].data[0]^=1;
        desc[i].filled=true;
    }
    if(acknowledge) regs[BR_WINDOW_ACK]=regs[BR_GRANT];
    step();
}
static void frame(bool acknowledge_last,bool corrupt) {
    size_t initial=wait_delivered;wait_delivered=0;
    for(size_t off=initial;off<BR_FRAME_BYTES;) {
        unsigned n=regs[BR_CREDIT];
        if(off==512 && initial) n--;
        else assert(n==br_window(BR_FRAME_BYTES-off,16));
        batch(off,n,acknowledge_last || off+n*512<BR_FRAME_BYTES,corrupt && off==0);
        off+=n*512;
    }
}
int main(int argc,char **argv) {
    if(argc>1) {
        fault=argv[1];
        if(!strncmp(fault,"runtime-",8)) {
            const char *next=fault+8;fault="";assert(sdio_bridge_init()==ESP_OK);step();
            select_host();assert(sdio_bridge_start(0)==ESP_OK);step();
            regs[BR_CMD]=BR_ABORT;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();
            fault=next;step();assert(!status().ready && !status().running);return 0;
        }
        assert(sdio_bridge_init()!=ESP_OK);
        assert(!status().ready);
        assert(sdio_bridge_start(1)==ESP_ERR_INVALID_STATE);
        (void)sdio_bridge_selftest();sdio_bridge_stop();(void)sdio_bridge_selftest_size();
        if(!strcmp(fault,"mutex")) { assert(sdio_bridge_selftest_size()==0);sdio_bridge_stop(); }
        if(entry) { step();assert(sdio_bridge_start(1)==ESP_ERR_INVALID_STATE); }
        puts("PASS: initialization failure remains unavailable without aborting web service");
        return 0;
    }
    raw_pattern_fill(reference,sizeof(reference));uint32_t checksum=br_crc32(0,reference,sizeof(reference));
    random_id=UINT32_MAX;
    assert(sdio_bridge_init()==ESP_OK);step();assert(status().free_slots==2 && status().session==1);
    connected=false;assert(sdio_bridge_start(1)==ESP_ERR_INVALID_STATE);connected=true;
    target_configured=false;assert(sdio_bridge_start(1)==ESP_ERR_INVALID_STATE);target_configured=true;
    assert(sdio_bridge_start(2)==ESP_ERR_INVALID_ARG);
    assert(sdio_bridge_start(1)==ESP_ERR_TIMEOUT);select_host();
    /* Unknown commands and BEGIN while not armed never allocate a frame. */
    regs[BR_CMD]=99;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();assert(regs[BR_RESULT]==BR_REJECTED);
    regs[BR_CMD]=BR_BEGIN;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();assert(regs[BR_RESULT]==BR_REJECTED);
    random_id=UINT32_MAX;
    assert(sdio_bridge_start(1)==ESP_OK);step();wait_inject=1;begin(1,checksum);frame(true,false);
    assert(status().received_frames==1 && jobs==1 && !status().running);
    assert(status().last_received_frame==1 && status().last_received_bytes==BR_FRAME_BYTES);
    assert(!memcmp(job_data,reference,BR_FRAME_BYTES));
    assert(sdio_bridge_selftest()==ESP_ERR_TIMEOUT);
    finish(RAW_OK);assert(status().free_slots==2 && status().sent_frames==1);
    assert(sdio_bridge_selftest()==ESP_OK);assert(sdio_bridge_start(0)==ESP_ERR_TIMEOUT);
    assert(sdio_bridge_selftest()==ESP_ERR_TIMEOUT);
    assert(!memcmp(job_data,reference,BR_FRAME_BYTES));finish(RAW_OK);
    assert(sdio_bridge_start(0)==ESP_OK);step();begin(2,checksum);frame(true,false);
    assert(sdio_bridge_selftest()==ESP_ERR_TIMEOUT);
    const uint8_t*leased=job_data;
    begin(3,checksum);frame(true,false);
    assert(status().free_slots==0 && (regs[BR_STATUS]&BR_BACKPRESSURE));
    assert(sdio_bridge_selftest()==ESP_ERR_NO_MEM);
    regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();assert(regs[BR_RESULT]==BR_REJECTED);
    assert(!memcmp(leased,reference,BR_FRAME_BYTES));
    finish(RAW_OK);assert(udp_busy && jobs==4 && status().free_slots==1);
    begin(4,checksum);batch(0,16,true,false);
    regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();assert(regs[BR_RESULT]==BR_REJECTED);
    sdio_bridge_stop();step();
    assert(!status().running && status().host_sdio && status().stopping);
    assert(status().free_slots==1); /* SENDING lease cannot be returned yet. */
    assert(status().last_received_bytes==BR_FRAME_BYTES && status().last_received_frame==3); /* Stop keeps verified facts and the link. */
    finish(RAW_CANCELLED);assert(status().free_slots==2 && !status().stopping && status().host_sdio);
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();begin(5,checksum);
    batch(0,16,true,false);clock_us+=31000000;step();
    assert(!status().running && status().free_slots==2);
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();
    /* Metadata is not consumed until its commit sequence changes. */
    put32(BR_FRAME_ID,10);put32(BR_LENGTH,BR_FRAME_BYTES);put32(BR_CRC,checksum);
    put32(BR_SESSION_ECHO,status().session+1);regs[BR_CMD]=BR_BEGIN;
    uint8_t previous=regs[BR_CMD_ACK];step();assert(regs[BR_CMD_ACK]==previous);
    regs[BR_CMD_SEQ]=br_next_seq(previous);step();assert(regs[BR_RESULT]==BR_REJECTED);
    begin(10,checksum);uint8_t grant=regs[BR_GRANT];step();assert(regs[BR_GRANT]==grant);
    fault="short";batch(0,16,true,false);fault="";step();assert(!status().running);
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();fault="recv";step();fault="";step();
    assert(!status().running);
    select_host();assert(sdio_bridge_start(1)==ESP_OK);step();begin(11,checksum);
    fault="submit";frame(true,false);fault="";step();assert(!status().running && status().free_slots==2);
    fault="submit";assert(sdio_bridge_selftest()==ESP_ERR_INVALID_STATE);fault="";
    assert(!status().raw_busy);
    select_host();assert(sdio_bridge_start(1)==ESP_OK);step();begin(6,checksum);frame(true,true);step();
    assert(status().crc_errors==1 && status().free_slots==2 && !status().running);
    assert(!status().last_received_bytes && !status().last_received_frame);
    select_host();assert(sdio_bridge_start(1)==ESP_OK);step();begin(7,checksum);frame(false,false);
    clock_us+=31000000;step();assert(!status().running && resets>=4);finish(RAW_CANCELLED);
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();begin(8,checksum);
    connected=false;step();assert(!status().running && status().free_slots==2);
    connected=true;select_host();assert(sdio_bridge_start(0)==ESP_OK);step();begin(9,checksum);
    regs[BR_CMD]=BR_ABORT;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();step();
    assert(!status().running && status().free_slots==2);
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();begin(20,checksum);
    /* Reload failure and output-mode withdrawal both disarm the test. */
    fault="load";batch(0,16,true,false);fault="";step();assert(!status().running);
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();begin(21,checksum);
    regs[BR_HOST_MODE]=0;step();assert(!status().running);
    /* Queue two READY frames behind an unrelated sender, preserving order. */
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();udp_busy=true;
    begin(30,checksum);frame(true,false);begin(31,checksum);frame(true,false);
    udp_busy=false;assert(sdio_bridge_start(1)==ESP_ERR_TIMEOUT);step();assert(job_data && status().free_slots==0);
    finish(RAW_OK);assert(udp_busy);finish(RAW_OK);
    sdio_bridge_stop();step();
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();udp_busy=true;
    begin(32,checksum);frame(true,false);connected=false;step();
    assert(!status().running && status().free_slots==2);udp_busy=false;connected=true;
    sdio_bridge_stop();assert(sdio_bridge_start(1)==ESP_ERR_TIMEOUT);
    assert(sdio_bridge_selftest()==ESP_OK);finish(RAW_OK);step();
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();
    put32(BR_SESSION_ECHO,status().session);put32(BR_FRAME_ID,40);put32(BR_LENGTH,512);
    regs[BR_CMD]=BR_BEGIN;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();assert(regs[BR_RESULT]==BR_REJECTED);
    /* Unsolicited DMA data without an accepted frame resets the session. */
    memcpy(desc[0].data,reference,512);desc[0].filled=true;step();step();assert(!status().running);
    /* Control drain must finish the accepted frame, not cancel its lease. */
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();begin(50,checksum);
    unsigned old_resets=resets;
    sdio_bridge_control_gate(true);assert(!sdio_bridge_control_drained());
    assert(sdio_bridge_start(1)==ESP_ERR_INVALID_STATE);
    assert(sdio_bridge_selftest()!=ESP_OK);
    frame(true,false);step();assert(!status().running&&udp_busy);
    assert(!sdio_bridge_control_drained()&&resets==old_resets);
    finish(RAW_OK);assert(sdio_bridge_control_drained()&&resets==old_resets);
    sdio_bridge_control_gate(false);assert(sdio_bridge_start(1)==ESP_OK);
    sdio_bridge_stop();step();
    for(unsigned d=0;d<=4;d++) {
        size_t bytes=((6144U>>d)+8U)*128U;
        select_host();assert(sdio_bridge_start(1)==ESP_OK);step();
        put32(BR_SESSION_ECHO,status().session);put32(BR_FRAME_ID,100+d);
        put32(BR_LENGTH,(uint32_t)bytes);put32(BR_CRC,br_crc32(0,reference,bytes));
        regs[BR_CMD]=BR_BEGIN;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();
        assert(regs[BR_RESULT]==BR_ACCEPTED);
        /* BEGIN is not a verified received frame. */
        assert(status().last_received_frame!=100+d);
        for(size_t off=0;off<bytes;) {
            unsigned n=regs[BR_CREDIT];assert(n==br_window(bytes-off,16));
            batch(off,n,true,false);off+=n*512;
        }
        assert(job_size==bytes && status().frame_bytes==bytes && !status().running);
        assert(status().last_received_frame==100+d && status().last_received_bytes==bytes);
        assert(!memcmp(job_data,reference,bytes));finish(RAW_OK);
    }
    printf("PASS: real bridge task / 1538 blocks / ownership / control drain / callbacks / CRC / stop / timeout / reset / disconnect\n");
    return 0;
}
