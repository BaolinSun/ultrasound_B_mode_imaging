#include "fake_uart_sdk.h"
#include "zynq_uart_bridge.h"
#include "temperature_cache.h"
#include "temperature_protocol.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
static int fail_call,call,deletes,installs,flushes,resets,warnings,step,read_error,console_timeouts;
static bool connected=true;
static bool stdout_locked;
static bool fail_next_sync;
static int hidden_warning_fault=1,stall_warning_fault=1,rx_warning_fault=1;
static int64_t now;
static void (*task_entry)(void*);
static jmp_buf stop;
static const char *pending;
static char temperature_line[TEMP_LINE_BYTES];
static int result(void) { return ++call==fail_call ? 9:ESP_OK; }
esp_err_t gpio_reset_pin(int pin) { assert(pin==11);return result(); }
esp_err_t gpio_set_direction(int pin,int mode) { assert(pin==11 && mode==GPIO_MODE_INPUT);return result(); }
esp_err_t uart_driver_install(int port,int rx,int tx,int depth,QueueHandle_t *q,int flags) {
    assert(port==0 && rx==4096 && tx==0 && depth==16 && flags==0);installs++;*q=(void*)1;return result();
}
esp_err_t uart_param_config(int port,const uart_config_t *c) {
    assert(port==0 && c->baud_rate==115200 && c->data_bits==8 && c->parity==0 &&
           c->stop_bits==1 && c->flow_ctrl==0);return result();
}
esp_err_t uart_set_pin(int port,int tx,int rx,int rts,int cts) {
    assert(port==0 && tx==-1 && rx==10 && rts==-1 && cts==-1);return result();
}
esp_err_t uart_driver_delete(int port) { assert(port==0);deletes++;return 0; }
esp_err_t uart_flush_input(int port) { assert(port==0);flushes++;pending=NULL;return 0; }
int xQueueReset(QueueHandle_t q) { assert(q);resets++;return 0; }
int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void *ctx,unsigned priority,void *handle) {
    assert(!strcmp(name,"zynq_uart_rx") && stack==3072 && priority==2 && !ctx && !handle);
    task_entry=fn;return result()==ESP_OK?pdPASS:0;
}
int xQueueReceive(QueueHandle_t q,uart_event_t *e,unsigned wait) {
    assert(q && wait==20);now+=250000;
    if(step==20)longjmp(stop,1);
    e->type=UART_DATA;
    switch(step++) {
    case 0:pending="first\r";break;
    case 1:pending="\n\xe4\xb8\xad\n";break;
    case 2:pending="lost partial";break;
    case 3:e->type=UART_FIFO_OVF;break;
    case 4:pending="next\n";break;
    case 5:e->type=UART_BUFFER_FULL;break;
    case 6:e->type=UART_PARITY_ERR;break;
    case 7:e->type=UART_FRAME_ERR;break;
    case 8:e->type=UART_BREAK;break;
    case 9:read_error=1;break;
    case 10:{
        temperature_message_t t={.seq=123,.mask=7,.mc={35000,0,-10125}};
        assert(temperature_format(temperature_line,sizeof(temperature_line),&t)>0);
        connected=false;pending=temperature_line;break;
    }
    case 11:{
        /* Parser works while monitor is disconnected, before print gate. */
        temperature_status_t t;temperature_cache_get(&t,(uint64_t)now/1000);
        assert(t.has_sample && t.seq==123 && t.mask==7 && t.mc[1]==0);
        connected=true;pending="visible\n";break;
    }
    case 12:pending="no newline";break;
    case 14:console_timeouts=1;pending="monitor stalled\n";break;
    case 15:pending="monitor resumed\n";break;
    case 16:console_timeouts=2;pending="stall twice\n";break;
    case 17:pending="warning not drained\n";break;
    case 18:pending="warning recovered\n";break;
    default:break;
    }
    return pdTRUE;
}
int uart_read_bytes(int port,void *dst,size_t cap,unsigned wait) {
    assert(port==0 && cap==256 && wait==0);
    if(read_error) { read_error=0;return -1; }
    if(!pending)return 0;
    size_t n=strlen(pending);assert(n<=cap);memcpy(dst,pending,n);pending=NULL;return (int)n;
}
void vTaskDelay(unsigned delay) { assert(delay==1); }
int64_t esp_timer_get_time(void) { return now; }
bool usb_serial_jtag_is_connected(void) { return connected; }
int fileno(FILE *f) { assert(f==stdout);return 1; }
int fsync(int fd) {
    assert(fd==1);
    if(fail_next_sync) { fail_next_sync=false;return -1; }
    if(console_timeouts) { console_timeouts--;return -1; }return 0;
}
void fake_log(const char *tag,const char *format,...) {
    assert(!stdout_locked); /* No logger/stdout lock-order inversion. */
    assert(!strcmp(tag,"zynq_uart"));
    if(strstr(format,"USB disconnected") && hidden_warning_fault) {
        hidden_warning_fault--;fail_next_sync=true;
    }
    if(strstr(format,"USB console stalled") && stall_warning_fault) {
        stall_warning_fault--;fail_next_sync=true;
    }
    if(strstr(format,"UART RX errors") && rx_warning_fault) {
        rx_warning_fault--;fail_next_sync=true;
    }
    if(strstr(format,"log may be incomplete") || strstr(format,"USB disconnected"))warnings++;
}
void flockfile(FILE *f) { assert(f==stdout && !stdout_locked);stdout_locked=true; }
void funlockfile(FILE *f) { assert(f==stdout && stdout_locked);stdout_locked=false; }
int main(int argc,char **argv) {
#if !CONFIG_ZYNQ_UART_BRIDGE_ENABLE
    (void)argc;(void)argv;
    assert(zynq_uart_bridge_start()==ESP_OK && call==0 && installs==0);
    puts("PASS: disabled UART bridge does not configure pins or UART");
    return 0;
#else
    if(argc>1) {
        fail_call=atoi(argv[1]);assert(fail_call>=1 && fail_call<=6);
        assert(zynq_uart_bridge_start()!=ESP_OK);
        assert(deletes==(fail_call>=4?1:0));
        fail_call=0;assert(zynq_uart_bridge_start()==ESP_OK); /* Retry after cleanup. */
        puts("PASS: UART bridge initialization failure cleaned and retry succeeded");return 0;
    }
    assert(zynq_uart_bridge_start()==ESP_OK && installs==1);
    assert(zynq_uart_bridge_start()==ESP_OK && installs==1); /* Idempotence. */
    if(setjmp(stop)==0)task_entry(NULL);
    assert(flushes==5 && resets==5 && warnings>=4);
    puts("PASS: UART RX-only pins/baud/task/errors/USB disconnect/stall/idle/recovery");
    return 0;
#endif
}
