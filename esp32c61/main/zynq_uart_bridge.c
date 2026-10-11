#include "zynq_uart_bridge.h"
#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_ZYNQ_UART_BRIDGE_ENABLE
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "Zynq UART bridge requires USB Serial/JTAG console (UART0 is receive-only)"
#endif
#include "uart_log_framer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>

#define RX_GPIO 10
#define UNUSED_TX_GPIO 11
#define RX_BUFFER_BYTES 4096
#define UART_BAUD 115200
static const char *TAG="zynq_uart";
static QueueHandle_t events;
static bool started;
static uint64_t hidden_bytes,console_stalls,errors,last_warning_ms;
static uint64_t milliseconds(void) { return (uint64_t)esp_timer_get_time()/1000U; }
static void print_line(const uint8_t *p,size_t length,void *ctx)
{
    (void)ctx;
    if(!usb_serial_jtag_is_connected()) { hidden_bytes+=length; return; }
    flockfile(stdout);
    clearerr(stdout);
    fputs("[ZYNQ] ",stdout);
    (void)fwrite(p,1,length,stdout);
    fputc('\n',stdout);
    fflush(stdout); /* Keep IDF's default bounded USB VFS; never use driver blocking mode. */
    /* SOF/connected alone does not mean monitor is reading. IDF 6.1 VFS
     * fsync honours the same 50 ms deadline and reports an undrained FIFO.
     * write() itself may silently drop bytes, so count suspect batches,
     * not an invented number of lost bytes. Report once output recovers. */
    int drained=fsync(fileno(stdout));
    funlockfile(stdout);
    /* Do not nest ESP_LOG's locks inside our stdout lock. Keep loss counters
     * until the warning itself drains, including a reconnect without reader. */
    if(drained!=0) {
        console_stalls++;
        return;
    }
    if(hidden_bytes) {
        ESP_LOGW(TAG,"USB disconnected: discarded %" PRIu64 " log bytes",hidden_bytes);
        if(fsync(fileno(stdout))==0) hidden_bytes=0;
        else console_stalls++;
    }
    if(console_stalls) {
        ESP_LOGW(TAG,"USB console stalled in %" PRIu64 " batches; Zynq log may be incomplete",console_stalls);
        if(fsync(fileno(stdout))==0) console_stalls=0;
    }
}
static void receive_task(void *ctx)
{
    (void)ctx;
    uart_log_framer_t framer={0};
    uint8_t bytes[256];
    for(;;) {
        uart_event_t event;
        if(xQueueReceive(events,&event,pdMS_TO_TICKS(20))==pdTRUE) {
            if(event.type==UART_FIFO_OVF || event.type==UART_BUFFER_FULL ||
               event.type==UART_PARITY_ERR || event.type==UART_FRAME_ERR || event.type==UART_BREAK) {
                errors++;
                (void)uart_flush_input(UART_NUM_0);
                (void)xQueueReset(events);
                uart_log_framer_reset(&framer); /* Never join text across lost data. */
            }
        }
        for(unsigned batch=0;batch<4;batch++) {
            int count=uart_read_bytes(UART_NUM_0,bytes,sizeof(bytes),0);
            if(count<0) { errors++; uart_log_framer_reset(&framer); break; }
            if(!count) break;
            uart_log_framer_feed(&framer,bytes,(size_t)count,milliseconds(),print_line,NULL);
            vTaskDelay(1); /* Lower priority than the SDIO task; no payload/control parsing. */
        }
        uint64_t now=milliseconds();
        uart_log_framer_idle(&framer,now,print_line,NULL);
        if(errors && now-last_warning_ms>=1000U && usb_serial_jtag_is_connected()) {
            ESP_LOGW(TAG,"UART RX errors/overflow=%" PRIu64 "; Zynq log may be incomplete",errors);
            if(fsync(fileno(stdout))==0) errors=0;
            last_warning_ms=now;
        }
    }
}
esp_err_t zynq_uart_bridge_start(void)
{
    if(started) return ESP_OK;
    const uart_config_t config={.baud_rate=UART_BAUD,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t r=gpio_reset_pin(UNUSED_TX_GPIO);
    if(r!=ESP_OK) return r;
    if((r=gpio_set_direction(UNUSED_TX_GPIO,GPIO_MODE_INPUT))!=ESP_OK) return r;
    /* Do not route TX to GPIO11, including when that wire is physically present. */
    if((r=uart_driver_install(UART_NUM_0,RX_BUFFER_BYTES,0,16,&events,0))!=ESP_OK) return r;
    if((r=uart_param_config(UART_NUM_0,&config))==ESP_OK)
        r=uart_set_pin(UART_NUM_0,UART_PIN_NO_CHANGE,RX_GPIO,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(r==ESP_OK && xTaskCreate(receive_task,"zynq_uart_rx",3072,NULL,2,NULL)!=pdPASS) r=ESP_ERR_NO_MEM;
    if(r!=ESP_OK) { (void)uart_driver_delete(UART_NUM_0); events=NULL; return r; }
    started=true;
    ESP_LOGI(TAG,"RX-only: Zynq MIO27/TX -> GPIO10, 115200 8N1; GPIO11 input; USB console");
    return ESP_OK;
}
#else
esp_err_t zynq_uart_bridge_start(void) { return ESP_OK; }
#endif
