#include "tx_power_machine.h"
#include <string.h>
static uint32_t rd(const tx_power_machine *p,unsigned a) {return p->io.read(p->io.ctx,a);}
static bool abi(const tx_power_machine *p) {return rd(p,TP_VERSION)==TP_MAGIC;}
static void fault(tx_power_machine *p,const char *s) {
    p->io.gate(p->io.ctx,true); (void)p->io.stop(p->io.ctx);
    p->state=TX_FAULT;p->error=s;
    /* A failed drain never forces STDBY or resets an in-flight SPI engine. */
}
static void issue(tx_power_machine *p,uint32_t control) {
    p->sequence=rd(p,TP_ACK)+1U;if(!p->sequence)p->sequence=1;
    p->io.write(p->io.ctx,TP_CONTROL,control);
    p->io.write(p->io.ctx,TP_SEQUENCE,p->sequence);
}
static bool ack(const tx_power_machine *p) {
    return rd(p,TP_ACK)==p->sequence && rd(p,TP_SEQUENCE)==p->sequence && !(rd(p,TP_STATUS)&TP_BUSY);
}
bool tp_allowed(const tx_power_machine *p) {
    return p->state==TX_READY && abi(p) && ack(p) &&
      (rd(p,TP_STATUS)&(TP_HELD|TP_ASLEEP|TP_CLOCK_READY|TP_ERROR))==TP_CLOCK_READY;
}
int tp_confirm_idle(tx_power_machine *p,uint32_t timeout_ms) {
    /* Control remains zero: no STDBY, HOLD, reset or clock change. A fresh
     * sequence is ACKed only after eight quiet TX-domain cycles in existing PL. */
    if(!timeout_ms || !tp_allowed(p) || rd(p,TP_CONTROL)!=0 || !p->io.idle(p->io.ctx)) return -1;
    uint64_t start=p->io.ms(p->io.ctx);
    issue(p,0);
    for(unsigned polls=0;polls<10000000U;polls++) {
        if(!abi(p)) {fault(p,"TX_CONFIG_ABI");return -1;}
        if(ack(p) && (rd(p,TP_STATUS)&63U)==(TP_IDLE|TP_CLOCK_READY) && rd(p,TP_CONTROL)==0) return 0;
        if(p->io.ms(p->io.ctx)-start>=timeout_ms) break;
    }
    fault(p,"TX_CONFIG_IDLE_TIMEOUT");return -1;
}
const char *tp_name(tx_power_state s) {
    static const char *const n[]={"unknown","ready","quiescing","sleep","waking","fault"};
    return (unsigned)s<6?n[s]:"fault";
}
void tp_init(tx_power_machine *p,const tx_power_ops *o) {
    memset(p,0,sizeof(*p));p->io=*o;p->error="NONE";o->gate(o->ctx,true);
    if(!abi(p)) {fault(p,"TX_ABI");return;}
    /* Fresh boot transaction: never trust a cached READY across a PL reset. */
    if((rd(p,TP_STATUS)&(TP_HELD|TP_ASLEEP|TP_ERROR|TP_BUSY)) || rd(p,TP_CONTROL)) {
        fault(p,"TX_BOOT_STATE");return;
    }
    p->state=TX_WAKING;p->deadline=o->ms(o->ctx)+5000;p->phase=1;issue(p,0);
}
int tp_prepare(tx_power_machine *p,uint32_t owner) {
    if(p->state==TX_SLEEPING)return 0;
    if(p->state==TX_QUIESCING&&p->owner==owner)return 0;
    if(p->state!=TX_READY||!owner){p->error="TX_OWNER_OR_STATE";return -1;}
    p->io.gate(p->io.ctx,true);
    if(p->io.stop(p->io.ctx)){fault(p,"TX_STOP_FAILED");return -1;}
    p->owner=owner;p->state=TX_QUIESCING;p->phase=0;p->error="NONE";
    p->deadline=p->io.ms(p->io.ctx)+90000;return 0;
}
int tp_sleep(tx_power_machine *p,uint32_t owner) {
    if(p->state==TX_SLEEPING)return 0;
    if(p->state!=TX_QUIESCING||p->owner!=owner){p->error="TX_OWNER_OR_STATE";return -1;}
    if(p->phase)return 0;
    if(p->io.ms(p->io.ctx)>=p->deadline){fault(p,"TX_DRAIN_TIMEOUT");return -1;}
    if(!p->io.idle(p->io.ctx)){p->error="TX_TRANSFER_BUSY";return -1;}
    if(!abi(p)){fault(p,"TX_ABI");return -1;}
    if(p->io.pause(p->io.ctx)){fault(p,"TX_DDR_DRAIN");return -1;}
    if(p->io.ms(p->io.ctx)>=p->deadline){fault(p,"TX_DRAIN_TIMEOUT");return -1;}
    issue(p,TP_HOLD|TP_SLEEP);p->phase=1;return 0;
}
int tp_wake(tx_power_machine *p) {
    if(p->state==TX_READY||p->state==TX_WAKING)return 0;
    if(!abi(p)){fault(p,"TX_ABI");return -1;}
    uint64_t deadline=p->io.ms(p->io.ctx)+5000;
    p->io.gate(p->io.ctx,true);
    if(p->io.stop(p->io.ctx)){fault(p,"TX_STOP_FAILED");return -1;}
    if(!p->io.idle(p->io.ctx)){p->error="TX_TRANSFER_BUSY";return -1;}
    /* Never replace a command whose TX clock domain has not acknowledged it. */
    if(rd(p,TP_STATUS)&TP_BUSY){p->error="TX_HW_BUSY";return -1;}
    if(p->io.pause(p->io.ctx)){fault(p,"TX_RECOVERY_DRAIN");return -1;}
    if(p->io.ms(p->io.ctx)>=deadline){fault(p,"TX_WAKE_TIMEOUT");return -1;}
    p->state=TX_WAKING;p->phase=0;p->error="NONE";
    p->deadline=deadline;issue(p,TP_HOLD);return 0;
}
void tp_poll(tx_power_machine *p) {
    uint64_t now=p->io.ms(p->io.ctx);
    if((unsigned)p->state>TX_FAULT){fault(p,"TX_STATE");return;}
    if(p->state==TX_READY) {
        if(now>=p->settle){p->settle=now+100;if(!tp_allowed(p))fault(p,"TX_CONTROL_LOST");}
        return; /* bounded health rate: do not flood AXI in the SDIO foreground */
    }
    if(p->state!=TX_WAKING&&p->state!=TX_QUIESCING)return;
    if(!abi(p)){fault(p,"TX_ABI");return;}
    if(now>=p->deadline){fault(p,p->state==TX_WAKING?"TX_WAKE_TIMEOUT":"TX_COMMIT_TIMEOUT");return;}
    if(p->state==TX_QUIESCING&&!p->phase)return;
    if(!ack(p))return;
    uint32_t f=rd(p,TP_STATUS);
    if(f&TP_ERROR){fault(p,"TX_HW_REJECTED");return;}
    if(!(f&TP_CLOCK_READY))return;
    if(p->state==TX_QUIESCING) {
        if((f&(TP_IDLE|TP_HELD|TP_ASLEEP))!=(TP_IDLE|TP_HELD|TP_ASLEEP)){fault(p,"TX_SLEEP_PROOF");return;}
        p->state=TX_SLEEPING;p->error="NONE";return;
    }
    if(p->phase==0) {
        if((f&(TP_HELD|TP_ASLEEP|TP_IDLE))!=(TP_HELD|TP_IDLE)){fault(p,"TX_WAKE_PROOF");return;}
        /* ms() is floored: two ticks guarantee >=1ms even just before a tick. */
        p->settle=now+2;p->phase=2;
    }
    if(p->phase==2&&now>=p->settle){issue(p,0);p->phase=1;return;}
    if(p->phase==1 && !(f&(TP_HELD|TP_ASLEEP))) {
        p->state=TX_READY;p->owner=0;p->error="NONE";p->settle=now+100;p->io.gate(p->io.ctx,false);
    }
}
