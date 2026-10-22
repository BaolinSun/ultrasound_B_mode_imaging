#include "afe_power.h"
#include "afe5832.h"
#include "../GPIO/gpio.h"
#include "../HV/hv.h"
#include "../bram/bram.h"
#include "../data_transfer/frame_store.h"
#include "../data_transfer/sdio_bridge.h"
#include "../usb/usb_transport.h"
#include "xil_io.h"
#include "xiltimer.h"
#include "sleep.h"
#include <string.h>
#include "../../../../common/afe_power_machine.h"
#include "../../../../common/afe_power_machine.c"
static afe_power_machine machine;
static int initialized;
static const u8 saved_addresses[]={1,2,3,4,0xc6,0xc7,0xc8,0x41,0x42};
static u32 saved[sizeof(saved_addresses)];
static int have_saved;
static uint64_t ms(void *c) { (void)c;XTime n;XTime_GetTime(&n);return (uint64_t)n*1000U/(uint64_t)(COUNTS_PER_SECOND); }
static uint32_t rd(void *c,unsigned offset) { (void)c;return Xil_In32(AP_BASE+offset); }
static void wr(void *c,unsigned offset,uint32_t value) { (void)c;Xil_Out32(AP_BASE+offset,value);__asm__ volatile("dsb sy":::"memory");(void)Xil_In32(AP_BASE+offset); }
static int pdn(void *c,bool off) {
    (void)c;XGpioPs_WritePin(&gpiops,AFE5832_PDN_FAST_PIN_EMIO,0);
    XGpioPs_WritePin(&gpiops,AFE5832_PDN_GBL_PIN_EMIO,off?1:0);
    return (XGpioPs_ReadPin(&gpiops,AFE5832_PDN_GBL_PIN_EMIO)!=0)==off?0:-1;
}
static int stop(void *c) {
    (void)c;hv_disable();Beamform_Halt();
    return hv_is_enabled() || Beamform_GetRunRequest() || Beamform_GetSpiStartRequest()?-1:0;
}
static bool idle(void *c) { (void)c;return sdio_bridge_power_idle() && usb_transport_power_idle(); }
static int pause_frame(void *c) { (void)c;return frame_store_pause(); }
static void gate(void *c,bool blocked) { (void)c;usb_transport_power_gate(blocked); }
static int save(void *c) {
    (void)c;have_saved=0;
    for(unsigned i=0;i<sizeof(saved_addresses);i++)
        if(AFE5832_ReadRegMap(AFE5832_REG_MAP_NORMAL,saved_addresses[i],&saved[i])!=XST_SUCCESS) return -1;
    have_saved=1;return 0;
}
static int restore(void *c) {
    (void)c;u32 v;
    /* Never reset the AFE or overwrite the user's ADC/VCA/DTGC configuration. */
    if(AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL)!=XST_SUCCESS) return -1;
    for(unsigned i=0;i<sizeof(saved_addresses);i++) {
        if(AFE5832_ReadRegMap(AFE5832_REG_MAP_NORMAL,saved_addresses[i],&v)!=XST_SUCCESS) return -1;
        if(have_saved && (v&((i>=7)?0x7fffU:0xffffU))!=(saved[i]&((i>=7)?0x7fffU:0xffffU))) return -2;
    }
    /* SBAS922 10.4: >=100 us clock stability, both PLL resets >=10 us. */
    usleep(100);
    u32 a=0,b=0;
    if(AFE5832_ReadRegMap(0,0x41,&a)!=XST_SUCCESS || AFE5832_ReadRegMap(0,0x42,&b)!=XST_SUCCESS) return -1;
    if(AFE5832_WriteRegMap(0,0x41,a|0x8000)!=XST_SUCCESS || AFE5832_WriteRegMap(0,0x42,b|0x8000)!=XST_SUCCESS) return -3;
    usleep(20);
    if(AFE5832_WriteRegMap(0,0x41,a&0x7fff)!=XST_SUCCESS || AFE5832_WriteRegMap(0,0x42,b&0x7fff)!=XST_SUCCESS) return -3;
    XGpioPs_WritePin(&gpiops,AFE5832_TX_TRIG_PIN_EMIO,0);usleep(2);
    XGpioPs_WritePin(&gpiops,AFE5832_TX_TRIG_PIN_EMIO,1);usleep(2);
    XGpioPs_WritePin(&gpiops,AFE5832_TX_TRIG_PIN_EMIO,0);
    return 0;
}
void afe_power_init(void) {
    afe_power_ops o={0,ms,rd,wr,pdn,stop,idle,pause_frame,save,restore,gate};
    u32 boot_epoch=0;
    /* Bind cold boot to a nonzero receive epoch: a later PL reset back to zero
     * cannot silently resurrect a READY state cached by the PS. No PLL/AFE
     * register initialization is repeated and the processing clock keeps running. */
    if(rd(0,AP_VERSION)==AP_MAGIC && rd(0,AP_CONTROL)==0 && (rd(0,AP_STATUS)&AP_READY)==AP_READY) {
        u32 epoch=rd(0,AP_EPOCH)+1U;if(!epoch)epoch=1;
        boot_epoch=epoch;
        wr(0,AP_CONTROL,AP_RX_RESET);wr(0,AP_EPOCH,epoch);wr(0,AP_CONTROL,0);
        uint64_t until=ms(0)+100;
        while(ms(0)<until && (rd(0,AP_ACK_EPOCH)!=epoch || (rd(0,AP_STATUS)&AP_READY)!=AP_READY)) usleep(10);
    }
    initialized=1;ap_init(&machine,&o);
    if(machine.state==AFE_READY && (!boot_epoch || rd(0,AP_EPOCH)!=boot_epoch || rd(0,AP_ACK_EPOCH)!=boot_epoch)) {
        machine.state=AFE_FAULT;machine.error="AFE_BOOT_EPOCH";stop(0);gate(0,true);
    }
}
void afe_power_poll(void) { if(initialized) ap_poll(&machine); }
int afe_power_idle(void) { return idle(0); }
int afe_power_allowed(void) {
    return initialized && machine.state==AFE_READY && rd(0,AP_VERSION)==AP_MAGIC &&
        (rd(0,AP_STATUS)&AP_READY)==AP_READY && rd(0,AP_EPOCH)==machine.epoch && rd(0,AP_ACK_EPOCH)==machine.epoch;
}
const char *afe_power_state_name(void) {
    if(!initialized || (machine.state==AFE_READY && !afe_power_allowed())) return "unknown";
    return ap_name(machine.state);
}
const char *afe_power_error(void) { return initialized?machine.error:"AFE_NOT_INITIALIZED"; }
u32 afe_power_flags(void) { return initialized && rd(0,AP_VERSION)==AP_MAGIC?rd(0,AP_STATUS):0; }
int afe_power_command(const char *op,u32 owner) {
    if(!initialized) return -1;
    if(machine.state==AFE_READY) {machine.settle=0;ap_poll(&machine);}
    if(!strcmp(op,"WAKE") && machine.state==AFE_READY &&
       (Beamform_GetRunRequest() || hv_is_enabled() || !idle(0))) {
        machine.error="AFE_ALREADY_ACTIVE";return -1;
    }
    if(!strcmp(op,"PREPARE")) return ap_prepare(&machine,owner);
    if(!strcmp(op,"SLEEP")) return ap_sleep(&machine,owner);
    if(!strcmp(op,"WAKE")) return ap_wake(&machine);
    return -1;
}
