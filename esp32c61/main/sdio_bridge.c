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
    raw_result_t result; size_t sent; bool cancel_expected; } slot_t;
static slot_t slots[2];
static uint8_t *dma[BR_WINDOW_BLOCKS];
static TaskHandle_t task;
static SemaphoreHandle_t mutex;
static sdio_bridge_status_t stats;
static bool driver_live, stop_requested, reset_requested;
static bool resetting, stop_needs_host, stop_host_done;
static uint8_t stop_sequence;
static int64_t stop_deadline;
static int64_t health_due;
static uint8_t health_heartbeat;
static const char *reset_reason="none";
static bool control_blocked;
static unsigned frames_left, window_size, window_received;
static uint8_t grant, command_ack;
static int receiving=-1;
static size_t offset;
static uint32_t crc;
static int64_t frame_start;
static uint64_t crc_us;
static int64_t last_progress;
static int64_t waiting_start;
static const char *TAG="sdio_bridge";
static void request_reset(const char *why) {
    if(!reset_requested && !resetting) {
        reset_reason=why;
        if(strcmp(why,"host_abort"))stats.errors++; /* Explicit boot handshake is not a data fault. */
    }
    reset_requested=true;
}
static uint8_t read8(unsigned p) { return sdio_slave_read_reg(p); }
static void write8(unsigned p,uint8_t v) {
    if(sdio_slave_write_reg(p,v)!=ESP_OK) { request_reset("register_write"); }
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
            else {
                stats.dropped_frames++;
                if(slots[i].result!=RAW_CANCELLED || !slots[i].cancel_expected) {
                    request_reset("sender_failure");
                }
            }
        } else if(slots[i].state==RAW_TEST) {
            stats.raw_busy=false;
            ESP_LOGI(TAG,"raw selftest result=%s bytes=%u",raw_result_name(slots[i].result),(unsigned)slots[i].sent);
        }
        slots[i].state=FREE;
        slots[i].cancel_expected=false;
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
    stats.last_received_bytes=0; stats.last_received_frame=0;
    last_progress=esp_timer_get_time();
    write8(BR_CMD,BR_NOP); write8(BR_CMD_SEQ,0); write8(BR_CMD_ACK,0);
    write8(BR_HOST_MODE,0); write8(BR_GRANT,0); write8(BR_WINDOW_ACK,0); write8(BR_CREDIT,0);
    write8(BR_HEALTH,0);write8(BR_HEARTBEAT,0);health_due=0;
    write8(BR_STOP_REQ,0); write8(BR_STOP_ACK,0);
    write8(BR_RESULT,BR_ACCEPTED); write32(BR_SESSION,stats.session);
    write8(BR_DEVICE,0xc6); write8(BR_PROTOCOL,BR_SDIO_VERSION);
    write8(BR_STATUS,BR_READY|BR_BACKPRESSURE);
    return reset_requested ? ESP_ERR_INVALID_STATE : ESP_OK;
}
static bool sender_owned(void) {
    for(unsigned i=0;i<2;i++) if(slots[i].state==SENDING || slots[i].state==RAW_TEST) return true;
    return false;
}
static void cancel_sender(void) {
    for(unsigned i=0;i<2;i++) if(slots[i].state==SENDING || slots[i].state==RAW_TEST)
        slots[i].cancel_expected=true;
    psram_udp_stop(); /* Async: neither busy=false nor cancel request releases a slot. */
}
static void discard_ready(void) {
    for(unsigned i=0;i<2;i++) if(slots[i].state==READY) {slots[i].state=FREE;stats.dropped_frames++;}
}
static void reset_link(void) {
    if(!resetting) {
        stats.running=false;stats.stopping=true;stats.host_sdio=false;stats.ready=false;
        cancel_sender();
        /* Genuine fault only. Deinit returns driver DMA ownership before
         * discarding RECEIVE storage. SENDING stays leased until its callback. */
        if(driver_live) {sdio_slave_stop();sdio_slave_deinit();driver_live=false;}
        if(receiving>=0) {slots[receiving].state=FREE;receiving=-1;stats.dropped_frames++;}
        discard_ready();resetting=true;
    }
    if(sender_owned()) return; /* Never rebuild in a loop while callbacks lag. */
    reset_requested=false;stop_requested=false;
    stats.ready=open_driver()==ESP_OK;
    if(!stats.ready && driver_live) {sdio_slave_stop();sdio_slave_deinit();driver_live=false;}
    /* Failed initialization remains unavailable; no tight automatic retry. */
    reset_requested=false;resetting=false;stats.stopping=false;
    stats.reset_reason=reset_reason;stats.reset_count++;
    ESP_LOGW(TAG,"SDIO reset reason=%s, new session=%" PRIu32 "; explicit start required",
        reset_reason,stats.session);
}
static void begin_stop(void) {
    stop_requested=false;stats.running=false;stats.stopping=true;
    stop_deadline=esp_timer_get_time()+BR_STOP_TIMEOUT_US;
    stop_needs_host=stats.host_sdio || receiving>=0 || window_size;stop_host_done=!stop_needs_host;
    cancel_sender();discard_ready();
    if(stop_needs_host) {
        stop_sequence=br_next_seq(stop_sequence);
        write8(BR_STOP_REQ,stop_sequence);
        /* No new credit; one CMD53 already issued by the host may still finish.
         * Keep the driver/DMA descriptors loaded until the host confirms idle. */
        write8(BR_STATUS,BR_READY|BR_BACKPRESSURE|BR_STOPPING);
    }
}
static void poll_stop(void) {
    if(stop_needs_host && read8(BR_STOP_ACK)==stop_sequence) stop_host_done=true;
    /* Consume/discard late in-flight blocks, returning every driver handle.
     * This is cancellation, not a short frame or CRC failure. */
    for(unsigned n=0;n<BR_WINDOW_BLOCKS;n++) {
        sdio_slave_buf_handle_t h=NULL;uint8_t *p=NULL;size_t length=0;
        esp_err_t r=sdio_slave_recv(&h,&p,&length,0);
        if(r==ESP_ERR_TIMEOUT) break;
        if(r!=ESP_OK) {request_reset("stop_receive");return;}
        if(sdio_slave_recv_load_buf(h)!=ESP_OK) {request_reset("stop_reload");return;}
    }
    if(stop_host_done) {
        if(receiving>=0) {slots[receiving].state=FREE;receiving=-1;stats.dropped_frames++;}
        window_size=window_received=0;offset=0;
        if(!sender_owned()) {
            /* Host has stopped committing BEGIN. Retire any raced BEGIN once,
             * including one committed just before it observed STOPPING. */
            command_ack=read8(BR_CMD_SEQ);write8(BR_CMD_ACK,command_ack);
            write8(BR_RESULT,BR_CANCELLED);write8(BR_CREDIT,0);
            grant=0;write8(BR_GRANT,0);write8(BR_WINDOW_ACK,0);
            write8(BR_STATUS,BR_READY|BR_BACKPRESSURE);
            if(!reset_requested) {
                stats.stopping=false;
                ESP_LOGI(TAG,"SDIO stopped; host idle and callbacks returned; session=%" PRIu32,stats.session);
            }
            return;
        }
    }
    if(esp_timer_get_time()>=stop_deadline) {
        request_reset(stop_host_done?"stop_callback_timeout":"stop_host_timeout");
    }
}
static void publish_window(void) {
    if(window_size) {
        if(window_received!=window_size || read8(BR_WINDOW_ACK)!=grant) return;
        window_size=0;
    }
    if(control_blocked && receiving<0) stats.running=false;
    bool blocked=!stats.running || (receiving<0 && free_slot()<0);
    if(blocked) {
        uint8_t previous=read8(BR_STATUS);
        if(stats.running && !(previous&BR_BACKPRESSURE)) stats.backpressure++;
        write8(BR_STATUS,BR_READY|(stats.running?BR_STREAM:0)|BR_BACKPRESSURE);
        return;
    }
    if(receiving<0) { write8(BR_STATUS,BR_READY|BR_STREAM); return; }
    window_size=br_window(slots[receiving].meta.frame_bytes-offset,BR_WINDOW_BLOCKS);
    window_received=0; grant=br_next_seq(grant);
    write8(BR_CREDIT,(uint8_t)window_size);
    write8(BR_STATUS,BR_READY|BR_STREAM);
    write8(BR_GRANT,grant); /* Commit marker after count/status. */
}
static void command(void) {
    uint8_t seq=read8(BR_CMD_SEQ);
    if(seq==command_ack) return;
    uint8_t op=read8(BR_CMD);
    if(op==BR_ABORT) { request_reset("host_abort"); return; }
    int index=free_slot();
    bridge_frame_t m={read32(BR_SESSION_ECHO),read32(BR_FRAME_ID),read32(BR_LENGTH),read32(BR_CRC)};
    if(op!=BR_BEGIN || control_blocked || !stats.running || stats.stopping || !stats.host_sdio || receiving>=0 || window_size || index<0 ||
       !br_meta_valid(&m,stats.session,BR_FRAME_BYTES) || !br_image_bytes_valid(m.frame_bytes)) {
        write8(BR_RESULT,BR_REJECTED);
    } else {
        receiving=index; slots[index].state=RECEIVING; slots[index].meta=m;
        stats.frame_bytes=m.frame_bytes;
        offset=0; crc=0; crc_us=0; frame_start=esp_timer_get_time();
        last_progress=frame_start;
        write8(BR_RESULT,BR_ACCEPTED);
    }
    command_ack=seq; write8(BR_CMD_ACK,seq);
}
static void consume(const uint8_t *p,size_t length) {
    if(receiving<0 || !window_size || window_received>=window_size || length!=BR_BLOCK_BYTES ||
       (receiving>=0 && offset+length>slots[receiving].meta.frame_bytes)) { request_reset("receive_bounds"); return; }
    slot_t *s=&slots[receiving];
    int64_t crc_start=esp_timer_get_time();
    crc=bridge_crc32_copy(crc,s->data+offset,p,length);
    crc_us+=(uint64_t)(esp_timer_get_time()-crc_start);
    offset+=length; window_received++;
    last_progress=esp_timer_get_time();
    if(offset==s->meta.frame_bytes) {
        stats.last_receive_us=(uint64_t)(esp_timer_get_time()-frame_start);
        stats.last_crc_us=crc_us;
        stats.last_copy_crc_us=crc_us;
        if(crc!=s->meta.crc32) {
            stats.crc_errors++; stats.dropped_frames++; s->state=FREE;
            write8(BR_RESULT,BR_BAD_CRC); request_reset("frame_crc");
        } else {
            stats.last_received_bytes=s->meta.frame_bytes;
            stats.last_received_frame=s->meta.frame_id;
            stats.received_frames++; s->state=READY; write8(BR_RESULT,BR_DONE);
            if(frames_left && --frames_left==0) stats.running=false;
            ESP_LOGD(TAG,"SDIO frame=%" PRIu32 " bytes=%u crc=%08" PRIx32 " elapsed_ms=%" PRIi64,
                s->meta.frame_id,(unsigned)s->meta.frame_bytes,crc,(esp_timer_get_time()-frame_start)/1000);
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
    slot_t *s=&slots[index]; s->state=SENDING;s->cancel_expected=false;
    esp_err_t r=psram_frame_submit(s->data,s->meta.frame_bytes,&s->meta,done,s);
    if(r!=ESP_OK) { s->state=FREE; stats.dropped_frames++; request_reset("submit_failure"); }
}
static void bridge_task(void *ctx) {
    (void)ctx;
    for(;;) {
        xSemaphoreTake(mutex,portMAX_DELAY);
        reap();
        psram_udp_status_t u; psram_udp_get_status(&u);
        bool pending=receiving>=0 || sender_owned();
        for(unsigned i=0;i<2;i++) pending|=slots[i].state==READY;
        if(resetting || reset_requested) reset_link();
        else if(stats.ready) {
            if(esp_timer_get_time()>=health_due) {
                health_due=esp_timer_get_time()+500000;
                health_heartbeat++;write8(BR_HEARTBEAT,health_heartbeat);
            }
            app_config_t health_config;config_store_get(&health_config);
            uint8_t health=0x80U | (u.connected?1U:0U) | (!u.tcp_failed?2U:0U) | (health_config.target_ip[0]?4U:0U);
            if(read8(BR_HEALTH)!=health)write8(BR_HEALTH,health);
            stats.source_flags=read8(BR_SOURCE_FLAGS);
            stats.source_down=read8(BR_SOURCE_DOWN);stats.source_slot=read8(BR_SOURCE_SLOT);
            stats.source_occupancy=read8(BR_SOURCE_OCCUPANCY);
            stats.source_drops=read32(BR_SOURCE_DROPS);stats.source_errors=read32(BR_SOURCE_ERRORS);
            stats.source_latest=read32(BR_SOURCE_LATEST);stats.source_published=read32(BR_SOURCE_PUBLISHED);
            stats.host_sdio=read8(BR_HOST_MODE)==1;
            /* Expected STOP/cancel is not an upload or handshake failure. */
            if(!stats.stopping && !stop_requested) {
                if(!u.connected && (stats.running || pending)) request_reset("wifi_disconnect");
                if(u.tcp_failed && (stats.running || pending)) request_reset("tcp_failure");
                if(!stats.host_sdio && (stats.running || pending)) request_reset("host_lost");
                if(stats.running && receiving<0 && ((stats.source_flags&12U) ||
                   (frames_left && esp_timer_get_time()-waiting_start>=30000000))) {
                    request_reset("source_unavailable");
                    ESP_LOGE(TAG,"No complete source frame / PL ABI error; explicit restart required");
                }
                if((receiving>=0 || window_size) && esp_timer_get_time()-frame_start>=30000000) {
                    request_reset("receive_timeout");
                }
            }
            if(reset_requested) reset_link();
            else {
                if(stop_requested && !stats.stopping) begin_stop();
                if(stats.stopping) {
                    if(read8(BR_CMD)==BR_ABORT && read8(BR_CMD_SEQ)!=command_ack)
                        request_reset("host_abort");
                    else poll_stop();
                } else {
                    publish_window(); /* Retire the previous window before BEGIN. */
                    command();
                    if(!reset_requested) {
                        for(unsigned n=0;n<BR_WINDOW_BLOCKS;n++) {
                            sdio_slave_buf_handle_t h=NULL;uint8_t *p=NULL;size_t len=0;
                            esp_err_t r=sdio_slave_recv(&h,&p,&len,0);
                            if(r==ESP_ERR_TIMEOUT) break;
                            if(r!=ESP_OK) {request_reset("receive_driver");break;}
                            consume(p,len);
                            if(sdio_slave_recv_load_buf(h)!=ESP_OK) {request_reset("reload_driver");}
                            if(reset_requested) break;
                        }
                        if(!reset_requested) {publish_window();upload_ready();}
                    }
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
        bool wait_rx=stats.ready && !stats.stopping && !stop_requested && !reset_requested &&
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
                if(!stats.stopping && !stop_requested && !reset_requested) consume(p,len);
                if(sdio_slave_recv_load_buf(h)!=ESP_OK) {request_reset("reload_wait");}
            } else if(r!=ESP_ERR_TIMEOUT) {request_reset("receive_wait");}
            xSemaphoreGive(mutex);
        } else {
            vTaskDelay(1); /* Idle/ACK/backpressure polls stay bounded and fair. */
        }
    }
}
esp_err_t sdio_bridge_init(void) {
    if(!bridge_crc_init()) return ESP_ERR_INVALID_STATE;
    stats.reset_reason="none";
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
    if(control_blocked || !stats.ready || !u.connected || !c.target_ip[0]) r=ESP_ERR_INVALID_STATE;
    else if(stats.stopping || resetting || !stats.host_sdio || stats.raw_busy || stats.running || u.busy ||
            receiving>=0 || window_size || slots[0].state!=FREE || slots[1].state!=FREE || stop_requested || reset_requested)
        r=ESP_ERR_TIMEOUT;
    else {
        psram_frame_prepare(frames==0);
        stats.session=esp_random(); if(!stats.session) stats.session=1;
        write8(BR_STREAM_KIND,(uint8_t)frames); write8(BR_SOURCE_FLAGS,0);
        write32(BR_SESSION,stats.session); frames_left=frames; stats.running=true;
        waiting_start=esp_timer_get_time();
        last_progress=esp_timer_get_time();
    }
    xSemaphoreGive(mutex); return r;
}
void sdio_bridge_stop(void) {
    if(!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(stats.ready && !stats.stopping &&
       (stats.running || receiving>=0 || window_size || sender_owned() ||
        slots[0].state==READY || slots[1].state==READY)) stop_requested=true;
    /* Mark before the async cancellation can deliver a callback. */
    for(unsigned i=0;i<2;i++) if(slots[i].state==SENDING || slots[i].state==RAW_TEST)
        slots[i].cancel_expected=true;
    stats.running=false;
    xSemaphoreGive(mutex);psram_udp_stop();
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
    else if(!control_blocked && !stats.stopping && !resetting && !stats.running && !stats.raw_busy && receiving<0 && !window_size && !stop_requested && !reset_requested &&
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
void sdio_bridge_control_gate(bool blocked) {
    if(!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY); control_blocked=blocked; xSemaphoreGive(mutex);
}
bool sdio_bridge_control_drained(void) {
    if(!mutex) return true;
    xSemaphoreTake(mutex,portMAX_DELAY);
    psram_udp_status_t u; psram_udp_get_status(&u);
    bool idle=!u.busy && !stats.stopping && !resetting && !stats.running && !stats.raw_busy && receiving<0 && !window_size &&
        slots[0].state==FREE && slots[1].state==FREE && !stop_requested && !reset_requested;
    xSemaphoreGive(mutex); return idle;
}
