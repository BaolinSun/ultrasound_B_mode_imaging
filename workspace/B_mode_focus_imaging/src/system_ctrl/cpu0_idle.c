#include "cpu0_idle.h"
#include "cpu0_idle_config.h"
#ifdef CPU0_IDLE_HOST_TEST
#include "cpu0_idle_test_services.h"
#else
#include "temp.h"
#include "temp_hw.h"
#include "../GPIO/gpio.h"
#include "../AFE5832/afe_power.h"
#include "../TX7332/tx_power.h"
#include "../HV/hv.h"
#include "../bram/bram.h"
#include "../uart/uart.h"
#include "../uart/uart_log.h"
#include "../usb/usb.h"
#include "../usb/usb_transport.h"
#include "../data_transfer/frame_store.h"
#include "../data_transfer/sdio_bridge.h"
#include "xscugic.h"
#include "xscutimer_hw.h"
#include "xiltimer.h"
#include "xcortexa9_config.h"
#include "xpseudo_asm.h"
#endif
#include <string.h>
#include "../../../../common/cpu0_idle_core.h"
#include "../../../../common/cpu0_idle_core.c"
#include "../../../../common/cpu_status_protocol.h"

extern XScuGic xInterruptController;
#define TIMER_BASE XPAR_XSCUTIMER_0_BASEADDR
#define TIMER_IRQ 29U /* CPU0 banked private timer PPI, never a CPU1 target. */
#define TIMER_CONTROL ((255U<<8)|5U) /* prescale /256, IRQ, one shot, enable */
static cpu0_idle_core idle_core;
static volatile u32 timer_irqs;
static u32 timer_frequency, armed_irq_count;
static int owned, initialized, last_quiet, led_level;
static uint64_t led_due;
static XScuGic_VectorTableEntry old_handler;
static u8 old_priority, old_trigger;
static u32 old_control,old_load,old_counter;
static u32 cpu_report_seq,cpu_report_temperature_seq;
static int cpu_report_pending;

