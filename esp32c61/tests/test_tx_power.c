#include "../../common/tx_power_machine.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint64_t now;
static uint32_t r[16];
static bool gated,idle;
static int stop_error,pause_error;
static unsigned issues,pauses,pause_delay;
static uint64_t ms(void *c){(void)c;return now;}
static uint32_t read_reg(void *c,unsigned a){(void)c;return r[a/4];}
static void write_reg(void *c,unsigned a,uint32_t v){(void)c;r[a/4]=v;if(a==TP_SEQUENCE){issues++;r[TP_STATUS/4]|=TP_BUSY;}}
static int stop(void *c){(void)c;return stop_error;}
static bool io_idle(void *c){(void)c;return idle;}
static int pause_io(void *c){(void)c;pauses++;now+=pause_delay;return pause_error;}
static void gate(void *c,bool v){(void)c;gated=v;}
static tx_power_machine p;
static void ack(void){
    r[TP_ACK/4]=p.sequence;
    r[TP_STATUS/4]=TP_IDLE|TP_CLOCK_READY|((r[TP_CONTROL/4]&1)?TP_HELD:0)|((r[TP_CONTROL/4]&2)?TP_ASLEEP:0);
}
static void init(void){
    memset(r,0,sizeof(r));r[0]=TP_MAGIC;now=0;idle=true;stop_error=pause_error=0;issues=pauses=pause_delay=0;
    tx_power_ops o={0,ms,read_reg,write_reg,stop,io_idle,pause_io,gate};tp_init(&p,&o);
    assert(p.state==TX_WAKING&&gated);ack();tp_poll(&p);assert(tp_allowed(&p)&&!gated);
}
int main(void){
    init();assert(tp_prepare(&p,7)==0&&gated);assert(tp_prepare(&p,7)==0);
    assert(tp_prepare(&p,8)<0);assert(tp_sleep(&p,8)<0);idle=false;assert(tp_sleep(&p,7)<0&&issues==1);
    idle=true;assert(tp_sleep(&p,7)==0);assert(tp_sleep(&p,7)==0&&issues==2);
    tp_poll(&p);assert(p.state==TX_QUIESCING);ack();tp_poll(&p);assert(p.state==TX_SLEEPING&&gated);
    assert(tp_sleep(&p,7)==0&&issues==2);assert(tp_prepare(&p,7)==0);
    assert(tp_wake(&p)==0);assert(tp_wake(&p)==0&&issues==3);tp_poll(&p);assert(p.phase==0);
    ack();tp_poll(&p);assert(p.phase==2&&gated);tp_poll(&p);assert(issues==3);
    now=1;tp_poll(&p);assert(p.phase==2&&issues==3); // fractional-ms guard
    now=2;tp_poll(&p);assert(p.phase==1&&issues==4&&gated);tp_poll(&p);assert(p.state==TX_WAKING);
    ack();tp_poll(&p);assert(tp_allowed(&p)&&!gated);assert(tp_wake(&p)==0);
    init();pause_error=1;tp_prepare(&p,1);assert(tp_sleep(&p,1)<0&&p.state==TX_FAULT&&issues==1);
    init();tp_prepare(&p,1);now=90000;tp_poll(&p);assert(p.state==TX_FAULT&&issues==1);
    init();tp_prepare(&p,1);now=90000;assert(tp_sleep(&p,1)<0);
    init();tp_prepare(&p,1);tp_sleep(&p,1);ack();r[TP_STATUS/4]|=TP_ERROR;tp_poll(&p);assert(p.state==TX_FAULT);
    init();tp_prepare(&p,1);tp_sleep(&p,1);r[TP_ACK/4]=p.sequence-1;tp_poll(&p);assert(p.state==TX_QUIESCING);
    now=90000;tp_poll(&p);assert(p.state==TX_FAULT);assert(tp_wake(&p)<0&&!strcmp(p.error,"TX_HW_BUSY"));
    init();r[0]=0;now=100;tp_poll(&p);assert(p.state==TX_FAULT&&gated);assert(tp_wake(&p)<0);
    init();r[TP_SEQUENCE/4]=0;now=100;tp_poll(&p);assert(p.state==TX_FAULT);
    init();stop_error=1;assert(tp_prepare(&p,1)<0);
    init();assert(tp_prepare(&p,0)<0);assert(tp_sleep(&p,1)<0);
    init();tp_prepare(&p,1);tp_sleep(&p,1);ack();tp_poll(&p);idle=false;assert(tp_wake(&p)<0);
    idle=true;pause_error=1;assert(tp_wake(&p)<0&&p.state==TX_FAULT);
    init();tp_prepare(&p,1);tp_sleep(&p,1);ack();tp_poll(&p);tp_wake(&p);now=5000;tp_poll(&p);assert(p.state==TX_FAULT);
    init();tp_prepare(&p,1);tp_sleep(&p,1);ack();r[TP_STATUS/4]&=~TP_ASLEEP;tp_poll(&p);assert(p.state==TX_FAULT);
    init();p.state=(tx_power_state)99;tp_poll(&p);assert(p.state==TX_FAULT);assert(!strcmp(tp_name(p.state),"fault"));
    tx_power_ops o=p.io;r[0]=0;tp_init(&p,&o);assert(p.state==TX_FAULT);
    r[0]=TP_MAGIC;r[TP_CONTROL/4]=1;tp_init(&p,&o);assert(p.state==TX_FAULT);
    init();assert(!strcmp(tp_name((tx_power_state)99),"fault"));p.state=TX_SLEEPING;assert(!tp_allowed(&p));tp_poll(&p);
    init();r[TP_STATUS/4]=TP_CLOCK_READY|TP_HELD;assert(!tp_allowed(&p));
    init();tp_prepare(&p,1);r[0]=0;assert(tp_sleep(&p,1)<0&&p.state==TX_FAULT);
    init();tp_prepare(&p,1);r[0]=0;tp_poll(&p);assert(p.state==TX_FAULT);
    init();tp_prepare(&p,1);tp_sleep(&p,1);ack();tp_poll(&p);stop_error=1;assert(tp_wake(&p)<0);
    init();tp_prepare(&p,1);tp_sleep(&p,1);ack();r[TP_STATUS/4]&=~TP_CLOCK_READY;tp_poll(&p);assert(p.state==TX_QUIESCING);
    r[TP_STATUS/4]|=TP_CLOCK_READY;tp_poll(&p);assert(p.state==TX_SLEEPING);tp_wake(&p);ack();r[TP_STATUS/4]&=~TP_HELD;
    tp_poll(&p);assert(p.state==TX_FAULT&&!strcmp(p.error,"TX_WAKE_PROOF"));
    init();o=p.io;r[TP_ACK/4]=UINT32_MAX;r[TP_SEQUENCE/4]=UINT32_MAX;tp_init(&p,&o);assert(p.sequence==1);ack();tp_poll(&p);assert(p.state==TX_READY);
    init();tp_prepare(&p,1);now=89999;pause_delay=2;assert(tp_sleep(&p,1)<0&&issues==1&&p.state==TX_FAULT);
    init();tp_prepare(&p,1);tp_sleep(&p,1);ack();tp_poll(&p);pause_delay=5001;assert(tp_wake(&p)<0&&issues==2&&p.state==TX_FAULT);
    puts("PASS tx_power_machine: drain, ownership, ACK, settle, reset, fault");return 0;
}
