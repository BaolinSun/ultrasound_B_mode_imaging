#include "tx_power.h"
#include "../AFE5832/afe_power.h"
#include "../bram/bram.h"
#include "../HV/hv.h"
#include "../data_transfer/frame_store.h"
#include "../data_transfer/sdio_bridge.h"
#include "../usb/usb_transport.h"
#include "xiltimer.h"
#include "xil_io.h"
#include "sleep.h"
#include <string.h>
#include "../../../../common/tx_power_machine.h"
#include "../../../../common/tx_power_machine.c"
static tx_power_machine machine;
static int initialized;
static uint64_t ms(void *c) {(void)c;XTime t;XTime_GetTime(&t);return (uint64_t)t*1000U/(uint64_t)(COUNTS_PER_SECOND);}
static uint32_t read_reg(void *c,unsigned a) {(void)c;return Xil_In32(TP_BASE+a);}
static void write_reg(void *c,unsigned a,uint32_t v) {
    (void)c;Xil_Out32(TP_BASE+a,v);__asm__ volatile("dsb sy":::"memory");(void)Xil_In32(TP_BASE+a);
}
static void gating(void *c,bool blocked) {(void)c;usb_transport_power_vote(2U,blocked);}
static int stop_tx(void *c) {
    (void)c;hv_disable();Beamform_RequestStop();
    return hv_is_enabled() || Beamform_GetRunRequest() ? -1:0;
}
static bool idle_tx(void *c) {(void)c;return sdio_bridge_power_idle()&&usb_transport_power_idle();}
static int pause_tx(void *c) {
    (void)c;
    /* An AFE SLEEP stops the FIFO write clock. Reuse only a still-current,
     * acknowledged PAUSE; never send a new flush into that stopped domain. */
    int sleeping=!strcmp(afe_power_state_name(),"sleep");
    int rc=sleeping?frame_store_pause_retained():frame_store_pause();
    if(!rc)Beamform_Halt();
    return rc;
}
void tx_power_init(void) {
    tx_power_ops ops={0,ms,read_reg,write_reg,stop_tx,idle_tx,pause_tx,gating};
    initialized=1;tp_init(&machine,&ops);
    uint64_t until=ms(0)+100;
    while(machine.state==TX_WAKING&&ms(0)<until){tp_poll(&machine);usleep(10);}
}
void tx_power_poll(void) {if(initialized)tp_poll(&machine);}
int tx_power_allowed(void) {return initialized&&tp_allowed(&machine);}
int tx_power_idle(void) {return idle_tx(0);}
int tx_power_sleep_confirmed(void) {
    return initialized && machine.state==TX_SLEEPING && read_reg(0,TP_VERSION)==TP_MAGIC &&
        read_reg(0,TP_CONTROL)==3U && (read_reg(0,TP_STATUS)&63U)==15U &&
        read_reg(0,TP_ACK)==machine.sequence && read_reg(0,TP_SEQUENCE)==machine.sequence;
}
u32 tx_power_flags(void) {return initialized&&read_reg(0,TP_VERSION)==TP_MAGIC?read_reg(0,TP_STATUS):0;}
const char *tx_power_state_name(void) {
    if(!initialized || (machine.state==TX_READY&&!tx_power_allowed()))return "unknown";
    return tp_name(machine.state);
}
const char *tx_power_error(void) {return initialized?machine.error:"TX_NOT_INITIALIZED";}
int tx_power_command(const char *op,u32 owner) {
    if(!initialized)return -1;
    if(machine.state==TX_READY)machine.settle=0;
    tp_poll(&machine);
    if(!strcmp(afe_power_state_name(),"quiescing")||!strcmp(afe_power_state_name(),"waking")) {
        machine.error="TX_AFE_TRANSITION";return -1;
    }
    if(!strcmp(op,"PREPARE"))return tp_prepare(&machine,owner);
    if(!strcmp(op,"SLEEP"))return tp_sleep(&machine,owner);
    if(!strcmp(op,"WAKE")) {
        if(machine.state==TX_READY&&(Beamform_GetRunRequest()||hv_is_enabled()||!idle_tx(0))) {
            machine.error="TX_ALREADY_ACTIVE";return -1;
        }
        return tp_wake(&machine);
    }
    return -1;
}
