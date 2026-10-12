#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_INVALID_ARG 4
#define ESP_ERR_INVALID_SIZE 5
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_8BIT 8
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define eSetBits 0
#define CONFIG_PSRAM_UDP_TEST_BYTES 787456
#define CONFIG_PSRAM_UDP_MAX_BYTES 1048576
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *sdio_slave_buf_handle_t;
typedef struct { int sending_mode,send_queue_size; size_t recv_buffer_size; } sdio_slave_config_t;
#define SDIO_SLAVE_SEND_PACKET 0
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*);
int xTaskNotify(TaskHandle_t,uint32_t,int);
int xTaskNotifyWait(uint32_t,uint32_t,uint32_t*,uint32_t);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
void *heap_caps_malloc(size_t,unsigned);
void fake_log(const char*,const char*,...);
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGD(...) fake_log(__VA_ARGS__)
esp_err_t sdio_slave_initialize(sdio_slave_config_t*);
esp_err_t sdio_slave_start(void);
void sdio_slave_stop(void);
void sdio_slave_deinit(void);
sdio_slave_buf_handle_t sdio_slave_recv_register_buf(uint8_t*);
esp_err_t sdio_slave_recv_load_buf(sdio_slave_buf_handle_t);
esp_err_t sdio_slave_recv(sdio_slave_buf_handle_t*,uint8_t**,size_t*,uint32_t);
uint8_t sdio_slave_read_reg(unsigned);
esp_err_t sdio_slave_write_reg(unsigned,uint8_t);
