#include "cpu1_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint32_t ctrl, initial, locked;
    uint64_t time, step;
    unsigned core, reads, writes, resets, clocks, barriers;
    unsigned no_unlock, no_reset, no_clock, no_lock, corrupt, stopped;
} model;
static unsigned core(void *p) { return ((model *)p)->core; }
static uint32_t rd(void *p, uint32_t a) {
    model *m=p;m->reads++;
    if(a==CPU1_RESET_CTRL)return m->ctrl;
    assert(a==CPU1_SLCR_LOCKSTA);return m->locked;
}
static void wr(void *p, uint32_t a, uint32_t v) {
    model *m=p;m->writes++;
    if(a==CPU1_SLCR_UNLOCK){assert(v==0xDF0D);if(!m->no_unlock)m->locked=0;return;}
    if(a==CPU1_SLCR_LOCK){assert(v==0x767B);if(!m->no_lock)m->locked=1;return;}
    assert(a==CPU1_RESET_CTRL && !m->locked);
    assert((v&~CPU1_DISABLED_MASK)==(m->initial&~CPU1_DISABLED_MASK));
    assert((v & m->ctrl)==m->ctrl); /* Never clear any existing control bit. */
    if(!m->resets){m->resets++;if(!m->no_reset)m->ctrl=v;}
    else {assert(m->ctrl&CPU1_RESET_BIT);m->clocks++;if(!m->no_clock)m->ctrl=v;}
    if(m->corrupt)m->ctrl^=0x100;
}
static void barrier(void *p){((model *)p)->barriers++;}
static uint64_t ticks(void *p){model*m=p;if(!m->stopped)m->time+=m->step;return m->time;}
static cpu1_result run(model *m){cpu1_ops o={m,core,rd,wr,barrier,ticks,10};return cpu1_disable(&o);}
static model fresh(uint32_t ctrl,unsigned lock){model m;memset(&m,0,sizeof m);m.initial=m.ctrl=ctrl;m.locked=lock;m.step=1;return m;}
int main(void){
    cpu1_result r;model m;
    for(unsigned locked=0;locked<2;locked++)for(unsigned partial=0;partial<4;partial++){
        uint32_t bits=(partial&1?CPU1_RESET_BIT:0)|(partial&2?CPU1_CLOCK_BIT:0);
        m=fresh(0x101U|bits,locked);r=run(&m);
        assert(r.error==CPU1_OK&&r.before==(0x101U|bits)&&r.after==(0x101U|CPU1_DISABLED_MASK));
        assert(m.locked==locked&&r.lock_restored);
        assert(m.resets==(partial==3?0U:1U)&&m.clocks==(partial==3?0U:1U));
        unsigned writes=m.writes;r=run(&m);assert(r.error==CPU1_OK&&m.writes==writes);
    }
    m=fresh(0,1);m.core=1;r=run(&m);assert(r.error==CPU1_NOT_CPU0&&!m.reads&&!m.writes);
    m=fresh(0,1);m.no_unlock=1;r=run(&m);assert(r.error==CPU1_UNLOCK_FAILED&&!m.resets&&m.locked);
    m=fresh(0,1);m.no_reset=1;r=run(&m);assert(r.error==CPU1_RESET_TIMEOUT&&!m.clocks&&m.locked&&m.time>=10);
    m=fresh(0,0);m.no_clock=1;r=run(&m);assert(r.error==CPU1_CLOCK_TIMEOUT&&!m.locked&&(r.after&2));
    m=fresh(0,1);m.corrupt=1;r=run(&m);assert(r.error==CPU1_OTHER_BITS_CHANGED&&!m.clocks&&m.locked);
    m=fresh(0,1);m.no_lock=1;r=run(&m);assert(r.error==CPU1_LOCK_RESTORE_FAILED&&!r.lock_restored);
    m=fresh(0,1);m.no_reset=m.no_lock=1;r=run(&m);assert(r.error==CPU1_RESET_TIMEOUT&&!r.lock_restored);
    m=fresh(0,1);m.stopped=m.no_reset=1;r=run(&m);assert(r.error==CPU1_RESET_TIMEOUT&&m.reads<100010&&m.locked);
    m=fresh(0,1);m.time=UINT64_MAX-5;m.no_clock=1;r=run(&m);assert(r.error==CPU1_CLOCK_TIMEOUT&&m.locked);
    for(unsigned e=0;e<=CPU1_LOCK_RESTORE_FAILED;e++)assert(strcmp(cpu1_error_name((cpu1_error)e),"UNKNOWN"));
    assert(!strcmp(cpu1_error_name((cpu1_error)100),"UNKNOWN"));
    puts("PASS CPU1: ordered RMW, other bits, lock restore, repeat, timeout, stopped timer, non-CPU0");
    return 0;
}
