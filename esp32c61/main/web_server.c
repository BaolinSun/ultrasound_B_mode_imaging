#include "web_server.h"
#include "config_store.h"
#include "psram_udp.h"
#include "tcp_transfer.h"
#include "selftest.h"
#include "sdio_bridge.h"
#include "temperature_cache.h"
#include "temperature_protocol.h"
#include "zynq_control.h"
#include "zynq_control_http.h"
#include "esp_timer.h"
#include "wifi_manager.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "sdkconfig.h"

extern const unsigned char html_start[] asm("_binary_index_html_start");
extern const unsigned char html_end[] asm("_binary_index_html_end");

static esp_err_t reply(httpd_req_t *req, const char *status, cJSON *json)
{
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}
static esp_err_t message(httpd_req_t *req, const char *status, const char *msg)
{
    cJSON *json = cJSON_CreateObject();
    if (json) cJSON_AddStringToObject(json, "message", msg);
    return reply(req, status, json);
}
static esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)html_start, html_end - html_start);
}

static esp_err_t get_config(httpd_req_t *req)
{
    app_config_t config;
    config_store_get(&config);
    cJSON *json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "ssid", config.ssid);
        cJSON_AddBoolToObject(json, "has_password", config.password[0] != 0);
        cJSON_AddStringToObject(json, "target_ip", config.target_ip);
        cJSON_AddNumberToObject(json, "target_port", config.target_port);
    }
    return reply(req, "200 OK", json);
}

static bool copy_json_string(cJSON *json, const char *key, char *dest, size_t capacity,
                              bool optional)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!value) return optional;
    if (!cJSON_IsString(value) || strlen(value->valuestring) >= capacity) return false;
    strcpy(dest, value->valuestring);
    return true;
}

static esp_err_t put_config(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 768)
        return message(req, "400 Bad Request", "配置 JSON 长度必须为 1..768 字节");
    char body[769];
    size_t received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return message(req, "408 Request Timeout", "配置接收不完整");
        received += (size_t)count;
    }
    body[received] = 0;
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return message(req, "400 Bad Request", "需要有效 JSON 对象");
    }
    app_config_t previous, candidate;
    config_store_get(&previous);
    candidate = previous;
    cJSON *port = cJSON_GetObjectItemCaseSensitive(json, "target_port");
    bool valid = copy_json_string(json, "ssid", candidate.ssid, sizeof(candidate.ssid), false) &&
                 copy_json_string(json, "password", candidate.password, sizeof(candidate.password), true) &&
                 copy_json_string(json, "target_ip", candidate.target_ip, sizeof(candidate.target_ip), false) &&
                 cJSON_IsNumber(port) && port->valuedouble >= 1 && port->valuedouble <= 65535 &&
                 port->valuedouble == port->valueint;
    if (valid) candidate.target_port = (uint16_t)port->valueint;
    cJSON_Delete(json);
    if (!valid || !config_valid(&candidate))
        return message(req, "400 Bad Request", "检查 SSID(最多32字节)、密码、单播IPv4与端口(1..65535)");
    esp_err_t err = config_store_save(&candidate);
    if (err != ESP_OK) return message(req, "500 Internal Server Error", "NVS 保存失败，当前配置未改变");
    bool wifi_changed = strcmp(candidate.ssid, previous.ssid) != 0 ||
                        strcmp(candidate.password, previous.password) != 0;
    if (wifi_changed) { sdio_bridge_stop(); psram_udp_set_link(false); }
    esp_err_t response = message(req, "200 OK", wifi_changed ?
        "配置已保存，正在连接新 Wi-Fi；断开热点后请使用设备局域网 IP 访问" :
        "配置已保存，目标地址将在下一次上传生效");
    if (wifi_changed) wifi_manager_reconfigure();
    return response;
}

