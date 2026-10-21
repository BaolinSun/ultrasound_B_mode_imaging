#include "psram_udp.h"
#include "config_store.h"
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"
#include "tcp_transfer.h"
#include "tcp_socket_tuning.h"
#include "tcp_memory_guard.h"
#include "lwip/tcp.h"
#include <fcntl.h>

typedef struct { void *base; size_t size; bool allocated_here; } buffer_t;
static buffer_t buffers[4];
static SemaphoreHandle_t lock;
static TaskHandle_t worker;
static uint8_t *staging;
static raw_session_t session;
static psram_udp_status_t status;
static bool cancel_requested;
static uint32_t link_generation, accepted_generation;
static uint64_t accepted_deadline;
static uint64_t accepted_at_us;
static struct sockaddr_in accepted_dest;
static bool accepted_framed;
static bridge_frame_t accepted_frame;
static bool accepted_tcp, close_requested, tcp_continuous;
/* Only upload_worker owns this persistent socket, including idle cleanup. */
static int tcp_socket=-1;
static struct sockaddr_in tcp_dest;
static uint32_t tcp_generation;
static uint64_t last_tcp_finished_us;
static uint32_t last_tcp_session;

typedef struct {
    int socket; uint32_t generation; struct sockaddr_in dest;
    unsigned fast_ready_count,heap_waits,adaptive_writes; uint64_t extra_yield_us;
} send_ctx_t;
static uint64_t now_ms(void *ctx) { (void)ctx; return esp_timer_get_time() / 1000ULL; }
static uint64_t now_us(void *ctx) { (void)ctx; return (uint64_t)esp_timer_get_time(); }
static void delay_ms(void *ctx, unsigned ms)
{
    (void)ctx;
    /* One extra tick avoids a sub-millisecond wait near a tick boundary. */
    vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

static raw_result_t abort_reason(void *opaque)
{
    send_ctx_t *ctx = opaque;
    xSemaphoreTake(lock, portMAX_DELAY);
    raw_result_t result = cancel_requested ? RAW_CANCELLED :
        (!status.connected || ctx->generation != link_generation ? RAW_LINK_DOWN : RAW_OK);
    xSemaphoreGive(lock);
    return result;
}

static int send_datagram(void *opaque, const uint8_t *data, size_t length)
{
    send_ctx_t *ctx = opaque;
    int sent = sendto(ctx->socket, data, length, 0,
                      (struct sockaddr *)&ctx->dest, sizeof(ctx->dest));
    if (sent == (int)length) return 0;
    int saved_errno = sent < 0 ? errno : EIO;
    xSemaphoreTake(lock, portMAX_DELAY);
    status.socket_errno = saved_errno;
    xSemaphoreGive(lock);
    return saved_errno == ENOMEM || saved_errno == ENOBUFS ||
           saved_errno == EAGAIN || saved_errno == EWOULDBLOCK ? 1 : -1;
}

static void progress(void *ctx, size_t sent, unsigned packets, unsigned retries)
{
    (void)ctx;
    xSemaphoreTake(lock, portMAX_DELAY);
    status.sent_bytes = sent;
    status.packets = packets;
    status.retries = retries;
    xSemaphoreGive(lock);
}

static void drop_tcp(void)
{
    if(tcp_socket>=0) { shutdown(tcp_socket,SHUT_RDWR); close(tcp_socket); tcp_socket=-1; }
    last_tcp_finished_us=0;last_tcp_session=0;
    xSemaphoreTake(lock,portMAX_DELAY);status.tcp_connected=false;xSemaphoreGive(lock);
}
static void tcp_progress(void *ctx,size_t sent,unsigned chunks,unsigned waits)
{
    (void)ctx;xSemaphoreTake(lock,portMAX_DELAY);
    status.sent_bytes=sent;status.tcp_chunks=chunks;status.tcp_waits=waits;
    xSemaphoreGive(lock);
}
static int tcp_io_result(send_ctx_t *ctx,int n)
{
    if(n>=0) {
        if(n>0)ctx->fast_ready_count=0; /* Progress ends a spurious-ready streak. */
        return n;
    }
    int saved=errno;
    xSemaphoreTake(lock,portMAX_DELAY);status.socket_errno=saved;xSemaphoreGive(lock);
    if(saved==ENOMEM || saved==ENOBUFS)return -3;
    return saved==EAGAIN || saved==EWOULDBLOCK || saved==EINTR ? -2:-1;
}
static int tcp_socket_write(void *p,const uint8_t *data,size_t n) {
    send_ctx_t *ctx=p;
    size_t allowed=tcp_memory_write_limit(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                                          n,CONFIG_PSRAM_TCP_HEAP_HEADROOM,TCP_MSS);
    if(!allowed) {
        ctx->heap_waits++;return -3;
    }
    if(allowed<n)ctx->adaptive_writes++;
    return tcp_io_result(ctx,send(ctx->socket,data,allowed,0));
}
static int tcp_read(void *p,uint8_t *data,size_t n) { return tcp_io_result(p,recv(((send_ctx_t*)p)->socket,data,n,0)); }
static void tcp_phase(void *ctx,tcp_phase_t phase)
{
    (void)ctx;xSemaphoreTake(lock,portMAX_DELAY);status.tcp_phase=phase;xSemaphoreGive(lock);
}
static void tcp_wait(void *p,bool reading,bool resource,unsigned ms)
{
    send_ctx_t *ctx=p;
    if(resource) { delay_ms(p,1);ctx->fast_ready_count=0;return; }
    int fd=ctx->socket;fd_set ready;FD_ZERO(&ready);FD_SET(fd,&ready);
    struct timeval t={.tv_sec=0,.tv_usec=(long)ms*1000};
    uint64_t start=now_us(NULL);
    int selected=select(fd+1,reading?&ready:NULL,reading?NULL:&ready,NULL,&t);
    /* Normal EAGAIN waits for socket readiness without an extra sleep.
     * Rare ready-but-no-progress loops still get a bounded CPU backoff. */
    if(selected>0 && now_us(NULL)-start<250)ctx->fast_ready_count++;
    else ctx->fast_ready_count=0;
    if(ctx->fast_ready_count>=8) {
        start=now_us(NULL);delay_ms(p,1);ctx->extra_yield_us+=now_us(NULL)-start;
        ctx->fast_ready_count=0;
    }
}
static raw_result_t connect_tcp(send_ctx_t *ctx,uint64_t deadline)
{
    if(tcp_socket>=0 && (tcp_generation!=ctx->generation || tcp_dest.sin_addr.s_addr!=ctx->dest.sin_addr.s_addr ||
                         tcp_dest.sin_port!=ctx->dest.sin_port)) drop_tcp();
    if(tcp_socket>=0) {
        uint8_t byte;int n=recv(tcp_socket,&byte,1,MSG_PEEK);
        if(n==0 || n>0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) {
            xSemaphoreTake(lock,portMAX_DELAY);status.tcp_error=n>0?TCP_ERROR_BAD_ACK:TCP_ERROR_CLOSED;xSemaphoreGive(lock);
            return RAW_SEND_ERROR; /* Never reconnect silently inside this test. */
        }
        ctx->socket=tcp_socket;return RAW_OK;
    }
    tcp_socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(tcp_socket<0)return RAW_SEND_ERROR;
    ctx->socket=tcp_socket;
    unsigned actual=0;
    int tuning_error=tcp_upload_set_send_buffer(tcp_socket,CONFIG_PSRAM_TCP_SEND_BYTES,&actual);
    if(tuning_error) { errno=tuning_error;return RAW_SEND_ERROR; }
    xSemaphoreTake(lock,portMAX_DELAY);status.tcp_send_buffer_actual=actual;xSemaphoreGive(lock);
    struct timeval timeout={.tv_sec=0,.tv_usec=100000};int yes=1;
    if(setsockopt(tcp_socket,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))<0 ||
       setsockopt(tcp_socket,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes))<0 ||
       fcntl(tcp_socket,F_SETFL,O_NONBLOCK)<0) return RAW_SEND_ERROR;
    int64_t started=esp_timer_get_time();
    int r=connect(tcp_socket,(struct sockaddr*)&ctx->dest,sizeof(ctx->dest));
    if(r<0 && errno!=EINPROGRESS && errno!=EALREADY && errno!=EWOULDBLOCK)return RAW_SEND_ERROR;
    uint64_t until=now_ms(NULL)+5000;if(until>deadline)until=deadline;
    while(r<0) {
        raw_result_t abort=abort_reason(ctx);if(abort!=RAW_OK)return abort;
        if(now_ms(NULL)>=until)return RAW_TIMEOUT;
        fd_set wr;FD_ZERO(&wr);FD_SET(tcp_socket,&wr);struct timeval t={.tv_sec=0,.tv_usec=100000};
        int selected=select(tcp_socket+1,NULL,&wr,NULL,&t);
        if(selected<0) { if(errno==EINTR)continue;return RAW_SEND_ERROR; }
        if(!selected)continue;
        int error=0;socklen_t n=sizeof(error);
        if(getsockopt(tcp_socket,SOL_SOCKET,SO_ERROR,&error,&n)<0)return RAW_SEND_ERROR;
        if(error) { errno=error;return RAW_SEND_ERROR; }r=0;
    }
    tcp_dest=ctx->dest;tcp_generation=ctx->generation;
    xSemaphoreTake(lock,portMAX_DELAY);status.tcp_connected=true;
    status.tcp_connect_us=(uint64_t)(esp_timer_get_time()-started);xSemaphoreGive(lock);
    return RAW_OK;
}

