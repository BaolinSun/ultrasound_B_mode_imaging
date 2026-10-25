#include "cpu0_idle_core.h"
#include <string.h>
void cpu0_idle_core_init(cpu0_idle_core *p,const cpu0_idle_ops *ops,bool enabled)
{
    memset(p,0,sizeof(*p));p->io=*ops;p->enabled=enabled;p->error=enabled?"NONE":"DISABLED";
}
void cpu0_idle_core_try(cpu0_idle_core *p)
{
    if(!p->enabled || !p->io.quiet(p->io.ctx)) return;
    cpu0_idle_ops *o=&p->io;
    uint32_t saved=o->lock(o->ctx);
    if((saved&0x80U) || o->pending(o->ctx)) {o->restore(o->ctx,saved);return;}
    uint32_t ms=o->next_ms(o->ctx);
    if(!ms) {o->restore(o->ctx,saved);return;}
    if(!o->arm(o->ctx,ms)) {
        p->enabled=false;p->error="TIMER_ARM";o->restore(o->ctx,saved);
        (void)o->finish(o->ctx,0,0);return;
    }
    /* UART/USB or the timer may have become pending DURING timer programming. */
    if(o->pending(o->ctx) || !o->next_ms(o->ctx)) {
        o->restore(o->ctx,saved);
        if(!o->finish(o->ctx,0,0)) {p->enabled=false;p->error="TIMER_RUNTIME";}
        return;
    }
    uint64_t start=o->us(o->ctx);
    o->wait(o->ctx);
    o->restore(o->ctx,saved); /* Dispatch the pending ISR before foreground work. */
    uint64_t elapsed=o->us(o->ctx)-start;
    p->entries++;p->wait_us+=elapsed;
    if(!o->finish(o->ctx,ms,elapsed)) {p->enabled=false;p->error="TIMER_RUNTIME";}
}
uint32_t cpu0_idle_timer_count(uint32_t frequency,uint32_t ms)
{
    if(!frequency || !ms)return 0;
    /* UINT32_MAX squared plus the rounding term still fits uint64_t. */
    uint64_t count=((uint64_t)frequency*ms+255999U)/256000U;
    if(count>UINT32_MAX)return UINT32_MAX;
    return (uint32_t)count;
}
