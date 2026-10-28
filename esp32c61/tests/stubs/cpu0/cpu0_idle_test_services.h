#ifndef CPU0_TEST_SERVICES_H
#define CPU0_TEST_SERVICES_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
typedef uint32_t u32;
typedef uint8_t u8;
typedef uint64_t XTime;
typedef struct {void(*Handler)(void *);void *CallBackRef;} XScuGic_VectorTableEntry;
typedef struct {XScuGic_VectorTableEntry HandlerTable[128];} XScuGic_Config;
typedef struct {XScuGic_Config *Config;u32 IsReady;} XScuGic;
#define XST_SUCCESS 0
#define XIL_COMPONENT_IS_READY 1U
#define XIL_EXCEPTION_IRQ 0x80U
#define XPAR_XSCUTIMER_0_BASEADDR 0x1000U
#define XSCUTIMER_LOAD_OFFSET 0U
#define XSCUTIMER_COUNTER_OFFSET 4U
#define XSCUTIMER_CONTROL_OFFSET 8U
#define XSCUTIMER_ISR_OFFSET 12U
#define XSCUGIC_ENABLE_SET_OFFSET 0x100U
#define XSCUGIC_PENDING_CLR_OFFSET 0x280U
#define XSCUGIC_HI_PEND_OFFSET 0x18U
#define XSCUGIC_CPU_PRIOR_OFFSET 4U
#define COUNTS_PER_SECOND 1000000U
#define XTIMER_NO_TICK_TIMER 1
#define LED_2_EMIO_PIN 54U
extern u32 cpu0_test_mpidr,cpu0_test_cpsr;
extern int gpiops;
void cpu0_test_wfi(void);
void cpu0_test_restore(u32);
void cpu0_test_barrier(void);
#define dsb() cpu0_test_barrier()
#define isb() cpu0_test_barrier()
#define mfcpsr() cpu0_test_cpsr
#define mtcpsr(v) cpu0_test_restore(v)
void Xil_ExceptionDisableMask(u32);
u32 Xil_In32(u32);
void Xil_Out32(u32,u32);
void XTime_GetTime(XTime *);
u32 XGet_CpuFreq(void);
u32 XScuGic_DistReadReg(XScuGic *,u32);
u32 XScuGic_CPUReadReg(XScuGic *,u32);
void XScuGic_DistWriteReg(XScuGic *,u32,u32);
void XScuGic_Disable(XScuGic *,u32);
void XScuGic_Enable(XScuGic *,u32);
int XScuGic_Connect(XScuGic *,u32,void(*)(void *),void *);
void XScuGic_GetPriorityTriggerType(XScuGic *,u32,u8 *,u8 *);
void XScuGic_SetPriorityTriggerType(XScuGic *,u32,u8,u8);
u32 XGpioPs_ReadPin(int *,u32);
void XGpioPs_WritePin(int *,u32,u32);
void xil_printf(const char *,...);
const char *afe_power_state_name(void);
const char *tx_power_state_name(void);
int uart_config_active(void);
int sdio_bridge_power_idle(void);
int sdio_bridge_idle_pending(void);
int usb_transport_power_idle(void);
int uart_idle_pending(void);
int zynq_uart_idle_pending(void);
int usb_commands_idle_pending(void);
int usb_transport_idle_pending(void);
u32 Beamform_GetRunRequest(void);
u32 Beamform_GetSpiStartRequest(void);
u32 hv_is_enabled(void);
int afe_power_sleep_confirmed(void);
int tx_power_sleep_confirmed(void);
int frame_store_pause_retained(void);
uint32_t temperature_service_remaining_ms(void);
uint32_t temperature_service_sequence(void);
int temp_hw_send(const char *,size_t);
void temperature_service_poll(void);
void uart_process_commands(void);
void usb_process_commands(void);
void sdio_bridge_poll(void);
void startup_recovery_poll(void);
static inline void system_health_poll(void) {}
#endif
