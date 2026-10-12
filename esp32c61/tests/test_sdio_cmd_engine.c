#include "sdio_cmd_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t norm,err,post_norm,post_err;
static uint32_t present,arg_reg,cmd_reg;
static uint64_t tick;
static unsigned busy_reads,reads,writes,issued,freeze;
static uint32_t r32(void *ctx,unsigned p) {
    (void)ctx;assert(p==0x24);reads++;
    if(busy_reads) { busy_reads--;return 1; }return present;
}
static uint16_t r16(void *ctx,unsigned p) { (void)ctx;assert(p==0x30 || p==0x32);return p==0x30?norm:err; }
static void w32(void *ctx,unsigned p,uint32_t v) {
    (void)ctx;assert(p==8 || p==12);writes++;
    if(p==8)arg_reg=v;
    else { cmd_reg=v;issued++;norm=post_norm;err=post_err; }
}
static void w16(void *ctx,unsigned p,uint16_t v) {
    (void)ctx;writes++;assert(p==6 || p==0x30 || p==0x32);
    if(p==0x30) norm&=(uint16_t)~v;
    if(p==0x32) err&=(uint16_t)~v;
    if(p==6)assert(v==0 || v==16);
}
static void w8(void *ctx,unsigned p,uint8_t v) { (void)ctx;writes++;assert(p==0x2e && v==14); }
static uint64_t time_us(void *ctx) { (void)ctx;if(!freeze)tick+=10;return tick; }
static const sdio_cmd_io_t io={NULL,r32,r16,w32,w16,w8,time_us};
static void reset(void) { norm=err=post_norm=post_err=0;present=arg_reg=cmd_reg=0;tick=0;busy_reads=reads=writes=issued=freeze=0; }
int main(void) {
    /* Real SDK defines the frequency as CPU/2 without parentheses. */
#define TEST_COUNTS_PER_SECOND 666666666U/2
    assert(sdio_ticks_us(333333333U,(uint64_t)(TEST_COUNTS_PER_SECOND))==1000000U);
    assert(sdio_ticks_us(666666665U,(uint64_t)(TEST_COUNTS_PER_SECOND))==1999999U);
    assert(sdio_ticks_us(666666666U,(uint64_t)(TEST_COUNTS_PER_SECOND))==2000000U);
    assert(sdio_ticks_us((uint64_t)333333333U*172800U,(uint64_t)(TEST_COUNTS_PER_SECOND))==172800000000ULL);
    reset();post_norm=1;
    assert(sdio_cmd_execute(&io,0x500,0x00ff8000,0,0,0,100)==0);
    assert(issued==1 && cmd_reg==0x05000000 && arg_reg==0x00ff8000 && norm==0);
    reset();busy_reads=2;post_norm=1;
    assert(sdio_cmd_execute(&io,0,0,0,0,0,100)==0 && reads==3);
    reset();present=1;
    assert(sdio_cmd_execute(&io,0,0,0,0,0,100)==SDIO_CMD_TIMEOUT && !writes);
    reset();present=2;
    assert(sdio_cmd_execute(&io,0x3520,0,16,3,1,100)==SDIO_CMD_TIMEOUT && !issued);
    reset();present=2;post_norm=1; /* Non-data commands do not require DAT idle. */
    assert(sdio_cmd_execute(&io,0x3400,0,0,0,0,100)==0);
    reset();norm=1; /* Stale CC must be cleared; idle is never success. */
    assert(sdio_cmd_execute(&io,0,0,0,0,0,100)==SDIO_CMD_TIMEOUT && issued==1);
    reset();post_norm=0x8001;
    assert(sdio_cmd_execute(&io,0,0,0,0,0,100)==SDIO_CMD_ERROR);
    reset();post_err=1;post_norm=1;
    assert(sdio_cmd_execute(&io,0,0,0,0,0,100)==SDIO_CMD_ERROR);
    reset();post_norm=0x10;
    assert(sdio_cmd_execute(&io,0x3520,5,16,3,1,100)==0 && norm==0x10 && cmd_reg==0x35200003);
    reset();post_norm=2;
    assert(sdio_cmd_execute(&io,0x3520,0,16,3,1,100)==0 && norm==2);
    reset();post_norm=0x11;
    assert(sdio_cmd_execute(&io,0x3520,0,16,3,1,100)==0 && norm==0x10);
    reset();post_norm=0x10;
    assert(sdio_cmd_execute(&io,0x3400,0,0,0,0,100)==SDIO_CMD_TIMEOUT);
    reset();freeze=1;present=1;
    assert(sdio_cmd_execute(&io,0,0,0,0,0,100)==SDIO_CMD_TIMEOUT && reads<=100002 && !issued);
    reset();freeze=1;
    assert(sdio_cmd_execute(&io,0,0,0,0,0,100)==SDIO_CMD_TIMEOUT && issued==1);
    puts("PASS: SDHCI strict CC/errors/busy/deadline/stale status/data events/frozen timer");
}
