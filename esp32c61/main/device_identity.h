#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DEVICE_IDENTITY_BYTES 96U
typedef struct { uint8_t mac[6]; char ssid[33]; char password[17]; } device_identity_t;
bool device_identity_valid(const device_identity_t *id);
bool device_identity_parse(const char *text, device_identity_t *id);
bool device_identity_format(const device_identity_t *id, char *text, size_t capacity);
/* Initialized once before Wi-Fi/tasks; immutable afterwards. No password in status. */
bool device_store_get(device_identity_t *id);
const char *device_store_state(void);
