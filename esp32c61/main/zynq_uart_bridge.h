#ifndef ZYNQ_UART_BRIDGE_H
#define ZYNQ_UART_BRIDGE_H
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
bool zynq_uart_bridge_available(void);
esp_err_t zynq_uart_bridge_send(const char *data,size_t length);
esp_err_t zynq_uart_bridge_start(void);
#endif
