#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#define SYSTEM_HEALTH_TEST 1
#include "../../build/src/ps/system_ctrl/system_health.c"
static uint64_t clock_ms;
static unsigned values[9],pin,writes,logs;
int gpiops;
void XTime_GetTime(XTime *t){*t=clock_ms;}
void XGpioPs_SetDirectionPin(int *p,unsigned a,unsigned b){(void)p;(void)a;(void)b;}
void XGpioPs_SetOutputEnablePin(int *p,unsigned a,unsigned b){(void)p;(void)a;(void)b;}
void XGpioPs_WritePin(int *p,unsigned a,unsigned b){(void)p;assert(a==LED_1_EMIO_PIN);pin=b;writes++;}
void xil_printf(const char *f,...){(void)f;logs++;}
int startup_recovery_blocks(void){return values[0];}
int frame_store_is_ready(void){return values[1];}
unsigned Xil_In32(unsigned a){assert(a==FS_BASE+FS_ERRORS);return values[2];}
int afe_power_allowed(void){return values[3];}
int afe5832_spi_healthy(void){return values[4];}
int tx_power_allowed(void){return values[5];}
int sdio_bridge_output_is_sdio(void){return values[8];}
int sdio_bridge_healthy(void){return values[6];}
int usb_transport_healthy(void){return values[6];}
int zynq_uart_command_healthy(void){return values[7];}
static unsigned temp=1,cfg;
int temperature_service_healthy(void){return temp;}
int uart_config_active(void){return cfg;}
static void step(void){clock_ms+=100;system_health_poll();}
int main(void){
 system_health_poll();assert(!writes);
 for(unsigned i=0;i<9;i++){values[i]=1;} values[0]=values[2]=0;
 system_health_init();assert(!pin);step();assert(system_health_faults()==HEALTH_BOOT);
 system_health_boot_complete();assert(pin && !system_health_faults());
 unsigned count=logs,wr=writes;system_health_poll();assert(logs==count && writes==wr);
 const unsigned masks[]={HEALTH_BOOT,HEALTH_DDR,HEALTH_DDR,HEALTH_AFE,HEALTH_AFE,HEALTH_TX,HEALTH_LINK,HEALTH_UART};
 for(unsigned i=0;i<8;i++){
  values[i]=!values[i];step();assert(!pin && system_health_faults()==masks[i]);
  values[i]=!values[i];step();assert(pin && !system_health_faults());
 }
 values[8]=0;step();assert(pin);values[6]=0;step();assert(!pin);values[6]=1;step();assert(pin);
 temp=0;step();assert(!pin && system_health_faults()==HEALTH_TEMP);temp=1;
 cfg=1;step();assert(!pin && system_health_faults()==HEALTH_CONFIG);cfg=0;step();assert(pin);
 system_health_init();assert(!pin);step();assert(system_health_faults()==HEALTH_BOOT);
 puts("PASS health: init, every fault, combined recovery, USB/SDIO, no idle/HV/CPU1 dependency, rate limit");
 return 0;
}
