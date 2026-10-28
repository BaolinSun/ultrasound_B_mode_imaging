#include "temperature_protocol.h"
#include "temperature_cache.h"
#include "temp.h"
#include "temp_hw.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static uint32_t now;
static unsigned reads[3],reports;
static int failures;
static char sent[TEMP_LINE_BYTES];
uint32_t temp_hw_now_ms(void) { return now; }
int temp_hw_init_xadc(void) { return failures&1?-1:0; }
int temp_hw_xadc_raw(uint16_t *r) { reads[0]++;*r=0x9770;return failures&1?-1:0; }
int temp_hw_sensor_raw(uint32_t a,uint16_t *r) {
    temperature_service_poll(); /* Re-entry must not start a second report. */
    assert(a==0x48 || a==0x49);unsigned i=a==0x48?1:2;
    reads[i]++;*r=i==1?0:0xf5e0;return failures&(1<<i)?-1:0;
}
int temp_hw_send(const char *p,size_t n) {
    assert(n<sizeof(sent));memcpy(sent,p,n);sent[n]=0;reports++;
    temperature_cache_feed((const uint8_t*)p,n,now);
    return failures&8?-1:0;
}
static void feed_message(uint32_t seq,uint8_t mask,uint64_t time) {
    temperature_message_t m={.seq=seq,.mask=mask,.mc={25000,0,-625}};
    char p[TEMP_LINE_BYTES];size_t n=temperature_format(p,sizeof(p),&m);assert(n);
    for(size_t i=0;i<n;i++)temperature_cache_feed((const uint8_t*)p+i,1,time);
}
static void protocol_tests(void) {
    assert(temperature_crc((const uint8_t*)"123456789",9)==0xcbf43926U);
    assert(temperature_sensor_mc(0x1900)==25000);
    assert(temperature_sensor_mc(0x0000)==0);
    assert(temperature_sensor_mc(0xfff0)==-63);
    assert(temperature_sensor_mc(0x001f)==63);
    assert(temperature_sensor_mc(0xf5e0)==-10125);
    assert(temperature_sensor_mc(0x8000)==-128000);
    assert(temperature_sensor_mc(0x7ff0)==127938);
    assert(temperature_xadc_mc(0x9770)==24978);
    assert(temperature_xadc_mc(0x977f)==24978);
    assert(temperature_due(20,UINT32_MAX-10));
    assert(!temperature_due(UINT32_MAX-10,20));
    temperature_message_t m={UINT32_MAX,7,{INT32_MIN,0,INT32_MAX}},result;
    char p[TEMP_LINE_BYTES];size_t n=temperature_format(p,sizeof(p),&m);assert(n);
    assert(temperature_parse(p,n-2,&result) && result.mc[0]==INT32_MIN && result.seq==UINT32_MAX);
    assert(!temperature_parse(p,n,&result));
    assert(!temperature_parse(NULL,n,&result));
    assert(!temperature_parse(p,n-2,NULL));
    assert(!temperature_format(p,2,&m));
    assert(!temperature_format(NULL,sizeof(p),&m));
    assert(!temperature_format(p,sizeof(p),NULL));
    assert(!temperature_parse(p,2,&result));
    assert(!temperature_parse(p,TEMP_LINE_BYTES,&result));
    char bad[TEMP_LINE_BYTES];memcpy(bad,p,n);
    bad[n-3]='z';assert(!temperature_parse(bad,n-2,&result));
    bad[n-3]='G';assert(!temperature_parse(bad,n-2,&result));
    bad[n-3]='/';assert(!temperature_parse(bad,n-2,&result));
    memcpy(bad,p,n);
    for(size_t i=0;i<n;i++)if(bad[i]>='a'&&bad[i]<='f')bad[i]=(char)(bad[i]-'a'+'A');
    assert(temperature_parse(bad,n-2,&result));
    memset(bad,'x',30);assert(!temperature_parse(bad,30,&result));
    memcpy(bad,"@TEMP1 ",7);assert(!temperature_parse(bad,30,&result));
    p[10]^=1;assert(!temperature_parse(p,n-2,&result));
    const char *invalid[]={
        "@TEMP1 4294967296 7 0 0 0","@TEMP1 -1 7 0 0 0",
        "@TEMP1 0 8 0 0 0","@TEMP1 0 7 2147483648 0 0",
        "@TEMP1 0 7 -2147483649 0 0","@TEMP1 0 7 0  0 0",
        "@TEMP1 0 7 0 0 0suffix","@TEMP1 0 7 0 0 +1",
        "@TEMP1 0 7 0 0 -","@TEMP1 0 7 1.0 0 0",
        "@TEMP1 1x 7 0 0 0","@TEMP1 0 7x 0 0 0","@TEMP1 0 7 z 0 0"
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        int len=snprintf(p,sizeof(p),"%s",invalid[i]);
        uint32_t c=temperature_crc((const uint8_t*)p,(size_t)len);
        int tail=snprintf(p+len,sizeof(p)-(size_t)len,"*%08" PRIx32,c);
        assert(tail==9 && !temperature_parse(p,(size_t)len+9,&result));
    }
    m.mask=8;assert(!temperature_format(p,sizeof(p),&m));
}
static void cache_tests(void) {
    temperature_status_t s;temperature_cache_get(&s,0);
    assert(!strcmp(temperature_channel_status(&s,0),"waiting"));
    temperature_cache_feed((const uint8_t*)"normal\r\n中文\n",strlen("normal\r\n中文\n"),1);
    feed_message(0,7,100);
    temperature_cache_get(&s,100);assert(s.has_sample && !s.stale && s.mc[1]==0);
    assert(!strcmp(temperature_channel_status(&s,2),"ok"));
    feed_message(0,7,29999); // Duplicate cannot refresh age.
    temperature_cache_get(&s,30100);assert(!s.stale);
    temperature_cache_get(&s,30101);assert(s.stale);
    feed_message(1,5,31000);temperature_cache_get(&s,31000);
    assert(!s.stale && !strcmp(temperature_channel_status(&s,1),"read_error"));
    assert(!strcmp(temperature_channel_status(&s,0),"ok"));
    const char *boot="@UART_LOG UART0 boot\n";
    temperature_cache_feed((const uint8_t*)boot,strlen(boot),32000);
    temperature_cache_get(&s,32000);assert(!s.has_sample && !s.stale);
    feed_message(0,0,33000);temperature_cache_get(&s,33000);
    assert(!strcmp(temperature_channel_status(&s,0),"read_error"));
    temperature_cache_uart_lost();temperature_cache_get(&s,34000);
    assert(!s.has_sample && !strcmp(temperature_channel_status(&s,0),"stale"));
    char long_line[180];memset(long_line,'a',sizeof(long_line));
    temperature_cache_feed((const uint8_t*)long_line,sizeof(long_line),35000);
    feed_message(1,7,35000);temperature_cache_get(&s,35000);assert(!s.has_sample);
    feed_message(2,7,35001);temperature_cache_get(&s,35001);assert(s.has_sample && s.seq==2);
    // A full valid body without newline is not a new sample.
    temperature_message_t m={3,7,{100,200,300}};char p[TEMP_LINE_BYTES];
    size_t n=temperature_format(p,sizeof(p),&m);
    temperature_cache_feed((uint8_t*)p,n-2,36000);temperature_cache_get(&s,36000);assert(s.seq==2);
    temperature_cache_feed((const uint8_t*)"\n",1,36001);temperature_cache_get(&s,36001);assert(s.seq==3);
    p[8]^=1;temperature_cache_feed((uint8_t*)p,n,37000);temperature_cache_get(&s,37000);assert(s.seq==3);
    temperature_cache_feed(NULL,9,37000);
    assert(!strcmp(temperature_channel_status(NULL,0),"waiting"));
    assert(!strcmp(temperature_channel_status(&s,3),"waiting"));
}
static void service_tests(void) {
    int32_t local_mc;
    assert(temperature_service_zynq_mc(&local_mc)!=0);
    assert(temperature_service_zynq_mc(NULL)!=0);
    assert(temperature_service_remaining_ms()==0);
    unsigned before=reports;temperature_service_poll();assert(reports==before);
    now=100;temperature_service_init();temperature_service_poll();
    assert(temperature_service_remaining_ms()==10000);
    assert(reports==before+1 && reads[0]==1 && reads[1]==1 && reads[2]==1);
    temperature_message_t m;assert(temperature_parse(sent,strlen(sent)-2,&m));
    assert(m.mask==7 && m.seq==0 && m.mc[1]==0 && m.mc[2]==-10125);
    assert(!temperature_service_zynq_mc(&local_mc) && local_mc==m.mc[0]);
    now=10101;assert(temperature_service_zynq_mc(&local_mc)!=0);
    now=10099;temperature_service_poll();assert(reports==before+1);
    assert(temperature_service_remaining_ms()==1);
    failures=2;now=10100;temperature_service_poll();
    assert(temperature_parse(sent,strlen(sent)-2,&m) && m.mask==5 && m.seq==1);
    assert(isnan(read_temp(0x48)));
    assert(read_temp(0x49)<0);
    assert(temp_read_checked(0x48,NULL)!=0);
    failures=1|4|8;now=100000;temperature_service_poll();
    assert(temperature_parse(sent,strlen(sent)-2,&m) && m.mask==2 && m.seq==2);
    assert(temperature_service_zynq_mc(&local_mc)!=0);
    temperature_service_poll();assert(reports==before+3); // No catch-up on send failure.
    failures=0;now=UINT32_MAX-5000;temperature_service_init();temperature_service_poll();
    assert(temperature_service_remaining_ms()==10000);
    assert(!temperature_service_zynq_mc(&local_mc));
    before=reports;now=4998;temperature_service_poll();assert(reports==before);
    now=4999;temperature_service_poll();assert(reports==before+1);
}
int main(void) {
    protocol_tests();cache_tests();service_tests();
    puts("PASS: temperature conversion, strict CRC protocol, cache freshness, independent failures, period/wrap and no catch-up");
    return 0;
}
