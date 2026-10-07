#pragma once
#include "esp_err.h"
#include <stddef.h>
esp_err_t selftest_init(void);
esp_err_t selftest_upload(void);
size_t selftest_size(void);
