#include <stdint.h>
#include "../../../../build/src/ps/data_transfer/frame_store.h"
#include "afe_power_regs.h"
extern int gpiops;
#define AFE5832_PDN_GBL_PIN_EMIO 71U
uint32_t temp_hw_now_ms(void);
int temperature_service_zynq_mc(int32_t *);
uint32_t afe_power_operation_sequence(void);
uint32_t tx_power_operation_sequence(void);
int uart_config_active(void);
int uart_idle_pending(void);
int zynq_uart_idle_pending(void);
uint32_t Beamform_GetRunRequest(void);
uint32_t Beamform_GetSpiStartRequest(void);
uint32_t hv_is_enabled(void);
int usb_transport_power_idle(void);
int sdio_bridge_power_idle(void);
int sdio_bridge_source_recovery_begin(void);
void sdio_bridge_init(void);
int tx_power_allowed(void);
void afe_power_init(void);
int afe_power_allowed(void);
const char *afe_power_error(void);
uint32_t XGpioPs_ReadPin(int *,uint32_t);
uint32_t Xil_In32(uint32_t);
void xil_printf(const char *,...);
