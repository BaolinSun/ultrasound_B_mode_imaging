#include "config_store.h"
#include "psram_udp.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "selftest.h"
#include "sdio_bridge.h"
#include "zynq_uart_bridge.h"
#include "zynq_control.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

void app_main(void)
{
    esp_err_t uart_err=zynq_uart_bridge_start();
    if(uart_err!=ESP_OK) ESP_LOGE("app","Zynq UART log unavailable: %s; data services continue",esp_err_to_name(uart_err));
    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(psram_udp_init());
    esp_err_t sdio_err = sdio_bridge_init();
    if (sdio_err != ESP_OK) ESP_LOGE("app", "SDIO unavailable: %s", esp_err_to_name(sdio_err));
    esp_err_t err = selftest_init();
    if (err != ESP_OK) ESP_LOGE("app", "PSRAM self-test unavailable: %s; no SRAM fallback", esp_err_to_name(err));
    ESP_LOGI("app", "PSRAM total=%u free=%u largest=%u", (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(wifi_manager_init());
    esp_err_t control_err=zynq_control_start();
    if(control_err!=ESP_OK) ESP_LOGE("app","Zynq control unavailable: %s",esp_err_to_name(control_err));
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI("app", "Ready: configure target and start PC receiver before pressing Upload");
}
