#define CPU0_IDLE_HOST_TEST 1
#include "../../build/src/ps/system_ctrl/cpu0_idle.c"
#include <assert.h>
#include <stdio.h>

XScuGic xInterruptController;
static XScuGic_Config config;
int gpiops;
u32 cpu0_test_mpidr,cpu0_test_cpsr;
static u32 regs[4],irq_enable,hipend,priority_mask,freq;
static u8 priority,trigger;
static uint64_t clock_us,expire_at,next_temp;
static unsigned no_fire,no_arm,connect_fail,time_stopped,wfi_calls,led_writes,temp_polls,uart_polls,usb_polls,sdio_polls,fired_once;
static unsigned afe,tx,active_cfg,sdio_busy,usb_busy,uart_work,log_work,usb_work,transport_work,sdio_dma;
static unsigned scan,spi,hv,afe_proof,tx_proof,pause_error,wake_mode,logs;
static unsigned temp_reports,cpu_reports,cpu_send_fail;
static u32 cpu_control;
static cpu_status_message_t cpu_last;
static void unused(void *p){(void)p;}
static void different(void *p){(void)p;}
static void advance(void) {
    if((regs[2]&1U) && !no_fire && !fired_once && clock_us>=expire_at) {fired_once=1;regs[1]=0;regs[3]=1;hipend=29;}
    if((regs[3]&1U) && (irq_enable&(1U<<29)) && !(cpu0_test_cpsr&0x80U)) {
        hipend=1023;config.HandlerTable[29].Handler(config.HandlerTable[29].CallBackRef);
    }
}
void XTime_GetTime(XTime *t){if(!time_stopped)clock_us+=50;advance();*t=clock_us;}
void cpu0_test_barrier(void){}
void cpu0_test_restore(u32 v){cpu0_test_cpsr=v;advance();}
void Xil_ExceptionDisableMask(u32 m){cpu0_test_cpsr|=m;}
u32 Xil_In32(u32 a){if(a==0xF8000244U)return cpu_control;assert(a>=0x1000&&a<=0x100c);return regs[(a-0x1000)/4];}
void Xil_Out32(u32 a,u32 v){
    assert(a>=0x1000&&a<=0x100c);unsigned i=(a-0x1000)/4;
    if(i==2&&no_arm&&(v&1U))return;
    if(i==3){regs[3]&=~v;return;}
    regs[i]=v;
    if(i==0)regs[1]=v;
    if(i==2&&(v&1U)){fired_once=0;expire_at=clock_us+((uint64_t)regs[0]*256000000U)/(freq/2U);}
}
u32 XGet_CpuFreq(void){return freq;}
u32 XScuGic_DistReadReg(XScuGic*g,u32 a){(void)g;assert(a==0x100);return irq_enable;}
u32 XScuGic_CPUReadReg(XScuGic*g,u32 a){(void)g;assert(a==0x18||a==4);return a==4?priority_mask:hipend;}
void XScuGic_DistWriteReg(XScuGic*g,u32 a,u32 v){(void)g;assert(a==0x280&&v==(1U<<29));if(hipend==29)hipend=1023;}
void XScuGic_Disable(XScuGic*g,u32 id){(void)g;assert(id==29);irq_enable&=~(1U<<29);}
void XScuGic_Enable(XScuGic*g,u32 id){(void)g;assert(id==29);irq_enable|=1U<<29;}
int XScuGic_Connect(XScuGic*g,u32 id,void(*fn)(void *),void*p){assert(id==29);if(connect_fail)return 1;g->Config->HandlerTable[id].Handler=fn;g->Config->HandlerTable[id].CallBackRef=p;return 0;}
void XScuGic_GetPriorityTriggerType(XScuGic*g,u32 id,u8*p,u8*t){(void)g;assert(id==29);*p=priority;*t=trigger;}
void XScuGic_SetPriorityTriggerType(XScuGic*g,u32 id,u8 p,u8 t){(void)g;assert(id==29);priority=p;trigger=t;}
u32 XGpioPs_ReadPin(int*g,u32 pin){(void)g;assert(pin==54);return 1;}
void XGpioPs_WritePin(int*g,u32 pin,u32 v){(void)g;(void)v;assert(pin==54);led_writes++;}
void xil_printf(const char *fmt,...){(void)fmt;logs++;}
const char *afe_power_state_name(void){return afe==2?"fault":afe?"sleep":"ready";}
const char *tx_power_state_name(void){return tx==2?"waking":tx?"sleep":"ready";}
int uart_config_active(void){return active_cfg;}
int sdio_bridge_power_idle(void){return !sdio_busy;}
int sdio_bridge_idle_pending(void){return sdio_dma;}
int usb_transport_power_idle(void){return !usb_busy;}
int uart_idle_pending(void){return uart_work;}
int zynq_uart_idle_pending(void){return log_work;}
int usb_commands_idle_pending(void){return usb_work;}
int usb_transport_idle_pending(void){return transport_work;}
u32 Beamform_GetRunRequest(void){return scan;}
u32 Beamform_GetSpiStartRequest(void){return spi;}
u32 hv_is_enabled(void){return hv;}
int afe_power_sleep_confirmed(void){return afe_proof;}
int tx_power_sleep_confirmed(void){return tx_proof;}
int frame_store_pause_retained(void){return pause_error;}
uint32_t temperature_service_remaining_ms(void){return clock_us>=next_temp?0:(u32)((next_temp-clock_us+999)/1000);}
void temperature_service_poll(void){temp_polls++;if(clock_us>=next_temp){next_temp=clock_us+10000000U;temp_reports++;}}
uint32_t temperature_service_sequence(void){return temp_reports;}
int temp_hw_send(const char *p,size_t n){assert(!(cpu0_test_cpsr&0x80U));assert(cpu_status_parse(p,n-2,&cpu_last));cpu_reports++;return cpu_send_fail?-1:0;}
void uart_process_commands(void){uart_polls++;if(wake_mode==1&&log_work){afe=0;log_work=0;}}
void usb_process_commands(void){usb_polls++;}
void sdio_bridge_poll(void){sdio_polls++;}
void cpu0_test_wfi(void){
    assert(cpu0_test_cpsr&0x80U);wfi_calls++;
    if(wake_mode==1){clock_us+=100;log_work=1;}
    else if(wake_mode==2){no_fire=1;clock_us+=10005000;}
    else {clock_us=expire_at+10;advance();}
}
static void reset(void){
    memset(&idle_core,0,sizeof idle_core);memset(regs,0,sizeof regs);memset(&config,0,sizeof config);
    xInterruptController.Config=&config;xInterruptController.IsReady=1;
    for(unsigned i=0;i<128;i++){config.HandlerTable[i].Handler=unused;config.HandlerTable[i].CallBackRef=&xInterruptController;}
    cpu0_test_mpidr=cpu0_test_cpsr=0;owned=initialized=last_quiet=led_level=0;
    led_due=0;timer_irqs=armed_irq_count=0;
    irq_enable=0;hipend=1023;priority_mask=0xF0;freq=2000000;priority=0xA0;trigger=1;
    clock_us=0;expire_at=0;next_temp=10000000;
    no_fire=no_arm=connect_fail=time_stopped=wfi_calls=led_writes=fired_once=0;
    temp_polls=uart_polls=usb_polls=sdio_polls=0;
    afe=tx=active_cfg=sdio_busy=usb_busy=uart_work=log_work=usb_work=transport_work=0;
    sdio_dma=0;
    scan=spi=hv=pause_error=wake_mode=logs=0;afe_proof=tx_proof=1;
    temp_reports=cpu_reports=cpu_send_fail=0;cpu_control=0x22;memset(&cpu_last,0,sizeof cpu_last);
}
static void fail_init(const char *expected){cpu0_idle_init();assert(!cpu0_idle_enabled());assert(!strcmp(idle_core.error,expected));assert(!owned);}
int main(void){
    reset();assert(!cpu0_idle_enabled());cpu0_idle_init();
#if !CPU0_IDLE_WFI_ENABLE
    assert(!cpu0_idle_enabled()&&!owned&&timer_irqs==0);afe=tx=1;cpu0_foreground_poll();assert(!wfi_calls);
    assert(cpu_reports==1&&cpu_last.mode==CPU_MODE_DISABLED&&cpu_last.reason==CPU_REASON_DISABLED&&cpu_last.cpu_control==0x22);
#else
    assert(cpu0_idle_enabled()&&timer_irqs==1);
    for(unsigned a=0;a<2;a++)for(unsigned t=0;t<2;t++){afe=a;tx=t;assert(cpu0_idle_quiet()==(a&&t));}
    afe=tx=1;active_cfg=1;assert(!cpu0_idle_quiet());active_cfg=0;
    sdio_busy=1;assert(!cpu0_idle_quiet());sdio_busy=0;usb_busy=1;assert(!cpu0_idle_quiet());usb_busy=0;
    unsigned *guards[]={&uart_work,&log_work,&usb_work,&transport_work,&sdio_dma,&scan,&spi,&hv,&pause_error};
    for(unsigned i=0;i<sizeof guards/sizeof guards[0];i++){*guards[i]=1;cpu0_foreground_poll();*guards[i]=0;assert(!wfi_calls);}
    afe_proof=0;cpu0_foreground_poll();afe_proof=1;tx_proof=0;cpu0_foreground_poll();tx_proof=1;assert(!wfi_calls);
    hipend=59;cpu0_foreground_poll();hipend=1023;assert(!wfi_calls);
    cpu0_foreground_poll();assert(wfi_calls==1&&cpu0_idle_enabled()&&!sdio_polls&&!led_writes);
    assert(timer_irqs==2&&!hv&&!scan);cpu0_foreground_poll();assert(temp_polls>1);
    reset();cpu0_idle_init();afe=tx=1;wake_mode=1;cpu0_foreground_poll();assert(log_work&&wfi_calls==1);
    cpu0_foreground_poll();assert(!afe&&tx&&sdio_polls==1&&!hv&&!scan); /* No automatic analog wake/scan/HV. */
    clock_us+=600000;cpu0_foreground_poll();assert(led_writes==1);
    reset();cpu0_idle_init();afe=tx=1;wake_mode=2;cpu0_foreground_poll();assert(!cpu0_idle_enabled()&&!owned&&!strcmp(idle_core.error,"TIMER_RUNTIME"));
    reset();cpu0_idle_init();afe=tx=1;no_arm=1;cpu0_foreground_poll();assert(!cpu0_idle_enabled()&&!owned);
    reset();cpu0_idle_init();afe=tx=1;irq_enable=0;cpu0_foreground_poll();assert(!cpu0_idle_enabled()&&!wfi_calls);
    reset();cpu0_idle_init();afe=tx=1;priority_mask=0xE0;cpu0_foreground_poll();assert(!cpu0_idle_enabled()&&!wfi_calls);
    reset();cpu0_test_mpidr=1;fail_init("CPU_OR_IRQ_NOT_READY");
    reset();cpu0_test_cpsr=0x80;fail_init("CPU_OR_IRQ_NOT_READY");
    reset();xInterruptController.IsReady=0;fail_init("CPU_OR_IRQ_NOT_READY");
    reset();xInterruptController.Config=0;fail_init("CPU_OR_IRQ_NOT_READY");
    reset();regs[2]=1;fail_init("TIMER_OWNED");assert(regs[2]==1);
    reset();regs[3]=1;fail_init("TIMER_OWNED");
    reset();irq_enable=1U<<29;fail_init("TIMER_OWNED");assert(irq_enable==(1U<<29));
    reset();config.HandlerTable[29].Handler=different;fail_init("TIMER_OWNED");
    reset();config.HandlerTable[29].CallBackRef=0;fail_init("TIMER_OWNED");
    reset();priority_mask=0xE0;fail_init("TIMER_IRQ_MASKED");
    reset();freq=0;fail_init("TIMER_FREQUENCY");
    reset();connect_fail=1;fail_init("TIMER_CONNECT");assert(priority==0xA0&&trigger==1);
    reset();no_arm=1;fail_init("TIMER_SELFTEST_ARM");
    reset();no_fire=1;fail_init("TIMER_SELFTEST_IRQ");
    reset();time_stopped=1;fail_init("TIMER_SELFTEST_IRQ");
    reset();cpu0_idle_init();assert(!timer_arm(0,0));owned=0;assert(!timer_arm(0,1));timer_release();
    owned=1;timer_frequency=0;assert(!timer_arm(0,1));timer_release();
    reset();cpu0_idle_init();timer_isr(0);assert(timer_irqs==1); /* Spurious IRQ doesn't fabricate expiry. */
    reset();cpu0_idle_init();assert(cpu_snapshot().mode==CPU_MODE_ACTIVE);
    active_cfg=1;assert(cpu_snapshot().reason==CPU_REASON_CONFIG);active_cfg=0;
    afe=2;assert(cpu_snapshot().reason==CPU_REASON_POWER_STATE);afe=0;tx=2;assert(cpu_snapshot().reason==CPU_REASON_POWER_STATE);
    afe=tx=1;sdio_busy=1;assert(cpu_snapshot().reason==CPU_REASON_TRANSFER);sdio_busy=0;usb_busy=1;assert(cpu_snapshot().reason==CPU_REASON_TRANSFER);usb_busy=0;
    unsigned *hw[]={&scan,&spi,&hv};
    for(unsigned i=0;i<3;i++){*hw[i]=1;assert(cpu_snapshot().reason==CPU_REASON_HARDWARE);*hw[i]=0;}
    afe_proof=0;assert(cpu_snapshot().reason==CPU_REASON_HARDWARE);afe_proof=1;tx_proof=0;assert(cpu_snapshot().reason==CPU_REASON_HARDWARE);tx_proof=1;
    pause_error=1;assert(cpu_snapshot().reason==CPU_REASON_PAUSE);pause_error=0;
    cpu_control=0;assert(cpu_snapshot().mode==CPU_MODE_IDLE&&cpu_snapshot().cpu_control==0); /* CPU1 warning-only policy unchanged. */
    log_work=1;publish_cpu_status();assert(cpu_reports==0&&cpu_report_pending);log_work=0;
    cpu_send_fail=1;publish_cpu_status();assert(cpu_reports==1&&!cpu_report_pending);publish_cpu_status();assert(cpu_reports==1);cpu_send_fail=0;
    cpu0_foreground_poll();cpu0_foreground_poll();assert(cpu_reports>=2&&cpu_last.entries>0&&cpu_last.wait_us>0);
    reset();cpu0_idle_init();idle_core.enabled=false;assert(cpu_snapshot().mode==CPU_MODE_DISABLED&&cpu_snapshot().reason==CPU_REASON_TIMER);
#endif
    /* Explicit STATUS/CPU1 commands request a passive report; no timer is armed. */
    unsigned prior_irqs=timer_irqs;
    cpu_report_pending=0;log_work=0;cpu0_idle_report_request();
    assert(cpu_report_pending&&timer_irqs==prior_irqs);
    publish_cpu_status();assert(!cpu_report_pending&&timer_irqs==prior_irqs);
    puts("PASS CPU0 adapter: private timer/selftest/conflicts, IRQs, hardware/queue guards, wake, fallback, foreground/LED");
    return 0;
}

void startup_recovery_poll(void) {}
