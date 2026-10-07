#include "wifi_manager.h"
#include "config_store.h"
#include "psram_udp.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static SemaphoreHandle_t lock;
static wifi_manager_status_t state;
static uint64_t disconnected_at, connected_at, retry_at, apply_at;
static unsigned retry_delay_ms = 1000;
static uint32_t request_generation, applied_generation;
static uint64_t millis(void) { return esp_timer_get_time() / 1000ULL; }

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *event)
{
    (void)arg;
    xSemaphoreTake(lock, portMAX_DELAY);
    if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
        (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        if (state.connected) disconnected_at = millis();
        state.connected = false;
        state.sta_ip[0] = 0;
        connected_at = 0;
        psram_udp_set_link(false);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && !state.reconfiguring) {
        ip_event_got_ip_t *got = event;
        app_config_t current_config;
        wifi_ap_record_t associated_ap = {0};
        config_store_get(&current_config);
        /* Reject delayed IP events belonging to a disconnected/previous AP. */
        if (esp_wifi_sta_get_ap_info(&associated_ap) != ESP_OK ||
            strcmp((const char *)associated_ap.ssid, current_config.ssid) != 0) {
            xSemaphoreGive(lock);
            return;
        }
        if (!state.connected) connected_at = millis();
        state.connected = true;
        snprintf(state.sta_ip, sizeof(state.sta_ip), IPSTR, IP2STR(&got->ip_info.ip));
        retry_delay_ms = 1000;
        ESP_LOGI("wifi", "STA configuration page: http://%s/", state.sta_ip);
        if (!state.reconfiguring) psram_udp_set_link(true);
    }
    xSemaphoreGive(lock);
}

static esp_err_t set_station_config(const app_config_t *config)
{
    if (!config->ssid[0]) return ESP_OK; /* Supervisor does not connect with empty SSID. */
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, config->ssid, strlen(config->ssid));
    memcpy(wifi.sta.password, config->password, strlen(config->password));
    wifi.sta.pmf_cfg.capable = true;
    return esp_wifi_set_config(WIFI_IF_STA, &wifi);
}

static esp_err_t enable_ap(const app_config_t *config)
{
    wifi_config_t ap = {0};
    memcpy(ap.ap.ssid, state.ap_ssid, strlen(state.ap_ssid));
    ap.ap.ssid_len = strlen(state.ap_ssid);
    memcpy(ap.ap.password, config->ap_password, strlen(config->ap_password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err == ESP_OK) {
        xSemaphoreTake(lock, portMAX_DELAY);
        state.ap_active = true;
        xSemaphoreGive(lock);
        ESP_LOGI("wifi", "Configuration hotspot SSID=%s password=%s URL=http://192.168.4.1/",
                 state.ap_ssid, config->ap_password);
    }
    return err;
}

static void supervisor(void *arg)
{
    (void)arg;
    for (;;) {
        uint64_t now = millis();
        app_config_t config;
        config_store_get(&config);
        xSemaphoreTake(lock, portMAX_DELAY);
        bool apply = request_generation != applied_generation && now >= apply_at;
        uint32_t generation = request_generation;
        wifi_manager_status_t snapshot = state;
        uint64_t since_down = disconnected_at, since_up = connected_at;
        bool retry = !state.reconfiguring && !state.connected &&
                     config.ssid[0] && now >= retry_at;
        if (retry) {
            retry_at = now + retry_delay_ms;
            if (retry_delay_ms < 16000) retry_delay_ms *= 2;
        }
        xSemaphoreGive(lock);
        if (apply) {
            /* HTTP response is sent before this deferred Wi-Fi change (500 ms). */
            psram_udp_set_link(false);
            (void)esp_wifi_disconnect();
            xSemaphoreTake(lock, portMAX_DELAY);
            state.connected = false;
            state.sta_ip[0] = 0;
            disconnected_at = now;
            connected_at = 0;
            retry_at = now;
            retry_delay_ms = 1000;
            xSemaphoreGive(lock);
            esp_err_t err = set_station_config(&config);
            xSemaphoreTake(lock, portMAX_DELAY);
            if (err == ESP_OK) {
                applied_generation = generation;
                state.reconfiguring = applied_generation != request_generation;
            } else {
                apply_at = now + 1000;
            }
            xSemaphoreGive(lock);
            if (err != ESP_OK) ESP_LOGE("wifi", "Apply STA config: %s", esp_err_to_name(err));
        } else {
            if (retry) {
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) ESP_LOGW("wifi", "STA connect: %s", esp_err_to_name(err));
            }
            if (!snapshot.ap_active && (!config.ssid[0] ||
                (!snapshot.connected && now - since_down >= 30000))) {
                esp_err_t err = enable_ap(&config);
                if (err != ESP_OK) ESP_LOGE("wifi", "Enable AP: %s", esp_err_to_name(err));
            }
            if (snapshot.ap_active && snapshot.connected && !snapshot.reconfiguring &&
                since_up && now - since_up >= 15000) {
                esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
                if (err == ESP_OK) {
                    xSemaphoreTake(lock, portMAX_DELAY);
                    state.ap_active = false;
                    xSemaphoreGive(lock);
                    ESP_LOGI("wifi", "Configuration AP disabled; use http://%s/", snapshot.sta_ip);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t wifi_manager_init(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!sta || !ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    snprintf(state.ap_ssid, sizeof(state.ap_ssid), "Ultrasound-C61-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    app_config_t config;
    config_store_get(&config);
    err = set_station_config(&config);
    if (err != ESP_OK) return err;
    /* Default AP interface is 192.168.4.1/24; do not enable NAPT/routing. */
    if (!config.ssid[0]) {
        err = enable_ap(&config);
        if (err != ESP_OK) return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) return err;
    disconnected_at = millis();
    return xTaskCreate(supervisor, "wifi_manager", 4096, NULL, 4, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

void wifi_manager_reconfigure(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    ++request_generation;
    state.reconfiguring = true;
    apply_at = millis() + 500;
    psram_udp_set_link(false);
    xSemaphoreGive(lock);
}
void wifi_manager_get_status(wifi_manager_status_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY); *out = state; xSemaphoreGive(lock);
}
