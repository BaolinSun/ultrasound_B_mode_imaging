#include "afe_power_machine.h"
#include <string.h>
static bool abi(afe_power_machine *p) { return p->io.read(p->io.ctx,AP_VERSION)==AP_MAGIC; }
static void fault(afe_power_machine *p,const char *why) {
    p->io.stop(p->io.ctx); p->io.gate(p->io.ctx,true);
    p->state=AFE_FAULT; p->error=why;
    /* Never stop clocks on a drain failure. A waking frame path was already
     * drained before SLEEP; it is safe to retain only the receiver in reset. */
}
const char *ap_name(afe_state s) {
    static const char *const names[]={"unknown","ready","quiescing","sleep","waking","fault"};
    return (unsigned)s<6?names[s]:"fault";
}
void ap_init(afe_power_machine *p,const afe_power_ops *o) {
    memset(p,0,sizeof(*p)); p->io=*o; p->error="NONE";
    if(!abi(p)) { fault(p,"AFE_ABI");return; }
    /* Do not alter the validated cold-start AFE register initialization. */
    if((o->read(o->ctx,AP_CONTROL)&3U) ||
       (o->read(o->ctx,AP_STATUS)&AP_READY)!=AP_READY) { fault(p,"AFE_BOOT_SYNC");return; }
    p->epoch=o->read(o->ctx,AP_EPOCH);p->state=AFE_READY;
}
int ap_prepare(afe_power_machine *p,uint32_t owner) {
    if(p->state==AFE_SLEEP) return 0;
    if(p->state==AFE_QUIESCING && p->owner==owner) return 0;
    if(p->state!=AFE_READY || !owner) {p->error="AFE_OWNER_OR_STATE";return -1;}
    p->io.gate(p->io.ctx,true);
    if(p->io.stop(p->io.ctx)) {fault(p,"AFE_STOP_FAILED");return -1;}
    p->owner=owner;p->state=AFE_QUIESCING;p->error="NONE";
    p->deadline=p->io.ms(p->io.ctx)+90000;
    return 0;
}
int ap_sleep(afe_power_machine *p,uint32_t owner) {
    if(p->state==AFE_SLEEP) return 0;
    if(p->state!=AFE_QUIESCING || owner!=p->owner) {p->error="AFE_OWNER_OR_STATE";return -1;}
    if(p->io.ms(p->io.ctx)>=p->deadline) {fault(p,"AFE_DRAIN_TIMEOUT");return -1;}
    if(!p->io.idle(p->io.ctx)) {p->error="AFE_TRANSFER_BUSY";return -1;}
    if(!abi(p)) {fault(p,"AFE_ABI");return -1;}
    if(p->io.pause(p->io.ctx)) {fault(p,"AFE_DDR_DRAIN");return -1;}
    if(p->io.save(p->io.ctx)) {fault(p,"AFE_SAVE_SPI");return -1;}
    /* Register write is ordered/read back by the adapter before the GPIO. */
    p->io.write(p->io.ctx,AP_CONTROL,AP_MMCM_RESET|AP_RX_RESET);
    if(p->io.read(p->io.ctx,AP_CONTROL)!=3U) {fault(p,"AFE_RESET_CONTROL");return -1;}
    if(p->io.pdn(p->io.ctx,true)) {fault(p,"AFE_PDN_GPIO");return -1;}
    p->state=AFE_SLEEP;p->error="NONE";
    return 0;
}
int ap_wake(afe_power_machine *p) {
    if(p->state==AFE_READY || p->state==AFE_WAKING) return 0;
    if(!abi(p)) {fault(p,"AFE_ABI");return -1;}
    p->io.gate(p->io.ctx,true);
    if(p->io.stop(p->io.ctx)) {fault(p,"AFE_STOP_FAILED");return -1;}
    /* Fault before confirmed drain must not stop the AFE/processing clock. */
    if(!p->io.idle(p->io.ctx)) {p->error="AFE_TRANSFER_BUSY";return -1;}
    if(p->state!=AFE_SLEEP && p->io.read(p->io.ctx,AP_CONTROL)!=3U && p->io.pause(p->io.ctx)) {
        fault(p,"AFE_RECOVERY_DRAIN");return -1;
    }
    p->io.write(p->io.ctx,AP_CONTROL,3);
    if(p->io.pdn(p->io.ctx,false)) {fault(p,"AFE_PDN_GPIO");return -1;}
    p->state=AFE_WAKING;p->phase=0;p->error="NONE";
    p->deadline=p->io.ms(p->io.ctx)+5000;p->settle=p->io.ms(p->io.ctx)+10;
    return 0;
}
void ap_poll(afe_power_machine *p) {
    uint64_t now=p->io.ms(p->io.ctx);
    if((unsigned)p->state>AFE_FAULT) {fault(p,"AFE_STATE");return;}
    if(p->state==AFE_READY && now>=p->settle) {
        p->settle=now+100;
        if(!abi(p) || (p->io.read(p->io.ctx,AP_STATUS)&AP_READY)!=AP_READY ||
           p->io.read(p->io.ctx,AP_EPOCH)!=p->epoch || p->io.read(p->io.ctx,AP_ACK_EPOCH)!=p->epoch) fault(p,"AFE_SYNC_LOST");
    }
    if(p->state==AFE_QUIESCING && now>=p->deadline) fault(p,"AFE_COMMIT_TIMEOUT");
    if(p->state!=AFE_WAKING) return;
    if(now>=p->deadline) {
        uint32_t f=p->io.read(p->io.ctx,AP_STATUS);
        fault(p,p->phase==0?"AFE_SETTLE_TIMEOUT":
            !(f&AP_RAW_ALIVE)?"AFE_DCLK_TIMEOUT":
            !(f&AP_MMCM_LOCKED)?"AFE_MMCM_TIMEOUT":
            !(f&AP_IDELAY_READY)?"AFE_IDELAY_TIMEOUT":"AFE_FCLK_OR_EPOCH_TIMEOUT");return;
    }
    if(!abi(p)) {fault(p,"AFE_ABI");return;}
    if(p->phase==0 && now>=p->settle) {
        int rc=p->io.restore(p->io.ctx);
        if(rc) {fault(p,rc==-2?"AFE_CONFIG_CHANGED":rc==-3?"AFE_PLL_SPI":"AFE_RESTORE_SPI");return;}
        /* Running MMCM, receiver held reset. Existing transfers are idle. */
        p->io.write(p->io.ctx,AP_CONTROL,AP_RX_RESET);p->phase=1;
    }
    uint32_t flags=p->io.read(p->io.ctx,AP_STATUS);
    if(p->phase==1 && (flags&7U)==7U) {
        uint32_t requested=p->io.read(p->io.ctx,AP_EPOCH);
        p->epoch=requested;
        if(p->io.read(p->io.ctx,AP_ACK_EPOCH)==requested && ++p->epoch==0) ++p->epoch;
        p->io.write(p->io.ctx,AP_EPOCH,p->epoch);
        p->io.write(p->io.ctx,AP_CONTROL,0);p->phase=2;
    }
    if(p->phase==2 && (flags&AP_READY)==AP_READY &&
       p->io.read(p->io.ctx,AP_ACK_EPOCH)==p->epoch) {
        /* Clock has returned: now both FIFO domains can acknowledge cleanup. */
        if(p->io.pause(p->io.ctx)) {fault(p,"AFE_WAKE_DDR_DRAIN");return;}
        if(p->io.stop(p->io.ctx)) {fault(p,"AFE_STOP_FAILED");return;}
        p->state=AFE_READY;p->owner=0;p->error="NONE";p->settle=now+100;p->io.gate(p->io.ctx,false);
    }
}