static esp_err_t get_status(httpd_req_t *req)
{
    wifi_manager_status_t wifi;
    psram_udp_status_t udp;
    app_config_t config;
    wifi_manager_get_status(&wifi);
    psram_udp_get_status(&udp);
    config_store_get(&config);
    cJSON *json = cJSON_CreateObject();
    if (json) {
        zynq_control_status_json(json);
        temperature_status_t t;
        temperature_cache_get(&t,(uint64_t)esp_timer_get_time()/1000U);
        cJSON *temperatures=cJSON_AddObjectToObject(json,"temperatures");
        if(temperatures) {
            cJSON_AddNumberToObject(temperatures,"period_ms",TEMP_PERIOD_MS);
            if(t.has_sample) {
                cJSON_AddNumberToObject(temperatures,"seq",t.seq);
                cJSON_AddNumberToObject(temperatures,"age_ms",(double)t.age_ms);
            } else {
                cJSON_AddNullToObject(temperatures,"seq");
                cJSON_AddNullToObject(temperatures,"age_ms");
            }
            const char *names[]={"zynq","power","afe"};
            for(unsigned i=0;i<3;i++) {
                cJSON *channel=cJSON_AddObjectToObject(temperatures,names[i]);
                if(!channel) continue;
                const char *state=temperature_channel_status(&t,i);
                cJSON_AddStringToObject(channel,"status",state);
                if(!strcmp(state,"ok")) cJSON_AddNumberToObject(channel,"celsius",t.mc[i]/1000.0);
                else cJSON_AddNullToObject(channel,"celsius");
            }
        }
        cJSON_AddBoolToObject(json, "connected", wifi.connected);
        cJSON_AddBoolToObject(json, "upload_ready", udp.connected && config.target_ip[0] != 0);
        cJSON_AddBoolToObject(json, "ap_active", wifi.ap_active);
        cJSON_AddBoolToObject(json, "reconfiguring", wifi.reconfiguring);
        cJSON_AddStringToObject(json, "sta_ip", wifi.sta_ip);
        cJSON_AddStringToObject(json, "ap_ssid", wifi.ap_ssid);
        cJSON_AddNumberToObject(json, "psram_total", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        cJSON_AddNumberToObject(json, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        cJSON_AddNumberToObject(json, "psram_largest", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        cJSON_AddNumberToObject(json, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        cJSON_AddNumberToObject(json, "internal_min_free", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        cJSON_AddNumberToObject(json, "selftest_bytes", selftest_size());
        cJSON_AddNumberToObject(json, "max_bytes", CONFIG_PSRAM_UDP_MAX_BYTES);
        cJSON_AddBoolToObject(json, "busy", udp.busy);
        cJSON_AddStringToObject(json, "result", udp.active_tcp && udp.result==RAW_OK ? "received_verified":raw_result_name(udp.result));
        cJSON_AddStringToObject(json,"sdio_transport",psram_frame_transport());
        cJSON_AddStringToObject(json,"active_transport",udp.active_tcp?"tcp":"udp");
        cJSON_AddBoolToObject(json,"tcp_connected",udp.tcp_connected);
        cJSON_AddBoolToObject(json,"tcp_failed",udp.tcp_failed);
        cJSON_AddNumberToObject(json,"tcp_confirmed_frames",udp.tcp_confirmed_frames);
        cJSON_AddNumberToObject(json,"tcp_connect_errors",udp.tcp_connect_errors);
        cJSON_AddNumberToObject(json,"tcp_ack_errors",udp.tcp_ack_errors);
        cJSON_AddNumberToObject(json,"tcp_error",udp.tcp_error);
        cJSON_AddStringToObject(json,"tcp_error_reason",tcp_error_name(udp.tcp_error));
        cJSON_AddNumberToObject(json,"tcp_chunks",udp.tcp_chunks);
        cJSON_AddNumberToObject(json,"tcp_waits",udp.tcp_waits);
        cJSON_AddNumberToObject(json,"tcp_connect_us",udp.tcp_connect_us);
        cJSON_AddNumberToObject(json,"tcp_ack_us",udp.tcp_ack_us);
        cJSON_AddStringToObject(json,"tcp_phase",tcp_phase_name(udp.tcp_phase));
        cJSON_AddNumberToObject(json,"tcp_send_buffer",CONFIG_PSRAM_TCP_SEND_BYTES);
        cJSON_AddNumberToObject(json,"tcp_send_buffer_actual",udp.tcp_send_buffer_actual);
        cJSON_AddNumberToObject(json,"tcp_default_send_buffer",CONFIG_LWIP_TCP_SND_BUF_DEFAULT);
        cJSON_AddNumberToObject(json,"tcp_heap_guard_waits",udp.tcp_heap_guard_waits);
        cJSON_AddNumberToObject(json,"tcp_adaptive_writes",udp.tcp_adaptive_writes);
        cJSON_AddNumberToObject(json,"tcp_cooperative_waits",udp.tcp_cooperative_waits);
        cJSON_AddNumberToObject(json,"tcp_queue_us",udp.tcp_queue_us);
        cJSON_AddNumberToObject(json,"tcp_idle_us",udp.tcp_idle_us);
        cJSON_AddNumberToObject(json,"tcp_heap_headroom",CONFIG_PSRAM_TCP_HEAP_HEADROOM);
        cJSON_AddNumberToObject(json,"tcp_receive_window",CONFIG_LWIP_TCP_WND_DEFAULT);
        cJSON_AddNumberToObject(json,"tcp_stage_bytes",TCP_STAGE_BYTES);
        cJSON_AddNumberToObject(json,"tcp_staging_alloc_bytes",TCP_STAGING_ALLOC_BYTES);
        cJSON_AddBoolToObject(json,"tcp_direct_psram",TCP_DIRECT_PSRAM);
#ifdef CONFIG_LWIP_TCPIP_CORE_LOCKING
        cJSON_AddBoolToObject(json,"lwip_core_locking",true);
#else
        cJSON_AddBoolToObject(json,"lwip_core_locking",false);
#endif
#ifdef CONFIG_LWIP_TCPIP_CORE_LOCKING_INPUT
        cJSON_AddBoolToObject(json,"lwip_core_locking_input",true);
#else
        cJSON_AddBoolToObject(json,"lwip_core_locking_input",false);
#endif
        cJSON_AddNumberToObject(json,"psram_speed_mhz",CONFIG_SPIRAM_SPEED);
        cJSON_AddNumberToObject(json,"tcp_metric_session",udp.tcp_metric_session);
        cJSON_AddNumberToObject(json,"tcp_metric_frame",udp.tcp_metric_frame);
        cJSON_AddNumberToObject(json,"tcp_metric_bytes",udp.tcp_metric_bytes);
        cJSON_AddStringToObject(json,"tcp_metric_result",udp.tcp_metric_result==RAW_OK?"received_verified":raw_result_name(udp.tcp_metric_result));
        cJSON_AddNumberToObject(json,"tcp_header_us",udp.tcp_header_us);
        cJSON_AddNumberToObject(json,"tcp_payload_us",udp.tcp_payload_us);
        cJSON_AddNumberToObject(json,"tcp_write_wait_us",udp.tcp_write_wait_us);
        cJSON_AddNumberToObject(json,"tcp_read_wait_us",udp.tcp_read_wait_us);
        cJSON_AddNumberToObject(json,"tcp_yield_us",udp.tcp_yield_us);
        cJSON_AddNumberToObject(json,"tcp_copy_us",udp.tcp_copy_us);
        cJSON_AddNumberToObject(json,"tcp_staged_bytes",udp.tcp_staged_bytes);
        cJSON_AddNumberToObject(json,"tcp_direct_bytes",udp.tcp_direct_bytes);
        cJSON_AddNumberToObject(json,"tcp_task_stack_min_bytes",udp.tcp_task_stack_min_bytes);
        cJSON_AddNumberToObject(json,"tcp_write_calls",udp.tcp_write_calls);
        cJSON_AddNumberToObject(json,"tcp_read_calls",udp.tcp_read_calls);
        cJSON_AddNumberToObject(json,"tcp_partial_writes",udp.tcp_partial_writes);
        cJSON_AddNumberToObject(json,"tcp_send_waits",udp.tcp_send_waits);
        cJSON_AddNumberToObject(json,"tcp_ack_waits",udp.tcp_ack_waits);
        cJSON_AddNumberToObject(json,"tcp_resource_waits",udp.tcp_resource_waits);
        cJSON_AddNumberToObject(json, "total_bytes", udp.total_bytes);
        cJSON_AddNumberToObject(json, "sent_bytes", udp.sent_bytes);
        cJSON_AddNumberToObject(json, "send_elapsed_us", udp.elapsed_us);
        cJSON_AddNumberToObject(json, "packets", udp.packets);
        cJSON_AddNumberToObject(json, "retries", udp.retries);
        cJSON_AddNumberToObject(json, "socket_errno", udp.socket_errno);
        cJSON_AddStringToObject(json, "active_ip", udp.active_ip);
        cJSON_AddNumberToObject(json, "active_port", udp.active_port);
        sdio_bridge_status_t s;
        sdio_bridge_get_status(&s);
        cJSON *sdio = cJSON_AddObjectToObject(json, "sdio");
        if (sdio) {
            cJSON_AddBoolToObject(sdio,"ready",s.ready);
            cJSON_AddBoolToObject(sdio,"running",s.running);
            cJSON_AddBoolToObject(sdio,"host_sdio",s.host_sdio);
            cJSON_AddBoolToObject(sdio,"raw_busy",s.raw_busy);
            cJSON_AddNumberToObject(sdio,"session",s.session);
            cJSON_AddNumberToObject(sdio,"received_frames",s.received_frames);
            cJSON_AddNumberToObject(sdio,"crc_errors",s.crc_errors);
            cJSON_AddNumberToObject(sdio,"errors",s.errors);
            cJSON_AddNumberToObject(sdio,"backpressure",s.backpressure);
            cJSON_AddNumberToObject(sdio,"sent_frames",s.sent_frames);
            cJSON_AddNumberToObject(sdio,"sent_bytes",s.sent_bytes);
            cJSON_AddNumberToObject(sdio,"dropped_frames",s.dropped_frames);
            cJSON_AddNumberToObject(sdio,"free_slots",s.free_slots);
            cJSON_AddNumberToObject(sdio,"receive_offset",s.receive_offset);
            cJSON_AddNumberToObject(sdio,"frame_bytes",s.frame_bytes);
            cJSON_AddNumberToObject(sdio,"last_received_bytes",s.last_received_bytes);
            cJSON_AddNumberToObject(sdio,"last_received_frame",s.last_received_frame);
            cJSON_AddNumberToObject(sdio,"source_flags",s.source_flags);
            cJSON_AddNumberToObject(sdio,"source_drops",s.source_drops);
            cJSON_AddNumberToObject(sdio,"source_errors",s.source_errors);
            cJSON_AddNumberToObject(sdio,"source_latest",s.source_latest);
            cJSON_AddNumberToObject(sdio,"source_published",s.source_published);
            cJSON_AddNumberToObject(sdio,"source_down",s.source_down);
            cJSON_AddNumberToObject(sdio,"source_slot",s.source_slot);
            cJSON_AddNumberToObject(sdio,"source_occupancy",s.source_occupancy);
            cJSON_AddNumberToObject(sdio,"current_frame",s.current_frame);
            cJSON_AddNumberToObject(sdio,"receiving_slot",s.receiving_slot);
            cJSON_AddNumberToObject(sdio,"window_blocks",s.window_blocks);
            cJSON_AddNumberToObject(sdio,"window_received",s.window_received);
            cJSON_AddNumberToObject(sdio,"grant",s.grant);
            cJSON_AddNumberToObject(sdio,"window_ack",s.window_ack);
            cJSON_AddNumberToObject(sdio,"cmd_seq",s.cmd_seq);
            cJSON_AddNumberToObject(sdio,"cmd_ack",s.cmd_ack);
            cJSON_AddNumberToObject(sdio,"command",s.command);
            cJSON_AddNumberToObject(sdio,"result",s.result);
            cJSON_AddNumberToObject(sdio,"register_status",s.register_status);
            cJSON_AddNumberToObject(sdio,"progress_idle_us",s.progress_idle_us);
            cJSON_AddNumberToObject(sdio,"last_receive_us",s.last_receive_us);
            cJSON_AddNumberToObject(sdio,"last_crc_us",s.last_crc_us);
            cJSON_AddNumberToObject(sdio,"last_copy_crc_us",s.last_copy_crc_us);
            cJSON_AddBoolToObject(sdio,"crc_copy_fused",s.crc_sram_table);
            cJSON_AddBoolToObject(sdio,"crc_sram_table",s.crc_sram_table);
            cJSON_AddNumberToObject(sdio,"crc_rom_bench_us",s.crc_rom_bench_us);
            cJSON_AddNumberToObject(sdio,"crc_table_bench_us",s.crc_table_bench_us);
        }
    }
    return reply(req, "200 OK", json);
}
static esp_err_t upload(httpd_req_t *req)
{
    esp_err_t err = selftest_upload();
    if (err == ESP_OK) return message(req, "202 Accepted", "已接受单次 PSRAM 自测上传");
    if (err == ESP_ERR_TIMEOUT) return message(req, "409 Conflict", "已有上传正在进行");
    return message(req, "503 Service Unavailable", err == ESP_ERR_NO_MEM ?
        "PSRAM 自测缓冲区不可用" : "请先连接 Wi-Fi 并设置上位机 IP/端口");
}
static esp_err_t stop(httpd_req_t *req)
{
    sdio_bridge_stop();
    return message(req, "202 Accepted", "停止请求已提交；正在发送的数据报可能已进入本地协议栈");
}
static esp_err_t sdio_start(httpd_req_t *req)
{
    if (req->content_len < 1 || req->content_len > 64)
        return message(req,"400 Bad Request","需要 JSON: {\"frames\":1} 或 {\"frames\":0}");
    char body[65]; size_t n=0;
    while (n<req->content_len) {
        int r=httpd_req_recv(req,body+n,req->content_len-n);
        if (r<=0) return message(req,"408 Request Timeout","请求不完整");
        n+=(size_t)r;
    }
    body[n]=0;
    cJSON *j=cJSON_ParseWithLengthOpts(body,n+1,NULL,true);
    cJSON *f=cJSON_GetObjectItemCaseSensitive(j,"frames");
    bool valid=cJSON_IsObject(j) && cJSON_IsNumber(f) && (f->valuedouble==0 || f->valuedouble==1);
    unsigned count=valid?(unsigned)f->valueint:0;
    cJSON_Delete(j);
    if (!valid) return message(req,"400 Bad Request","frames 必须为 0（连续）或 1（单帧）");
    esp_err_t r=sdio_bridge_start(count);
    if (r==ESP_OK) return message(req,"202 Accepted","SDIO 测试已启动，请核对 PC 接收校验结果");
    return message(req,r==ESP_ERR_TIMEOUT?"409 Conflict":"503 Service Unavailable",
        r==ESP_ERR_TIMEOUT?"请确认 Zynq 已编译为 SDIO 模式并启动，且当前任务已结束":"SDIO 不可用，或 Wi-Fi/目标地址尚未就绪");
}
esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_uri_handlers = 12;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page},
        {.uri = "/api/config", .method = HTTP_GET, .handler = get_config},
        {.uri = "/api/config", .method = HTTP_PUT, .handler = put_config},
        {.uri = "/api/status", .method = HTTP_GET, .handler = get_status},
        {.uri = "/api/upload", .method = HTTP_POST, .handler = upload},
        {.uri = "/api/stop", .method = HTTP_POST, .handler = stop},
        {.uri = "/api/sdio/start", .method = HTTP_POST, .handler = sdio_start},
        {.uri = "/api/sdio/stop", .method = HTTP_POST, .handler = stop},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) { httpd_stop(server); return err; }
    }
    err=zynq_control_http_register(server);
    if(err!=ESP_OK) httpd_stop(server);
    return err;
}
