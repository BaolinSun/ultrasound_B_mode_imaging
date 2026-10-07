#pragma once
#include <stdbool.h>
#include <stdint.h>

#define APP_CONFIG_VERSION 1U
typedef struct {
    uint32_t version;
    char ssid[33];
    char password[65];
    char target_ip[16];
    uint16_t target_port;
    char ap_password[17];
} app_config_t;
bool config_ipv4_valid(const char *text, bool allow_empty);
bool config_valid(const app_config_t *config);
typedef int (*config_persist_t)(void *ctx, const app_config_t *candidate);
/* Transaction: persistence must succeed before changing the active snapshot. */
int config_commit(app_config_t *active, const app_config_t *candidate,
                  config_persist_t persist, void *ctx);
