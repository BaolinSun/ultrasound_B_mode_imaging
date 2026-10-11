#ifndef FAKE_UART_SDK_H
#define FAKE_UART_SDK_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
typedef int esp_err_t;
typedef void *QueueHandle_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#ifndef CONFIG_ZYNQ_UART_BRIDGE_ENABLE
#define CONFIG_ZYNQ_UART_BRIDGE_ENABLE 1
#endif
#define CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG 1
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define GPIO_MODE_INPUT 1
#define UART_NUM_0 0
#define UART_PIN_NO_CHANGE -1
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
enum {UART_DATA,UART_FIFO_OVF,UART_BUFFER_FULL,UART_PARITY_ERR,UART_FRAME_ERR,UART_BREAK};
typedef struct {int type;size_t size;} uart_event_t;
typedef struct {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;} uart_config_t;
esp_err_t gpio_reset_pin(int);
esp_err_t gpio_set_direction(int,int);
esp_err_t uart_driver_install(int,int,int,int,QueueHandle_t*,int);
esp_err_t uart_param_config(int,const uart_config_t*);
esp_err_t uart_set_pin(int,int,int,int,int);
esp_err_t uart_driver_delete(int);
esp_err_t uart_flush_input(int);
int uart_read_bytes(int,void*,size_t,unsigned);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
int xQueueReceive(QueueHandle_t,uart_event_t*,unsigned);
int xQueueReset(QueueHandle_t);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
bool usb_serial_jtag_is_connected(void);
void fake_log(const char*,const char*,...);
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
void flockfile(FILE*);
void funlockfile(FILE*);
#endif