static uint64_t now_us(void *ctx) {
    XTime t;(void)ctx;XTime_GetTime(&t);
    const uint64_t hz=(uint64_t)(COUNTS_PER_SECOND);
    return (t/hz)*1000000U+(t%hz)*1000000U/hz;
}
static u32 irq_lock(void *ctx) {
    (void)ctx;u32 saved=mfcpsr();Xil_ExceptionDisableMask(XIL_EXCEPTION_IRQ);return saved;
}
static void irq_restore(void *ctx,u32 saved) {(void)ctx;mtcpsr(saved);isb();}
static void barrier(void) {dsb();isb();}
static void timer_stop(void) {
    if(!owned)return;
    Xil_Out32(TIMER_BASE+XSCUTIMER_CONTROL_OFFSET,255U<<8);
    Xil_Out32(TIMER_BASE+XSCUTIMER_ISR_OFFSET,1U);
    XScuGic_DistWriteReg(&xInterruptController,XSCUGIC_PENDING_CLR_OFFSET,1U<<TIMER_IRQ);
    barrier();
}
static void timer_release(void) {
    if(!owned)return;
    u32 saved=irq_lock(0);timer_stop();
    XScuGic_Disable(&xInterruptController,TIMER_IRQ);
    xInterruptController.Config->HandlerTable[TIMER_IRQ]=old_handler;
    XScuGic_SetPriorityTriggerType(&xInterruptController,TIMER_IRQ,old_priority,old_trigger);
    Xil_Out32(TIMER_BASE+XSCUTIMER_LOAD_OFFSET,old_load);
    Xil_Out32(TIMER_BASE+XSCUTIMER_COUNTER_OFFSET,old_counter);
    Xil_Out32(TIMER_BASE+XSCUTIMER_CONTROL_OFFSET,old_control);
    barrier();owned=0;irq_restore(0,saved);
}
#if CPU0_IDLE_WFI_ENABLE
static void timer_isr(void *ctx) {
    (void)ctx;
    if(Xil_In32(TIMER_BASE+XSCUTIMER_ISR_OFFSET)&1U) {
        Xil_Out32(TIMER_BASE+XSCUTIMER_ISR_OFFSET,1U);timer_irqs++;
    }
}
#endif
static int both_sleep(void) {
    return !strcmp(afe_power_state_name(),"sleep") && !strcmp(tx_power_state_name(),"sleep");
}
int cpu0_idle_quiet(void) {
    return both_sleep() && !uart_config_active() &&
        sdio_bridge_power_idle() && usb_transport_power_idle();
}
static bool quiet(void *ctx) {(void)ctx;return cpu0_idle_quiet()!=0;}
static bool pending(void *ctx) {
    (void)ctx;
    if(!cpu0_idle_quiet() || uart_idle_pending() || zynq_uart_idle_pending() ||
       usb_commands_idle_pending() || usb_transport_idle_pending() || sdio_bridge_idle_pending())return true;
    if(Beamform_GetRunRequest() || Beamform_GetSpiStartRequest() || hv_is_enabled())return true;
    /* Existing confirmations only: never issue a FIFO flush with AFE clock off. */
    if(!afe_power_sleep_confirmed() || !tx_power_sleep_confirmed() || frame_store_pause_retained())return true;
    /* HPPIR is read-only. Reading IAR here would steal an interrupt from its ISR. */
    return (XScuGic_CPUReadReg(&xInterruptController,XSCUGIC_HI_PEND_OFFSET)&1023U)!=1023U;
}
static uint32_t next_ms(void *ctx) {(void)ctx;return temperature_service_remaining_ms();}
static bool timer_arm(void *ctx,uint32_t ms) {
    (void)ctx;
    if(!owned || !timer_frequency || !ms)return false;
    if(!(XScuGic_DistReadReg(&xInterruptController,XSCUGIC_ENABLE_SET_OFFSET)&(1U<<TIMER_IRQ)) ||
       XScuGic_CPUReadReg(&xInterruptController,XSCUGIC_CPU_PRIOR_OFFSET)<=0xE0U)return false;
    u32 count=cpu0_idle_timer_count(timer_frequency,ms);
    timer_stop();armed_irq_count=timer_irqs;
    Xil_Out32(TIMER_BASE+XSCUTIMER_LOAD_OFFSET,count);
    Xil_Out32(TIMER_BASE+XSCUTIMER_CONTROL_OFFSET,TIMER_CONTROL);barrier();
    return Xil_In32(TIMER_BASE+XSCUTIMER_CONTROL_OFFSET)==TIMER_CONTROL &&
        Xil_In32(TIMER_BASE+XSCUTIMER_LOAD_OFFSET)==count;
}
static bool timer_finish(void *ctx,uint32_t ms,uint64_t elapsed) {
    (void)ctx;
    bool ok=owned && Xil_In32(TIMER_BASE+XSCUTIMER_CONTROL_OFFSET)==TIMER_CONTROL;
    bool fired=timer_irqs!=armed_irq_count || (Xil_In32(TIMER_BASE+XSCUTIMER_ISR_OFFSET)&1U);
    if(ms && elapsed>=(uint64_t)ms*1000U+1000U && !fired)ok=false;
    timer_stop();if(!ok)timer_release();return ok;
}
static void wait_irq(void *ctx) {
    (void)ctx;
#if CPU0_IDLE_WFI_ENABLE
#ifdef CPU0_IDLE_HOST_TEST
    cpu0_test_wfi();
#else
    dsb();__asm__ volatile("wfi" ::: "memory");isb();
#endif
#endif
}
static const char *timer_init(void) {
#if !CPU0_IDLE_WFI_ENABLE
    return "MACRO_OFF";
#else
#if !defined(XTIMER_NO_TICK_TIMER) || defined(XSLEEPTIMER_IS_SCUTIMER)
    return "BSP_TIMER_CONFLICT";
#else
    u32 mpidr;
#ifdef CPU0_IDLE_HOST_TEST
    mpidr=cpu0_test_mpidr;
#else
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 5":"=r"(mpidr));
#endif
    if((mpidr&255U)!=0 || (mfcpsr()&0x80U) || xInterruptController.IsReady!=XIL_COMPONENT_IS_READY || !xInterruptController.Config)
        return "CPU_OR_IRQ_NOT_READY";
    XScuGic_Config *g=xInterruptController.Config;
    old_control=Xil_In32(TIMER_BASE+XSCUTIMER_CONTROL_OFFSET);
    if((old_control&7U) || (Xil_In32(TIMER_BASE+XSCUTIMER_ISR_OFFSET)&1U) ||
       (XScuGic_DistReadReg(&xInterruptController,XSCUGIC_ENABLE_SET_OFFSET)&(1U<<TIMER_IRQ)) ||
       (g->HandlerTable[TIMER_IRQ].Handler &&
        (g->HandlerTable[TIMER_IRQ].Handler!=g->HandlerTable[30].Handler ||
         g->HandlerTable[TIMER_IRQ].CallBackRef!=&xInterruptController)))return "TIMER_OWNED";
    if(XScuGic_CPUReadReg(&xInterruptController,XSCUGIC_CPU_PRIOR_OFFSET)<=0xE0U)
        return "TIMER_IRQ_MASKED";
    old_load=Xil_In32(TIMER_BASE+XSCUTIMER_LOAD_OFFSET);
    old_counter=Xil_In32(TIMER_BASE+XSCUTIMER_COUNTER_OFFSET);
    old_handler=g->HandlerTable[TIMER_IRQ];
    XScuGic_GetPriorityTriggerType(&xInterruptController,TIMER_IRQ,&old_priority,&old_trigger);
    timer_frequency=XGet_CpuFreq()/2U;
    if(!timer_frequency)return "TIMER_FREQUENCY";
    owned=1;
    if(XScuGic_Connect(&xInterruptController,TIMER_IRQ,timer_isr,0)!=XST_SUCCESS) {
        timer_release();return "TIMER_CONNECT";
    }
    XScuGic_SetPriorityTriggerType(&xInterruptController,TIMER_IRQ,0xE0,3);
    XScuGic_Enable(&xInterruptController,TIMER_IRQ);
    if(!timer_arm(0,1)) {timer_release();return "TIMER_SELFTEST_ARM";}
    uint64_t start=now_us(0);unsigned budget=1000000;
    while(timer_irqs==armed_irq_count && now_us(0)-start<20000U && --budget) { /* IRQ enabled; never WFI in self-test. */ }
    bool passed=timer_irqs!=armed_irq_count;timer_stop();
    if(!passed){timer_release();return "TIMER_SELFTEST_IRQ";}
    return "NONE";
#endif
#endif
}
static void summary(void) {
    uint64_t ms=idle_core.wait_us/1000U;
    xil_printf("@CPU0_IDLE enabled=%u entries=%u wait_ms_hi=%u wait_ms_lo=%u timer_irqs=%u reason=%s\r\n",
        (unsigned)idle_core.enabled,(unsigned)idle_core.entries,(unsigned)(ms>>32),
        (unsigned)ms,(unsigned)timer_irqs,idle_core.error);
}
static cpu_status_message_t cpu_snapshot(void) {
    cpu_status_message_t m={.seq=cpu_report_seq,.flags=idle_core.enabled?CPU_STATUS_WFI_ENABLED:0,
        .mode=CPU_MODE_BLOCKED,.reason=CPU_REASON_POWER_STATE,.entries=idle_core.entries,
        .wait_us=idle_core.wait_us,.timer_irqs=timer_irqs,.cpu_control=Xil_In32(0xF8000244U)};
    /* Policy/hardware snapshot, not a claim that the executing CPU is asleep.
     * The strict IRQ-masked WFI entry checks remain unchanged. */
    if(!idle_core.enabled) {
        m.mode=CPU_MODE_DISABLED;
        m.reason=!strcmp(idle_core.error,"MACRO_OFF")?CPU_REASON_DISABLED:CPU_REASON_TIMER;
    } else if(uart_config_active())m.reason=CPU_REASON_CONFIG;
    else if((!strcmp(afe_power_state_name(),"ready")||!strcmp(afe_power_state_name(),"sleep")) &&
            (!strcmp(tx_power_state_name(),"ready")||!strcmp(tx_power_state_name(),"sleep"))) {
        if(!both_sleep()){m.mode=CPU_MODE_ACTIVE;m.reason=CPU_REASON_POWER_ACTIVE;}
        else if(!sdio_bridge_power_idle()||!usb_transport_power_idle())m.reason=CPU_REASON_TRANSFER;
        else if(Beamform_GetRunRequest()||Beamform_GetSpiStartRequest()||hv_is_enabled()||
                !afe_power_sleep_confirmed()||!tx_power_sleep_confirmed())m.reason=CPU_REASON_HARDWARE;
        else if(frame_store_pause_retained())m.reason=CPU_REASON_PAUSE;
        else {m.mode=CPU_MODE_IDLE;m.reason=CPU_REASON_NONE;}
    }
    return m;
}
static void publish_cpu_status(void) {
    if(!cpu_report_pending || zynq_uart_idle_pending())return;
    cpu_status_message_t m=cpu_snapshot();
    char line[CPU_STATUS_LINE_BYTES];size_t n=cpu_status_format(line,sizeof line,&m);
    cpu_report_seq++;cpu_report_pending=0;
    if(n)(void)temp_hw_send(line,n); /* Existing bounded UART path, no retry loop. */
}
void cpu0_idle_init(void) {
    const cpu0_idle_ops ops={0,quiet,irq_lock,irq_restore,pending,next_ms,timer_arm,wait_irq,timer_finish,now_us};
    const char *error=timer_init();
    cpu0_idle_core_init(&idle_core,&ops,CPU0_IDLE_WFI_ENABLE && !strcmp(error,"NONE"));
    idle_core.error=error;initialized=1;led_level=XGpioPs_ReadPin(&gpiops,LED_2_EMIO_PIN)!=0;
    led_due=now_us(0)+500000U;summary();
    if(CPU0_IDLE_WFI_ENABLE && !idle_core.enabled)
        xil_printf("WARNING @CPU0_IDLE WFI unavailable; foreground polling continues\r\n");
    cpu_report_seq=0;cpu_report_temperature_seq=temperature_service_sequence();cpu_report_pending=1;
}
unsigned cpu0_idle_enabled(void) {return initialized && idle_core.enabled;}
void cpu0_idle_report_request(void) {cpu_report_pending=1;}
void cpu0_foreground_poll(void) {
    temperature_service_poll();
    u32 temperature_seq=temperature_service_sequence();
    if(temperature_seq!=cpu_report_temperature_seq){cpu_report_temperature_seq=temperature_seq;cpu_report_pending=1;}
    uart_process_commands(); /* Includes power transitions; also retained in long I/O waits. */
    usb_process_commands();
    int q=cpu0_idle_quiet();
    if(!q)sdio_bridge_poll(); /* USB build supplies a no-op; no protocol change. */
    uint64_t now=now_us(0);
    if(q)led_due=now+500000U;
    else if(now>=led_due) {led_level=!led_level;XGpioPs_WritePin(&gpiops,LED_2_EMIO_PIN,led_level);led_due=now+500000U;}
    if(last_quiet && !q)summary();
    if(last_quiet!=q)cpu_report_pending=1;
    last_quiet=q;
    publish_cpu_status(); /* Existing temperature wake/transition, no new timer. */
    bool enabled=idle_core.enabled;
    cpu0_idle_core_try(&idle_core);
    if(enabled && !idle_core.enabled){summary();cpu_report_pending=1;}
}
