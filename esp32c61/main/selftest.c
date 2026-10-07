#include "selftest.h"
#include "psram_udp.h"
#include "esp_log.h"
#include "sdkconfig.h"
static uint8_t *test_buffer;
static void complete(raw_result_t result, size_t sent, void *ctx)
{
    (void)ctx;
    ESP_LOGI("selftest", "Upload result=%s local_bytes=%u", raw_result_name(result), (unsigned)sent);
}
esp_err_t selftest_init(void)
{
    if (CONFIG_PSRAM_UDP_TEST_BYTES > CONFIG_PSRAM_UDP_MAX_BYTES) return ESP_ERR_INVALID_SIZE;
    test_buffer = psram_udp_alloc(CONFIG_PSRAM_UDP_TEST_BYTES);
    if (!test_buffer) return ESP_ERR_NO_MEM;
    raw_pattern_fill(test_buffer, CONFIG_PSRAM_UDP_TEST_BYTES);
    ESP_LOGI("selftest", "PSRAM buffer=%p bytes=%u", test_buffer, CONFIG_PSRAM_UDP_TEST_BYTES);
    return ESP_OK;
}
esp_err_t selftest_upload(void)
{
    if (!test_buffer) return ESP_ERR_NO_MEM;
    return psram_udp_submit(test_buffer, CONFIG_PSRAM_UDP_TEST_BYTES, complete, NULL);
}
size_t selftest_size(void) { return test_buffer ? CONFIG_PSRAM_UDP_TEST_BYTES : 0; }
