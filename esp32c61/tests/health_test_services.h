#ifndef HEALTH_TEST_SERVICES_H
#define HEALTH_TEST_SERVICES_H
#include <stdint.h>
typedef uint64_t XTime;
#define COUNTS_PER_SECOND 1000U
#define LED_1_EMIO_PIN 54U
#define FS_BASE 0x404c0200U
#define FS_ERRORS 56U
extern int gpiops;
void XTime_GetTime(XTime *);
void XGpioPs_SetDirectionPin(int *,unsigned,unsigned);
void XGpioPs_SetOutputEnablePin(int *,unsigned,unsigned);
void XGpioPs_WritePin(int *,unsigned,unsigned);
unsigned Xil_In32(unsigned);
void xil_printf(const char *,...);
int startup_recovery_blocks(void),frame_store_is_ready(void),afe_power_allowed(void),afe5832_spi_healthy(void);
int tx_power_allowed(void),sdio_bridge_output_is_sdio(void),sdio_bridge_healthy(void),usb_transport_healthy(void);
int zynq_uart_command_healthy(void),temperature_service_healthy(void),uart_config_active(void);
#endif
