#ifndef FAKE_CONTROL_SDK_H
#define FAKE_CONTROL_SDK_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_INVALID_STATE 4
#define ESP_ERR_TIMEOUT 5
#define ESP_ERR_NOT_SUPPORTED 7
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(n) (n)
typedef struct cJSON { char dummy; } cJSON;
cJSON *cJSON_AddObjectToObject(cJSON*,const char*);
void cJSON_AddBoolToObject(cJSON*,const char*,bool);
void cJSON_AddStringToObject(cJSON*,const char*,const char*);
void cJSON_AddNumberToObject(cJSON*,const char*,double);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
QueueHandle_t xQueueCreate(unsigned,unsigned);
int xQueueSend(QueueHandle_t,const void*,unsigned);
int xQueueReceive(QueueHandle_t,void*,unsigned);
int xQueueReset(QueueHandle_t);
void vQueueDelete(QueueHandle_t);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
#endif
