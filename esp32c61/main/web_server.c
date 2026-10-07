#include "web_server.h"
#include "config_store.h"
#include "psram_udp.h"
#include "selftest.h"
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
    if (wifi_changed) psram_udp_set_link(false);
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
        cJSON_AddStringToObject(json, "result", raw_result_name(udp.result));
        cJSON_AddNumberToObject(json, "total_bytes", udp.total_bytes);
        cJSON_AddNumberToObject(json, "sent_bytes", udp.sent_bytes);
        cJSON_AddNumberToObject(json, "packets", udp.packets);
        cJSON_AddNumberToObject(json, "retries", udp.retries);
        cJSON_AddNumberToObject(json, "socket_errno", udp.socket_errno);
        cJSON_AddStringToObject(json, "active_ip", udp.active_ip);
        cJSON_AddNumberToObject(json, "active_port", udp.active_port);
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
    psram_udp_stop();
    return message(req, "202 Accepted", "停止请求已提交；正在发送的数据报可能已进入本地协议栈");
}
esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_uri_handlers = 6;
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
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) { httpd_stop(server); return err; }
    }
    return ESP_OK;
}
