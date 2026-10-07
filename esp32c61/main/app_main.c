#include "config_store.h"
#include "psram_udp.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "selftest.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

void app_main(void)
{
    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(psram_udp_init());
    esp_err_t err = selftest_init();
    if (err != ESP_OK) ESP_LOGE("app", "PSRAM self-test unavailable: %s; no SRAM fallback", esp_err_to_name(err));
    ESP_LOGI("app", "PSRAM total=%u free=%u largest=%u", (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI("app", "Ready: configure target and start PC receiver before pressing Upload");
}
