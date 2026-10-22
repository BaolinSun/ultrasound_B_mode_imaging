#include "../../common/afe_power_machine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t now;
static uint32_t regs[16];
static unsigned off_count,stops,writes;
static bool idle=true,gate;
static int fail_pause,fail_save,fail_restore;
static int fail_pdn,ignore_write;
static int fail_stop;
static uint64_t ms(void *c){(void)c;return now;}
static uint32_t rd(void *c,unsigned a){(void)c;return regs[a/4];}
static void wr(void *c,unsigned a,uint32_t v){(void)c;writes++;if(!ignore_write)regs[a/4]=v;}
static int pdn(void *c,bool off){(void)c;if(off)off_count++;return fail_pdn;}
static int stop(void *c){(void)c;stops++;return fail_stop;}
static bool is_idle(void *c){(void)c;return idle;}
static int pause_io(void *c){(void)c;return fail_pause;}
static int save(void *c){(void)c;return fail_save;}
static int restore(void *c){(void)c;return fail_restore;}
static void gating(void *c,bool on){(void)c;gate=on;}
static afe_power_machine p;
static void init(void){
    memset(regs,0,sizeof(regs));regs[AP_VERSION/4]=AP_MAGIC;regs[AP_STATUS/4]=AP_READY;
    idle=true;now=0;off_count=stops=writes=0;fail_pause=fail_save=fail_restore=0;fail_pdn=ignore_write=fail_stop=0;gate=false;
    afe_power_ops o={0,ms,rd,wr,pdn,stop,is_idle,pause_io,save,restore,gating};ap_init(&p,&o);assert(p.state==AFE_READY);
}
int main(void){
    init();assert(ap_prepare(&p,10)==0&&gate&&stops==1);assert(ap_prepare(&p,10)==0);
    assert(ap_prepare(&p,11)<0);assert(ap_sleep(&p,11)<0&&off_count==0);
    idle=false;assert(ap_sleep(&p,10)<0&&off_count==0);idle=true;
    assert(ap_sleep(&p,10)==0&&p.state==AFE_SLEEP&&off_count==1&&regs[1]==3);
    assert(ap_sleep(&p,10)==0&&off_count==1);assert(ap_wake(&p)==0&&p.state==AFE_WAKING);
    now=9;ap_poll(&p);assert(p.phase==0);now=10;ap_poll(&p);assert(p.phase==2&&regs[AP_EPOCH/4]==1);
    ap_poll(&p);assert(p.state==AFE_WAKING); // old lock, wrong epoch cannot pass
    regs[AP_ACK_EPOCH/4]=1;ap_poll(&p);assert(p.state==AFE_READY&&!gate);
    assert(ap_wake(&p)==0&&p.state==AFE_READY);
    init();fail_pause=1;ap_prepare(&p,1);assert(ap_sleep(&p,1)<0&&off_count==0&&p.state==AFE_FAULT);
    assert(ap_wake(&p)<0&&regs[AP_CONTROL/4]==0); // failure cannot stop clocks
    init();fail_save=1;ap_prepare(&p,1);assert(ap_sleep(&p,1)<0&&off_count==0);
    init();ap_prepare(&p,1);now=90000;ap_poll(&p);assert(p.state==AFE_FAULT&&off_count==0);
    init();ap_prepare(&p,1);now=90000;assert(ap_sleep(&p,1)<0&&off_count==0);
    init();ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);now=5000;ap_poll(&p);assert(p.state==AFE_FAULT&&gate);
    init();ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);fail_restore=1;now=10;ap_poll(&p);assert(p.state==AFE_FAULT&&gate);
    init();regs[AP_VERSION/4]=0;ap_prepare(&p,1);assert(ap_sleep(&p,1)<0&&off_count==0);
    init();regs[AP_STATUS/4]=0;ap_poll(&p);assert(p.state==AFE_FAULT&&gate);
    init();regs[AP_STATUS/4]=0;afe_power_ops o=p.io;ap_init(&p,&o);assert(p.state==AFE_FAULT);
    assert(!strcmp(ap_name((afe_state)99),"fault"));
    init();assert(ap_sleep(&p,1)<0);assert(ap_prepare(&p,0)<0);
    ap_prepare(&p,1);ap_sleep(&p,1);assert(ap_prepare(&p,1)==0);ap_wake(&p);assert(ap_wake(&p)==0);
    regs[AP_STATUS/4]=0;now=10;ap_poll(&p);assert(p.phase==1);regs[AP_STATUS/4]=7;ap_poll(&p);assert(p.phase==2);
    regs[AP_STATUS/4]=31;regs[AP_ACK_EPOCH/4]=p.epoch;fail_pause=1;ap_poll(&p);assert(p.state==AFE_FAULT);
    init();ap_prepare(&p,1);ignore_write=1;assert(ap_sleep(&p,1)<0&&off_count==0);
    init();ap_prepare(&p,1);fail_pdn=1;assert(ap_sleep(&p,1)<0&&p.state==AFE_FAULT);
    init();ap_prepare(&p,1);ap_sleep(&p,1);fail_pdn=1;assert(ap_wake(&p)<0);
    init();ap_prepare(&p,1);ap_sleep(&p,1);idle=false;assert(ap_wake(&p)<0&&p.state==AFE_SLEEP);
    init();regs[AP_VERSION/4]=0;assert(ap_wake(&p)==0);p.state=AFE_FAULT;assert(ap_wake(&p)<0);
    afe_power_ops o2=p.io;ap_init(&p,&o2);assert(p.state==AFE_FAULT);
    init();ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);regs[AP_VERSION/4]=0;ap_poll(&p);assert(p.state==AFE_FAULT);
    init();regs[AP_EPOCH/4]=UINT32_MAX;regs[AP_ACK_EPOCH/4]=UINT32_MAX;ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);now=10;ap_poll(&p);assert(p.epoch==1);
    init();regs[AP_EPOCH/4]=3;regs[AP_ACK_EPOCH/4]=2;ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);now=10;ap_poll(&p);assert(p.epoch==3);
    init();p.state=(afe_state)9;ap_poll(&p);assert(p.state==AFE_FAULT);
    for(int code=-3;code<=-2;code++) {init();ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);now=10;fail_restore=code;ap_poll(&p);assert(p.state==AFE_FAULT);}
    const unsigned masks[]={0,1,3,7,31};
    for(unsigned i=0;i<5;i++) {init();ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);now=10;regs[AP_STATUS/4]=masks[i];ap_poll(&p);now=6000;ap_poll(&p);assert(p.state==AFE_FAULT);}
    init();p.epoch=1;ap_poll(&p);assert(p.state==AFE_FAULT); // PL reset epoch mismatch
    init();fail_stop=1;assert(ap_prepare(&p,1)<0&&p.state==AFE_FAULT&&off_count==0);
    init();ap_prepare(&p,1);ap_sleep(&p,1);fail_stop=1;assert(ap_wake(&p)<0&&p.state==AFE_FAULT);
    init();ap_prepare(&p,1);ap_sleep(&p,1);ap_wake(&p);now=10;ap_poll(&p);regs[AP_ACK_EPOCH/4]=p.epoch;fail_stop=1;ap_poll(&p);assert(p.state==AFE_FAULT);
    puts("PASS afe_power_machine");return 0;
}