static void upload_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        xSemaphoreTake(lock, portMAX_DELAY);
        bool closing=close_requested;close_requested=false;
        bool have_job=session.busy;
        raw_session_t job = session;
        uint64_t deadline = accepted_deadline;
        uint64_t submitted_at = accepted_at_us;
        bool framed = accepted_framed;
        bool tcp=accepted_tcp;
        bridge_frame_t frame = accepted_frame;
        send_ctx_t ctx = {.socket = -1, .generation = accepted_generation,
                          .dest = accepted_dest};
        xSemaphoreGive(lock);
        if(closing)drop_tcp();
        if(!have_job) {
            /* A peer can exit while SDIO is receiving the next frame or while
             * a single-frame test is idle. Detect that without reconnecting. */
            if(tcp_socket>=0) {
                uint8_t byte;int n=recv(tcp_socket,&byte,1,MSG_PEEK);
                if(n>=0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) {
                    drop_tcp();xSemaphoreTake(lock,portMAX_DELAY);
                    /* A finite receiver normally closes after its successful
                     * single-frame ACK. Only an active continuous test fails. */
                    if(tcp_continuous) {
                        status.tcp_failed=true;status.tcp_error=n>0?TCP_ERROR_BAD_ACK:TCP_ERROR_CLOSED;
                    }
                    xSemaphoreGive(lock);
                }
            }
            continue;
        }
        raw_result_t result = abort_reason(&ctx);
        int64_t started=esp_timer_get_time();
        uint64_t idle_us=tcp && last_tcp_finished_us && last_tcp_session==frame.session_id ?
                         (uint64_t)started-last_tcp_finished_us:0;
        size_t sent = 0;
        tcp_metrics_t metrics={0};
        if(result==RAW_OK && tcp) {
            result=connect_tcp(&ctx,deadline);
            if(result==RAW_OK) {
                tcp_io_t io={.ctx=&ctx,.now_ms=now_ms,.now_us=now_us,.abort_reason=abort_reason,
                    .write=tcp_socket_write,.read=tcp_read,.wait=tcp_wait,.yield_ms=delay_ms,
                    .progress=tcp_progress,.phase=tcp_phase};
                result=tcp_frame_send(job.data,job.length,&frame,staging,deadline,&io,&metrics);
                sent=metrics.sent;
                xSemaphoreTake(lock,portMAX_DELAY);
                if(metrics.error)status.tcp_error=metrics.error;
                if(metrics.error==TCP_ERROR_BAD_ACK || metrics.error==TCP_ERROR_REJECTED ||
                   metrics.error==TCP_ERROR_ACK_TIMEOUT ||
                   (result==RAW_SEND_ERROR && sent==job.length))status.tcp_ack_errors++;
                xSemaphoreGive(lock);
            } else {
                int saved=result==RAW_TIMEOUT?ETIMEDOUT:errno;
                xSemaphoreTake(lock,portMAX_DELAY);
                if(result!=RAW_CANCELLED && result!=RAW_LINK_DOWN) {
                    status.socket_errno=saved;status.tcp_connect_errors++;
                    if(!status.tcp_error)status.tcp_error=TCP_ERROR_CONNECT;
                }
                xSemaphoreGive(lock);
            }
        } else if (result == RAW_OK) {
            ctx.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (ctx.socket < 0) {
                result = RAW_SEND_ERROR;
            } else {
                struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
                if (setsockopt(ctx.socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
                    result = RAW_SEND_ERROR;
            }
            if (result != RAW_OK) {
                int saved_errno = errno;
                xSemaphoreTake(lock, portMAX_DELAY);
                status.socket_errno = saved_errno;
                xSemaphoreGive(lock);
            } else {
                raw_io_t io = {&ctx, now_ms, abort_reason, send_datagram, delay_ms, progress};
                result = framed ? psram_framed_send(job.data, job.length, &frame, staging, deadline, &io, &sent) :
                    psram_raw_send(job.data, job.length, staging, deadline, &io, &sent);
            }
        }
        if(tcp && result!=RAW_OK)drop_tcp();
        if(!tcp && ctx.socket>=0)close(ctx.socket);
        raw_completion_t done = NULL;
        void *done_ctx = NULL;
        xSemaphoreTake(lock, portMAX_DELAY);
        status.result = result;
        if(tcp) {
            status.tcp_ack_us=metrics.ack_us;status.tcp_header_us=metrics.header_us;
            status.tcp_payload_us=metrics.payload_us;status.tcp_write_wait_us=metrics.write_wait_us;
            status.tcp_read_wait_us=metrics.read_wait_us;status.tcp_yield_us=metrics.yield_us+ctx.extra_yield_us;
            status.tcp_copy_us=metrics.copy_us;status.tcp_write_calls=metrics.write_calls;
            status.tcp_read_calls=metrics.read_calls;status.tcp_partial_writes=metrics.partial_writes;
            status.tcp_send_waits=metrics.send_waits;status.tcp_ack_waits=metrics.ack_waits;
            status.tcp_resource_waits=metrics.resource_waits;
            status.tcp_heap_guard_waits=ctx.heap_waits;
            status.tcp_adaptive_writes=ctx.adaptive_writes;
            status.tcp_queue_us=(uint64_t)started-submitted_at;
            status.tcp_idle_us=idle_us;
            status.tcp_metric_session=frame.session_id;status.tcp_metric_frame=frame.frame_id;
            status.tcp_metric_result=result;status.tcp_metric_bytes=sent;
            if(result==RAW_OK) {
                status.tcp_confirmed_frames++;
                last_tcp_finished_us=now_us(NULL);last_tcp_session=frame.session_id;
            }
            else if(result!=RAW_CANCELLED && result!=RAW_LINK_DOWN)status.tcp_failed=true;
        }
        status.sent_bytes = sent;
        status.elapsed_us=(uint64_t)(esp_timer_get_time()-started);
        status.busy = false;
        status.tcp_phase=TCP_PHASE_IDLE;
        bool completed = raw_finish(&session, &done, &done_ctx);
        xSemaphoreGive(lock);
        if (completed && done) done(result, sent, done_ctx);
    }
}

