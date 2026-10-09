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
static struct sockaddr_in accepted_dest;
static bool accepted_framed;
static bridge_frame_t accepted_frame;

typedef struct { int socket; uint32_t generation; struct sockaddr_in dest; } send_ctx_t;
static uint64_t now_ms(void *ctx) { (void)ctx; return esp_timer_get_time() / 1000ULL; }
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

static void upload_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(lock, portMAX_DELAY);
        raw_session_t job = session;
        uint64_t deadline = accepted_deadline;
        bool framed = accepted_framed;
        bridge_frame_t frame = accepted_frame;
        send_ctx_t ctx = {.socket = -1, .generation = accepted_generation,
                          .dest = accepted_dest};
        xSemaphoreGive(lock);
        raw_result_t result = abort_reason(&ctx);
        size_t sent = 0;
        if (result == RAW_OK) {
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
        if (ctx.socket >= 0) close(ctx.socket);
        raw_completion_t done = NULL;
        void *done_ctx = NULL;
        xSemaphoreTake(lock, portMAX_DELAY);
        status.result = result;
        status.sent_bytes = sent;
        status.busy = false;
        bool completed = raw_finish(&session, &done, &done_ctx);
        xSemaphoreGive(lock);
        if (completed && done) done(result, sent, done_ctx);
    }
}

esp_err_t psram_udp_init(void)
{
    lock = xSemaphoreCreateMutex();
    staging = heap_caps_malloc(RAW_UDP_BYTES + BR_UDP_HEADER, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!lock || !staging) return ESP_ERR_NO_MEM;
    status.result = RAW_NOT_READY;
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

static esp_err_t submit(const void *data, size_t length, const bridge_frame_t *frame,
                       psram_udp_completion_cb_t done, void *user_ctx)
{
    if (!lock || !worker) return ESP_ERR_INVALID_STATE;
    if (frame && (!br_meta_valid(frame, frame->session_id, 1048576) || frame->frame_bytes != length))
        return ESP_ERR_INVALID_ARG;
    app_config_t config;
    config_store_get(&config);
    xSemaphoreTake(lock, portMAX_DELAY);
    bool valid = false;
    for (unsigned i = 0; i < 4; ++i)
        if (buffers[i].base && raw_span_inside((uintptr_t)data, length,
                         (uintptr_t)buffers[i].base, buffers[i].size)) valid = true;
    raw_result_t result = raw_reserve(&session, data, length, CONFIG_PSRAM_UDP_MAX_BYTES,
                                       valid, status.connected, config.target_ip[0] != 0,
                                       done, user_ctx);
    if (result == RAW_OK) {
        accepted_framed = frame != NULL;
        if (frame) accepted_frame = *frame;
        accepted_dest = (struct sockaddr_in){.sin_family = AF_INET,
                                             .sin_port = htons(config.target_port)};
        inet_pton(AF_INET, config.target_ip, &accepted_dest.sin_addr);
        accepted_generation = link_generation;
        accepted_deadline = now_ms(NULL) + CONFIG_PSRAM_UDP_TIMEOUT_MS;
        cancel_requested = false;
        bool connected = status.connected;
        status = (psram_udp_status_t){.busy = true, .connected = connected,
                    .total_bytes = length, .result = RAW_BUSY, .active_port = config.target_port};
        memcpy(status.active_ip, config.target_ip, sizeof(status.active_ip));
        xTaskNotifyGive(worker);
    }
    xSemaphoreGive(lock);
    return result == RAW_OK ? ESP_OK : result == RAW_INVALID ? ESP_ERR_INVALID_ARG :
           result == RAW_BUSY ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
}
esp_err_t psram_udp_submit(const void *data, size_t length,
                          psram_udp_completion_cb_t done, void *ctx) {
    return submit(data,length,NULL,done,ctx);
}
esp_err_t psram_udp_submit_frame(const void *data,size_t length,const bridge_frame_t *frame,
                               psram_udp_completion_cb_t done,void *ctx) {
    if (!frame) return ESP_ERR_INVALID_ARG;
    return submit(data,length,frame,done,ctx);
}
void psram_udp_stop(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (session.busy) cancel_requested = true;
    xSemaphoreGive(lock);
}
void psram_udp_set_link(bool connected)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!connected) ++link_generation;
    status.connected = connected;
    xSemaphoreGive(lock);
}
void psram_udp_get_status(psram_udp_status_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY); *out = status; xSemaphoreGive(lock);
}
