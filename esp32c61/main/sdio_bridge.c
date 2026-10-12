#include "sdio_bridge.h"
#include "psram_udp.h"
#include "config_store.h"
#include "bridge_crc.h"
#include <string.h>
#include <inttypes.h>
#include "driver/sdio_slave.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum { FREE, RECEIVING, READY, SENDING, RAW_TEST };
typedef struct { uint8_t *data; unsigned state; bridge_frame_t meta;
    raw_result_t result; size_t sent; } slot_t;
static slot_t slots[2];
static uint8_t *dma[BR_WINDOW_BLOCKS];
static TaskHandle_t task;
static SemaphoreHandle_t mutex;
static sdio_bridge_status_t stats;
static bool driver_live, stop_requested, reset_requested;
static unsigned frames_left, window_size, window_received;
static uint8_t grant, command_ack;
static int receiving=-1;
static size_t offset;
static uint32_t crc;
static int64_t frame_start;
static uint64_t crc_us;
static int64_t last_progress;
static const char *TAG="sdio_bridge";
static uint8_t read8(unsigned p) { return sdio_slave_read_reg(p); }
static void write8(unsigned p,uint8_t v) {
    if(sdio_slave_write_reg(p,v)!=ESP_OK) { stats.errors++; reset_requested=true; }
}
static uint32_t read32(unsigned p) {
    uint32_t n=0; for(unsigned i=0;i<4;i++) n|=(uint32_t)read8(p+i)<<(8*i); return n;
}
static void write32(unsigned p,uint32_t n) { for(unsigned i=0;i<4;i++) write8(p+i,(uint8_t)(n>>(8*i))); }
static int free_slot(void) {
    for(unsigned i=0;i<2;i++) if(slots[i].data && slots[i].state==FREE) return (int)i;
    return -1;
}
static void done(raw_result_t result,size_t sent,void *ctx) {
    slot_t *s=ctx;
    /* Notify bits cannot overflow like a bounded queue. Only bridge_task
     * releases slots, after the sender has returned the PSRAM read lease. */
    s->result=result; s->sent=sent;
    xTaskNotify(task,1U<<(unsigned)(s-slots),eSetBits);
}
static void reap(void) {
    uint32_t bits=0;
    if(xTaskNotifyWait(0,UINT32_MAX,&bits,0)!=pdTRUE) return;
    for(unsigned i=0;i<2;i++) if(bits&(1U<<i)) {
        if(slots[i].state==SENDING) {
            if(slots[i].result==RAW_OK) { stats.sent_frames++; stats.sent_bytes+=slots[i].sent; }
            else { stats.dropped_frames++; stats.errors++;stop_requested=true; }
        } else if(slots[i].state==RAW_TEST) {
            stats.raw_busy=false;
            ESP_LOGI(TAG,"raw selftest result=%s bytes=%u",raw_result_name(slots[i].result),(unsigned)slots[i].sent);
        }
        slots[i].state=FREE;
    }
}
static esp_err_t open_driver(void) {
    sdio_slave_config_t cfg={.sending_mode=SDIO_SLAVE_SEND_PACKET,
        .send_queue_size=2,.recv_buffer_size=BR_BLOCK_BYTES};
    esp_err_t r=sdio_slave_initialize(&cfg);
    if(r!=ESP_OK) return r;
    driver_live=true;
    r=sdio_slave_start();
    if(r!=ESP_OK) return r;
    for(unsigned i=0;i<BR_WINDOW_BLOCKS;i++) {
        sdio_slave_buf_handle_t h=sdio_slave_recv_register_buf(dma[i]);
        if(!h) return ESP_ERR_NO_MEM;
        if((r=sdio_slave_recv_load_buf(h))!=ESP_OK) return r;
    }
    grant=0; command_ack=0; window_size=window_received=0;
    stats.session=esp_random(); if(!stats.session) stats.session=1;
    last_progress=esp_timer_get_time();
    write8(BR_CMD,BR_NOP); write8(BR_CMD_SEQ,0); write8(BR_CMD_ACK,0);
    write8(BR_HOST_MODE,0); write8(BR_GRANT,0); write8(BR_WINDOW_ACK,0); write8(BR_CREDIT,0);
    write8(BR_RESULT,BR_ACCEPTED); write32(BR_SESSION,stats.session);
    write8(BR_DEVICE,0xc6); write8(BR_PROTOCOL,BR_SDIO_VERSION);
    write8(BR_STATUS,BR_READY|BR_BACKPRESSURE);
    return reset_requested ? ESP_ERR_INVALID_STATE : ESP_OK;
}
static void reset_link(void) {
    stats.running=false; stats.host_sdio=false;
    if(receiving>=0) { slots[receiving].state=FREE; receiving=-1; stats.dropped_frames++; }
    for(unsigned i=0;i<2;i++) if(slots[i].state==READY) { slots[i].state=FREE; stats.dropped_frames++; }
    psram_udp_stop();
    /* Recreate descriptors: reset_hw alone does not return all registered
     * handles to application; loading still-owned handles is invalid. */
    if(driver_live) { sdio_slave_stop(); sdio_slave_deinit(); driver_live=false; }
    reset_requested=false;
    stats.ready=(open_driver()==ESP_OK);
    if(!stats.ready && driver_live) { sdio_slave_stop(); sdio_slave_deinit(); driver_live=false; }
    stop_requested=false;
    ESP_LOGW(TAG,"SDIO reset, new session=%" PRIu32 "; explicit start required",stats.session);
}
static void publish_window(void) {
    if(window_size) {
        if(window_received!=window_size || read8(BR_WINDOW_ACK)!=grant) return;
        window_size=0;
    }
    bool blocked=!stats.running || (receiving<0 && free_slot()<0);
    if(blocked) {
        uint8_t previous=read8(BR_STATUS);
        if(stats.running && !(previous&BR_BACKPRESSURE)) stats.backpressure++;
        write8(BR_STATUS,BR_READY|(stats.running?BR_STREAM:0)|BR_BACKPRESSURE);
        return;
    }
    if(receiving<0) { write8(BR_STATUS,BR_READY|BR_STREAM); return; }
    window_size=br_window(BR_FRAME_BYTES-offset,BR_WINDOW_BLOCKS);
    window_received=0; grant=br_next_seq(grant);
    write8(BR_CREDIT,(uint8_t)window_size);
    write8(BR_STATUS,BR_READY|BR_STREAM);
    write8(BR_GRANT,grant); /* Commit marker after count/status. */
}
static void command(void) {
    uint8_t seq=read8(BR_CMD_SEQ);
    if(seq==command_ack) return;
    uint8_t op=read8(BR_CMD);
    if(op==BR_ABORT) { reset_requested=true; return; }
    int index=free_slot();
    bridge_frame_t m={read32(BR_SESSION_ECHO),read32(BR_FRAME_ID),read32(BR_LENGTH),read32(BR_CRC)};
    if(op!=BR_BEGIN || !stats.running || !stats.host_sdio || receiving>=0 || window_size || index<0 ||
       !br_meta_valid(&m,stats.session,BR_FRAME_BYTES) || m.frame_bytes!=BR_FRAME_BYTES) {
        write8(BR_RESULT,BR_REJECTED);
    } else {
        receiving=index; slots[index].state=RECEIVING; slots[index].meta=m;
        offset=0; crc=0; crc_us=0; frame_start=esp_timer_get_time();
        last_progress=frame_start;
        write8(BR_RESULT,BR_ACCEPTED);
    }
    command_ack=seq; write8(BR_CMD_ACK,seq);
}
static void consume(const uint8_t *p,size_t length) {
    if(receiving<0 || !window_size || window_received>=window_size || length!=BR_BLOCK_BYTES ||
       offset+length>BR_FRAME_BYTES) { stats.errors++; reset_requested=true; return; }
    slot_t *s=&slots[receiving];
    memcpy(s->data+offset,p,length);
    int64_t crc_start=esp_timer_get_time();
    crc=bridge_crc32(crc,p,length);
    crc_us+=(uint64_t)(esp_timer_get_time()-crc_start);
    offset+=length; window_received++;
    last_progress=esp_timer_get_time();
    if(offset==BR_FRAME_BYTES) {
        stats.last_receive_us=(uint64_t)(esp_timer_get_time()-frame_start);
        stats.last_crc_us=crc_us;
        if(crc!=s->meta.crc32) {
            stats.crc_errors++; stats.dropped_frames++; s->state=FREE;
            write8(BR_RESULT,BR_BAD_CRC); reset_requested=true;
        } else {
            stats.received_frames++; s->state=READY; write8(BR_RESULT,BR_DONE);
            if(frames_left && --frames_left==0) stats.running=false;
            ESP_LOGD(TAG,"SDIO frame=%" PRIu32 " bytes=%u crc=%08" PRIx32 " elapsed_ms=%" PRIi64,
                s->meta.frame_id,BR_FRAME_BYTES,crc,(esp_timer_get_time()-frame_start)/1000);
        }
        receiving=-1;
    }
}
static void upload_ready(void) {
    psram_udp_status_t u; psram_udp_get_status(&u);
    if(u.busy) return;
    int index=-1;
    for(unsigned i=0;i<2;i++) if(slots[i].state==READY &&
        (index<0 || (int32_t)(slots[i].meta.frame_id-slots[index].meta.frame_id)<0)) index=(int)i;
    if(index<0) return;
    slot_t *s=&slots[index]; s->state=SENDING;
    esp_err_t r=psram_frame_submit(s->data,BR_FRAME_BYTES,&s->meta,done,s);
    if(r!=ESP_OK) { s->state=FREE; stats.dropped_frames++; stats.errors++; stop_requested=true; }
}
static void bridge_task(void *ctx) {
    (void)ctx;
    for(;;) {
        xSemaphoreTake(mutex,portMAX_DELAY);
        reap();
        psram_udp_status_t u; psram_udp_get_status(&u);
        bool pending=receiving>=0;
        for(unsigned i=0;i<2;i++) pending|=slots[i].state==READY || slots[i].state==SENDING;
        if(!u.connected && (stats.running || pending)) stop_requested=true;
        if(u.tcp_failed && (stats.running || pending))stop_requested=true;
        if(stats.ready) {
            stats.host_sdio=read8(BR_HOST_MODE)==1;
            if(!stats.host_sdio && (stats.running || pending)) stop_requested=true;
            if((receiving>=0 || window_size) && esp_timer_get_time()-frame_start>=30000000) {
                stats.errors++; reset_requested=true;
            }
            if(stop_requested || reset_requested) reset_link();
            else {
                publish_window(); /* Retire the previous window before BEGIN. */
                command();
                if(!reset_requested) {
                    for(unsigned n=0;n<BR_WINDOW_BLOCKS;n++) {
                        sdio_slave_buf_handle_t h=NULL; uint8_t *p=NULL; size_t len=0;
                        esp_err_t r=sdio_slave_recv(&h,&p,&len,0);
                        if(r==ESP_ERR_TIMEOUT) break;
                        if(r!=ESP_OK) { stats.errors++; reset_requested=true; break; }
                        consume(p,len);
                        if(sdio_slave_recv_load_buf(h)!=ESP_OK) { stats.errors++; reset_requested=true; }
                        if(reset_requested) break;
                    }
                    if(!reset_requested) { publish_window(); upload_ready(); }
                }
            }
        }
        stats.free_slots=0;
        for(unsigned i=0;i<2;i++) stats.free_slots+=slots[i].data && slots[i].state==FREE;
        stats.receive_offset=(uint32_t)offset;stats.receiving_slot=receiving;
        stats.current_frame=receiving>=0?slots[receiving].meta.frame_id:0;
        stats.window_blocks=window_size;stats.window_received=window_received;
        stats.grant=grant;stats.cmd_ack=command_ack;
        stats.progress_idle_us=stats.running?(uint64_t)(esp_timer_get_time()-last_progress):0;
        if(stats.ready) {
            stats.window_ack=read8(BR_WINDOW_ACK);stats.cmd_seq=read8(BR_CMD_SEQ);
            stats.command=read8(BR_CMD);stats.result=read8(BR_RESULT);stats.register_status=read8(BR_STATUS);
        }
        bool wait_rx=stats.ready && !stop_requested && !reset_requested &&
                     receiving>=0 && window_size && window_received<window_size;
        xSemaphoreGive(mutex);
        if(wait_rx) {
            /* Block on the DMA receive event, not a mandatory sleep between
             * every window. Never wait holding the web/control mutex. Only
             * this task resets/deinitializes the driver. A stop arriving here
             * suppresses consumption, but the returned descriptor is still
             * reloaded before reset on the next iteration. */
            sdio_slave_buf_handle_t h=NULL; uint8_t *p=NULL; size_t len=0;
            esp_err_t r=sdio_slave_recv(&h,&p,&len,1);
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(r==ESP_OK) {
                if(!stop_requested && !reset_requested) consume(p,len);
                if(sdio_slave_recv_load_buf(h)!=ESP_OK) { stats.errors++; reset_requested=true; }
            } else if(r!=ESP_ERR_TIMEOUT) { stats.errors++; reset_requested=true; }
            xSemaphoreGive(mutex);
        } else {
            vTaskDelay(1); /* Idle/ACK/backpressure polls stay bounded and fair. */
        }
    }
}
esp_err_t sdio_bridge_init(void) {
    if(!bridge_crc_init()) return ESP_ERR_INVALID_STATE;
    stats.crc_sram_table=bridge_crc_using_table();
    stats.crc_rom_bench_us=bridge_crc_benchmark_us(false);
    stats.crc_table_bench_us=bridge_crc_benchmark_us(true);
    if(CONFIG_PSRAM_UDP_TEST_BYTES>BR_FRAME_BYTES || BR_FRAME_BYTES>CONFIG_PSRAM_UDP_MAX_BYTES)
        return ESP_ERR_INVALID_SIZE;
    mutex=xSemaphoreCreateMutex(); if(!mutex) return ESP_ERR_NO_MEM;
    for(unsigned i=0;i<2;i++) slots[i].data=psram_udp_alloc(BR_FRAME_BYTES);
    bool allocated=slots[0].data && slots[1].data;
    for(unsigned i=0;i<BR_WINDOW_BLOCKS;i++) {
        dma[i]=heap_caps_malloc(BR_BLOCK_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
        allocated&=dma[i]!=NULL;
    }
    if(allocated) stats.ready=open_driver()==ESP_OK;
    if(!stats.ready && driver_live) { sdio_slave_stop(); sdio_slave_deinit(); driver_live=false; }
    if(xTaskCreate(bridge_task,"sdio_bridge",4096,NULL,5,&task)!=pdPASS) {
        if(driver_live) { sdio_slave_stop(); sdio_slave_deinit(); driver_live=false; }
        stats.ready=false; return ESP_ERR_NO_MEM;
    }
    return stats.ready?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t sdio_bridge_start(unsigned frames) {
    if(frames>1) return ESP_ERR_INVALID_ARG;
    if(!mutex || !task) return ESP_ERR_INVALID_STATE;
    app_config_t c; config_store_get(&c); psram_udp_status_t u; psram_udp_get_status(&u);
    xSemaphoreTake(mutex,portMAX_DELAY);
    esp_err_t r=ESP_OK;
    if(!stats.ready || !u.connected || !c.target_ip[0]) r=ESP_ERR_INVALID_STATE;
    else if(!stats.host_sdio || stats.raw_busy || stats.running || u.busy ||
            receiving>=0 || window_size || slots[0].state!=FREE || slots[1].state!=FREE || stop_requested || reset_requested)
        r=ESP_ERR_TIMEOUT;
    else {
        psram_frame_prepare(frames==0);
        stats.session=esp_random(); if(!stats.session) stats.session=1;
        write32(BR_SESSION,stats.session); frames_left=frames; stats.running=true;
        last_progress=esp_timer_get_time();
    }
    xSemaphoreGive(mutex); return r;
}
void sdio_bridge_stop(void) {
    if(!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(stats.ready) stop_requested=true;
    stats.running=false;
    xSemaphoreGive(mutex); psram_udp_stop();
}
void sdio_bridge_get_status(sdio_bridge_status_t *out) {
    if(!mutex) { memset(out,0,sizeof(*out)); return; }
    xSemaphoreTake(mutex,portMAX_DELAY); *out=stats; xSemaphoreGive(mutex);
}
esp_err_t sdio_bridge_selftest(void) {
    if(!mutex || !task) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mutex,portMAX_DELAY);
    int i=free_slot();
    esp_err_t r=ESP_ERR_TIMEOUT;
    if(i<0) r=ESP_ERR_NO_MEM;
    else if(!stats.running && !stats.raw_busy && receiving<0 && !window_size && !stop_requested && !reset_requested &&
            slots[0].state!=READY && slots[1].state!=READY && slots[0].state!=SENDING && slots[1].state!=SENDING) {
        slot_t *s=&slots[i]; s->state=RAW_TEST; stats.raw_busy=true;
        raw_pattern_fill(s->data,CONFIG_PSRAM_UDP_TEST_BYTES);
        r=psram_udp_submit(s->data,CONFIG_PSRAM_UDP_TEST_BYTES,done,s);
        if(r!=ESP_OK) { s->state=FREE; stats.raw_busy=false; }
    }
    xSemaphoreGive(mutex); return r;
}
size_t sdio_bridge_selftest_size(void) {
    return slots[0].data || slots[1].data ? CONFIG_PSRAM_UDP_TEST_BYTES : 0;
}
