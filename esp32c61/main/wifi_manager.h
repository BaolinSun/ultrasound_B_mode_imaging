#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef struct {
    bool connected;
    bool ap_active;
    bool reconfiguring;
    char sta_ip[16];
    char ap_ssid[33];
} wifi_manager_status_t;
esp_err_t wifi_manager_init(void);
void wifi_manager_reconfigure(void);
void wifi_manager_get_status(wifi_manager_status_t *status);
