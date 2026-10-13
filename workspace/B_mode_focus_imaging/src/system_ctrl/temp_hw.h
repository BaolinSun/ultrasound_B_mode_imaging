#ifndef SYSTEM_CTRL_TEMP_HW_H
#define SYSTEM_CTRL_TEMP_HW_H
#include <stdint.h>
#include <stddef.h>
#define TEMP_IO_TIMEOUT_MS 20U
uint32_t temp_hw_now_ms(void);
int temp_hw_init_xadc(void);
int temp_hw_xadc_raw(uint16_t *raw);
int temp_hw_sensor_raw(uint32_t address,uint16_t *raw);
int temp_hw_send(const char *bytes,size_t length);
#endif