esp_err_t psram_udp_init(void)
{
    lock = xSemaphoreCreateMutex();
    staging = heap_caps_malloc(TCP_STAGE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lock || !staging) return ESP_ERR_NO_MEM;
    status.result = RAW_NOT_READY;
    status.tcp_send_buffer_actual=0;
    status.tcp_metric_result=RAW_NOT_READY;
    if (xTaskCreate(upload_worker, "psram_udp", 4096, NULL, 5, &worker) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t psram_udp_register_buffer(void *base, size_t capacity)
{
    uintptr_t start = (uintptr_t)base;
    if (!lock || !base || !capacity || capacity - 1 > UINTPTR_MAX - start ||
        !esp_ptr_external_ram(base) ||
        !esp_ptr_external_ram((void *)(start + capacity - 1))) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    int free_slot = -1;
    for (unsigned i = 0; i < 4; ++i) {
        if (!buffers[i].base) { free_slot = (int)i; continue; }
        uintptr_t other = (uintptr_t)buffers[i].base;
        if (start <= other ? other - start < capacity : start - other < buffers[i].size) {
            xSemaphoreGive(lock);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (free_slot >= 0) buffers[free_slot] = (buffer_t){base, capacity, false};
    xSemaphoreGive(lock);
    return free_slot >= 0 ? ESP_OK : ESP_ERR_NO_MEM;
}

void *psram_udp_alloc(size_t length)
{
    if (!lock || !length || length > CONFIG_PSRAM_UDP_MAX_BYTES) return NULL;
    void *base = heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!base) return NULL;
    if (psram_udp_register_buffer(base, length) != ESP_OK) { heap_caps_free(base); return NULL; }
    xSemaphoreTake(lock, portMAX_DELAY);
    for (unsigned i = 0; i < 4; ++i) if (buffers[i].base == base) buffers[i].allocated_here = true;
    xSemaphoreGive(lock);
    return base;
}

static esp_err_t remove_buffer(void *base, bool free_memory)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    for (unsigned i = 0; i < 4; ++i) {
        buffer_t *b = &buffers[i];
        if (!base || b->base != base) continue;
        if (session.busy && raw_span_inside((uintptr_t)session.data, session.length,
                                            (uintptr_t)b->base, b->size)) {
            xSemaphoreGive(lock); return ESP_ERR_INVALID_STATE;
        }
        if (free_memory != b->allocated_here) {
            xSemaphoreGive(lock); return ESP_ERR_INVALID_ARG;
        }
        *b = (buffer_t){0};
        /* Keep registry locked until free is finished to prevent re-registration. */
        if (free_memory) heap_caps_free(base);
        xSemaphoreGive(lock); return ESP_OK;
    }
    xSemaphoreGive(lock); return ESP_ERR_NOT_FOUND;
}
esp_err_t psram_udp_free(void *base) { return remove_buffer(base, true); }
esp_err_t psram_udp_unregister_buffer(void *base) { return remove_buffer(base, false); }

static esp_err_t submit(const void *data, size_t length, const bridge_frame_t *frame,bool tcp,
                       psram_udp_completion_cb_t done, void *user_ctx)
{
    if (!lock || !worker) return ESP_ERR_INVALID_STATE;
    if (frame && (!br_meta_valid(frame, frame->session_id, 1048576) || frame->frame_bytes != length))
        return ESP_ERR_INVALID_ARG;
    app_config_t config;
    config_store_get(&config);
    xSemaphoreTake(lock, portMAX_DELAY);
    if(tcp && status.tcp_failed) { xSemaphoreGive(lock);return ESP_ERR_INVALID_STATE; }
    bool valid = false;
    for (unsigned i = 0; i < 4; ++i)
        if (buffers[i].base && raw_span_inside((uintptr_t)data, length,
                         (uintptr_t)buffers[i].base, buffers[i].size)) valid = true;
    raw_result_t result = raw_reserve(&session, data, length, CONFIG_PSRAM_UDP_MAX_BYTES,
                                       valid, status.connected, config.target_ip[0] != 0,
                                       done, user_ctx);
    if (result == RAW_OK) {
        accepted_framed = frame != NULL;
        accepted_tcp=tcp;
        if (frame) accepted_frame = *frame;
        accepted_dest = (struct sockaddr_in){.sin_family = AF_INET,
                                             .sin_port = htons(config.target_port)};
        inet_pton(AF_INET, config.target_ip, &accepted_dest.sin_addr);
        accepted_generation = link_generation;
        accepted_deadline = now_ms(NULL) + CONFIG_PSRAM_UDP_TIMEOUT_MS;
        accepted_at_us = now_us(NULL);
        cancel_requested = false;
        status.busy=true;status.total_bytes=length;status.sent_bytes=0;status.result=RAW_BUSY;
        status.active_port=config.target_port;status.active_tcp=tcp;status.packets=0;status.retries=0;
        status.tcp_phase=tcp?TCP_PHASE_CONNECT:TCP_PHASE_IDLE;
        status.socket_errno=0;status.tcp_error=0;status.tcp_chunks=0;status.tcp_waits=0;
        status.tcp_connect_us=0; /* Retain last completed frame timings while busy. */
        memcpy(status.active_ip, config.target_ip, sizeof(status.active_ip));
        xTaskNotifyGive(worker);
    }
    xSemaphoreGive(lock);
    return result == RAW_OK ? ESP_OK : result == RAW_INVALID ? ESP_ERR_INVALID_ARG :
           result == RAW_BUSY ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
}
esp_err_t psram_udp_submit(const void *data, size_t length,
                          psram_udp_completion_cb_t done, void *ctx) {
    return submit(data,length,NULL,false,done,ctx);
}
esp_err_t psram_udp_submit_frame(const void *data,size_t length,const bridge_frame_t *frame,
                               psram_udp_completion_cb_t done,void *ctx) {
    if (!frame) return ESP_ERR_INVALID_ARG;
    return submit(data,length,frame,false,done,ctx);
}
esp_err_t psram_frame_submit(const void *p,size_t n,const bridge_frame_t *f,psram_udp_completion_cb_t cb,void *ctx) {
    if(!f)return ESP_ERR_INVALID_ARG;
    return submit(p,n,f,WIFI_FRAME_TRANSPORT==WIFI_FRAME_TCP,cb,ctx);
}
void psram_frame_prepare(bool continuous) {
    if(!lock || !worker)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(session.busy) { xSemaphoreGive(lock);return; }
    tcp_continuous=continuous;status.tcp_failed=false;close_requested=true;xSemaphoreGive(lock);
    xTaskNotifyGive(worker);
}
const char *psram_frame_transport(void) { return WIFI_FRAME_TRANSPORT==WIFI_FRAME_TCP?"tcp":"udp"; }
void psram_udp_stop(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (session.busy) cancel_requested = true;
    tcp_continuous=false;
    close_requested=true;
    xSemaphoreGive(lock);
    xTaskNotifyGive(worker);
}
void psram_udp_set_link(bool connected)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!connected) { ++link_generation;close_requested=true; }
    status.connected = connected;
    xSemaphoreGive(lock);
    if(!connected)xTaskNotifyGive(worker);
}
void psram_udp_get_status(psram_udp_status_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY); *out = status; xSemaphoreGive(lock);
}
