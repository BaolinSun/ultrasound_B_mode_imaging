#include "system_health.h"
#ifdef SYSTEM_HEALTH_TEST
#include "health_test_services.h"
#else
#include "../main.h"
#include "../GPIO/gpio.h"
#include "../AFE5832/afe_power.h"
#include "../AFE5832/afe5832_spi.h"
#include "../TX7332/tx_power.h"
#include "../data_transfer/frame_store.h"
#include "../data_transfer/sdio_bridge.h"
#include "../usb/usb_transport.h"
#include "../uart/uart_log.h"
#include "startup_recovery.h"
#include "temp.h"
#endif
static int initialized,boot_done,last_level;
static uint32_t faults=255U;
static uint64_t due;
static uint64_t health_ms(void) {
    XTime t;XTime_GetTime(&t);return (uint64_t)t*1000U/(uint64_t)COUNTS_PER_SECOND;
}
void system_health_init(void) {
    initialized=1;boot_done=0;due=0;faults=255U;last_level=0;
    XGpioPs_SetDirectionPin(&gpiops,LED_1_EMIO_PIN,1);
    XGpioPs_WritePin(&gpiops,LED_1_EMIO_PIN,0);
    XGpioPs_SetOutputEnablePin(&gpiops,LED_1_EMIO_PIN,1);
}
void system_health_boot_complete(void) {boot_done=1;due=0;system_health_poll();}
uint32_t system_health_faults(void) {return faults;}
void system_health_poll(void) {
    if(!initialized)return;
    uint64_t now=health_ms();if(now<due)return;due=now+100U;
    uint32_t f=0;
    if(!boot_done || startup_recovery_blocks())f|=HEALTH_BOOT;
    if(!frame_store_is_ready() || Xil_In32(FS_BASE+FS_ERRORS))f|=HEALTH_DDR;
    if(!afe_power_allowed() || !afe5832_spi_healthy())f|=HEALTH_AFE;
    if(!tx_power_allowed())f|=HEALTH_TX;
    if(!(sdio_bridge_output_is_sdio()?sdio_bridge_healthy():usb_transport_healthy()))f|=HEALTH_LINK;
    if(!zynq_uart_command_healthy())f|=HEALTH_UART;
    if(!temperature_service_healthy())f|=HEALTH_TEMP;
    if(uart_config_active())f|=HEALTH_CONFIG;
    int level=f==0;
    if(level!=last_level){XGpioPs_WritePin(&gpiops,LED_1_EMIO_PIN,level);last_level=level;}
    if(f!=faults)xil_printf("@HEALTH led=%u faults=%02x (boot/ddr/afe/tx/link/uart/temp/config)\r\n",(unsigned)level,(unsigned)f);
    faults=f;
}
