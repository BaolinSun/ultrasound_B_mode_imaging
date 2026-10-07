#pragma once
#include "config_model.h"
#include "esp_err.h"
esp_err_t config_store_init(void);
void config_store_get(app_config_t *config);
esp_err_t config_store_save(const app_config_t *config);
