#include "selftest.h"
#include "sdio_bridge.h"
esp_err_t selftest_init(void) { return sdio_bridge_selftest_size() ? ESP_OK : ESP_ERR_NO_MEM; }
esp_err_t selftest_upload(void) { return sdio_bridge_selftest(); }
size_t selftest_size(void) { return sdio_bridge_selftest_size(); }
