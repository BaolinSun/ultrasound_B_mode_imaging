#define STARTUP_RECOVERY_HOST_TEST 1
#include "../../build/src/ps/system_ctrl/startup_recovery.c"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
int gpiops;
static uint32_t now,afe_seq,tx_seq,flags,version,control;
static int32_t temperature;
static int temp_error,cfg,uart_work,log_work,run,spi,hv,usb_busy,sdio_busy,tx_ready,pdn;
static int detach_error,fs_error,afe_error,pause_error;
static unsigned detached,fs_inits,afe_inits,pauses,reconnected;
static char trace[32];static unsigned trace_len;
static frame_store_init_diag_t diag;
static void step(char c) {assert(startup_recovery_blocks());trace[trace_len++]=c;}
uint32_t temp_hw_now_ms(void) {return now;}
int temperature_service_zynq_mc(int32_t *p) {*p=temperature;return temp_error;}
uint32_t afe_power_operation_sequence(void) {return afe_seq;}
uint32_t tx_power_operation_sequence(void) {return tx_seq;}
int uart_config_active(void) {return cfg;}
int uart_idle_pending(void) {return uart_work;}
int zynq_uart_idle_pending(void) {return log_work;}
uint32_t Beamform_GetRunRequest(void) {return (uint32_t)run;}
uint32_t Beamform_GetSpiStartRequest(void) {return (uint32_t)spi;}
uint32_t hv_is_enabled(void) {return (uint32_t)hv;}
int usb_transport_power_idle(void) {return !usb_busy;}
int sdio_bridge_power_idle(void) {return !sdio_busy;}
int tx_power_allowed(void) {return tx_ready;}
uint32_t XGpioPs_ReadPin(int *g,uint32_t pin) {assert(g==&gpiops && pin==AFE5832_PDN_GBL_PIN_EMIO);return (uint32_t)pdn;}
uint32_t Xil_In32(uint32_t a) {
    if(a==AP_BASE+AP_VERSION)return version;
    if(a==AP_BASE+AP_CONTROL)return control;
    assert(a==AP_BASE+AP_STATUS);return flags;
}
int sdio_bridge_source_recovery_begin(void) {step('D');detached++;return detach_error;}
int frame_store_init(void) {step('I');fs_inits++;return fs_error;}
const volatile frame_store_init_diag_t *frame_store_init_diagnostic(void) {return &diag;}
void afe_power_init(void) {step('A');afe_inits++;}
int afe_power_allowed(void) {return !afe_error;}
const char *afe_power_error(void) {return "INJECTED";}
int frame_store_pause(void) {step('P');pauses++;return pause_error;}
void sdio_bridge_init(void) {step('S');reconnected++;}
void xil_printf(const char *s,...) {(void)s;}
static void reset(void) {
    now=0;afe_seq=tx_seq=0;temperature=60000;
    version=AP_MAGIC;flags=AP_READY;control=0;
    temp_error=cfg=uart_work=log_work=run=spi=hv=usb_busy=sdio_busy=pdn=0;tx_ready=1;
    detach_error=fs_error=afe_error=pause_error=0;
    detached=fs_inits=afe_inits=pauses=reconnected=trace_len=0;memset(trace,0,sizeof trace);
    startup_recovery_init(-1,1);assert(startup_recovery_blocks());
}
int main(void) {
    reset();startup_recovery_init(0,1);startup_recovery_poll();assert(!detached&&!startup_recovery_blocks());
    reset();startup_recovery_init(-1,0);startup_recovery_poll();assert(!detached&&!startup_recovery_blocks());
    reset();temperature=59999;startup_recovery_poll();assert(!detached);
    temperature=60000;temp_error=1;startup_recovery_poll();assert(!detached);
    temp_error=0;
    int *busy[]={&cfg,&uart_work,&log_work,&run,&spi,&hv,&usb_busy,&sdio_busy,&pdn};
    for(unsigned i=0;i<sizeof busy/sizeof *busy;i++) {
        *busy[i]=1;startup_recovery_poll();assert(!detached);*busy[i]=0;
    }
    tx_ready=0;startup_recovery_poll();assert(!detached);tx_ready=1;
    version=0;startup_recovery_poll();assert(!detached);version=AP_MAGIC;
    control=3;startup_recovery_poll();assert(!detached);control=0;
    for(unsigned i=0;i<5;i++) {flags=AP_READY&~(1U<<i);startup_recovery_poll();assert(!detached);}
    now=30000;flags=0;startup_recovery_poll();assert(!detached);
    flags=AP_READY;startup_recovery_poll();
    assert(!startup_recovery_blocks()&&!strcmp(trace,"DIAPS")&&reconnected==1);
    /* Runtime faults/temperature changes after success cannot restart INIT. */
    flags=0;now=900000;temperature=80000;startup_recovery_poll();assert(fs_inits==1);
    reset();afe_seq++;startup_recovery_poll();assert(!startup_recovery_blocks()&&!detached);
    reset();tx_seq++;startup_recovery_poll();assert(!startup_recovery_blocks()&&!detached);
    reset();detach_error=1;startup_recovery_poll();assert(detached==1&&!fs_inits);
    detach_error=0;now=29999;startup_recovery_poll();assert(detached==1);
    now=30000;startup_recovery_poll();assert(!startup_recovery_blocks()&&fs_inits==1);
    reset();fs_error=-1;startup_recovery_poll();assert(fs_inits==1&&!afe_inits&&startup_recovery_blocks());
    fs_error=0;now=30000;startup_recovery_poll();assert(reconnected==1&&!startup_recovery_blocks());
    reset();afe_error=1;startup_recovery_poll();assert(afe_inits==1&&!pauses&&startup_recovery_blocks());
    afe_error=0;now=30000;startup_recovery_poll();assert(reconnected==1);
    reset();pause_error=-1;startup_recovery_poll();assert(pauses==1&&!reconnected&&startup_recovery_blocks());
    pause_error=0;now=30000;startup_recovery_poll();assert(reconnected==1);
    reset();now=UINT32_MAX-10000U;fs_error=-1;startup_recovery_poll();
    now+=29999U;startup_recovery_poll();assert(fs_inits==1);
    now++;fs_error=0;startup_recovery_poll();assert(fs_inits==2&&reconnected==1);
    puts("PASS: cold-boot-only recovery, valid 60 C threshold, safe gates, manual override, failures/backoff/wrap, no automatic scan/HV/upload");
}
