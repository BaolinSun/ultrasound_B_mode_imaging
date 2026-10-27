#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_INVALID_STATE 4
#define ESP_ERR_NVS_NOT_FOUND 5
#define ESP_ERR_NVS_INVALID_LENGTH 6
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffU
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define ESP_MAC_WIFI_SOFTAP 1
void fake_log(const char*,const char*,...);
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_init_partition(const char*);
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
esp_err_t nvs_open_from_partition(const char*,const char*,int,nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
esp_err_t nvs_get_str(nvs_handle_t,const char*,char*,size_t*);
esp_err_t nvs_set_str(nvs_handle_t,const char*,const char*);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t esp_efuse_mac_get_default(uint8_t*);
esp_err_t esp_read_mac(uint8_t*,int);
typedef int esp_event_base_t;
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_DISCONNECTED 1
#define IP_EVENT_STA_LOST_IP 2
#define IP_EVENT_STA_GOT_IP 3
#define ESP_EVENT_ANY_ID -1
#define WIFI_MODE_STA 1
#define WIFI_MODE_APSTA 3
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_STORAGE_RAM 0
#define WIFI_PS_NONE 0
typedef struct { unsigned reason; } wifi_event_sta_disconnected_t;
typedef struct { struct {struct {uint32_t addr;} ip;} ip_info; } ip_event_got_ip_t;
typedef struct {uint8_t ssid[33];} wifi_ap_record_t;
typedef struct {
 struct {uint8_t ssid[32],password[64];struct {bool capable;} pmf_cfg;} sta;
 struct {uint8_t ssid[32],password[64];unsigned ssid_len,channel,max_connection,authmode;} ap;
} wifi_config_t;
typedef int wifi_init_config_t;
typedef int esp_netif_t;
#define WIFI_INIT_CONFIG_DEFAULT() 0
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) (unsigned)((p)->addr>>24),(unsigned)(((p)->addr>>16)&255),(unsigned)(((p)->addr>>8)&255),(unsigned)((p)->addr&255)
int64_t esp_timer_get_time(void);
const char *esp_err_to_name(int);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_event_handler_register(int,int,void(*)(void*,int,int32_t,void*),void*);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int,const wifi_config_t*);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_ps(int);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
void vTaskDelay(unsigned);
