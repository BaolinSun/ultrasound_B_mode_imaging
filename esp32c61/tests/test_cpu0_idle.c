#include "cpu0_idle_core.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {
    unsigned afe,tx,masked,phase,pending_calls,next_calls,arm_calls,waits,restores,finishes;
    unsigned pending_first,pending_second,due_first,due_second,arm_fail,finish_fail,queue;
    uint64_t time,delta;
    unsigned last_restore;
} model;
static bool quiet(void *p){model*m=p;return m->afe==1&&m->tx==1;}
static uint32_t lock(void *p){model*m=p;unsigned old=m->masked;m->masked=0x80;return old;}
static void restore(void*p,uint32_t v){model*m=p;m->masked=v;m->restores++;m->last_restore=v;}
static bool pending(void *p){model*m=p;assert(m->masked==0x80);m->pending_calls++;return m->pending_calls==1?m->pending_first:m->pending_second;}
static uint32_t next(void*p){model*m=p;m->next_calls++;return (m->next_calls==1?m->due_first:m->due_second)?0:10000;}
static bool arm(void*p,uint32_t ms){model*m=p;assert(ms==10000&&m->masked==0x80);m->arm_calls++;return !m->arm_fail;}
static void wait(void*p){model*m=p;assert(m->masked==0x80);m->waits++;m->time+=m->delta;m->queue++;}
static bool finish(void*p,uint32_t ms,uint64_t elapsed){model*m=p;(void)elapsed;assert(!m->masked);assert(ms==0||ms==10000);m->finishes++;return !m->finish_fail;}
static uint64_t us(void*p){return ((model*)p)->time;}
static cpu0_idle_core setup(model *m,bool enabled){cpu0_idle_core c;memset(m,0,sizeof(*m));m->afe=m->tx=1;m->delta=10000000;
    cpu0_idle_ops o={m,quiet,lock,restore,pending,next,arm,wait,finish,us};cpu0_idle_core_init(&c,&o,enabled);return c;}
int main(void){
    model m;cpu0_idle_core c=setup(&m,false);cpu0_idle_core_try(&c);assert(!m.pending_calls&&!m.waits);
    for(unsigned a=0;a<6;a++)for(unsigned t=0;t<6;t++){
        c=setup(&m,true);m.afe=a;m.tx=t;cpu0_idle_core_try(&c);assert(m.waits==(a==1&&t==1));
    }
    c=setup(&m,true);m.masked=0x80;cpu0_idle_core_try(&c);assert(m.last_restore==0x80&&!m.arm_calls);
    c=setup(&m,true);m.pending_first=1;cpu0_idle_core_try(&c);assert(!m.waits&&!m.arm_calls&&m.restores==1);
    c=setup(&m,true);m.due_first=1;cpu0_idle_core_try(&c);assert(!m.arm_calls);
    c=setup(&m,true);m.arm_fail=1;cpu0_idle_core_try(&c);assert(!c.enabled&&!strcmp(c.error,"TIMER_ARM")&&m.finishes==1);
    c=setup(&m,true);m.pending_second=1;cpu0_idle_core_try(&c);assert(c.enabled&&!m.waits&&m.finishes==1);
    c=setup(&m,true);m.due_second=1;cpu0_idle_core_try(&c);assert(!m.waits&&m.finishes==1);
    c=setup(&m,true);m.pending_second=m.finish_fail=1;cpu0_idle_core_try(&c);assert(!c.enabled&&!strcmp(c.error,"TIMER_RUNTIME"));
    c=setup(&m,true);m.finish_fail=1;cpu0_idle_core_try(&c);assert(!c.enabled&&c.entries==1);
    c=setup(&m,true);m.delta=1;cpu0_idle_core_try(&c);assert(c.entries==1&&c.wait_us==1&&m.queue==1); /* IRQ after final check. */
    c=setup(&m,true);m.time=UINT64_MAX-9;m.delta=20;cpu0_idle_core_try(&c);assert(c.entries==1&&c.wait_us==20);
    assert(cpu0_idle_timer_count(0,1)==0&&cpu0_idle_timer_count(1,0)==0);
    assert(cpu0_idle_timer_count(1,1)==1);
    assert(cpu0_idle_timer_count(333333333U,10000)==13020834);
    assert(cpu0_idle_timer_count(UINT32_MAX,UINT32_MAX)==UINT32_MAX);
    puts("PASS CPU0 core: power combinations, pending IRQ/queue race, due/wrap, arm/runtime fallback, timer bounds");
    return 0;
}
