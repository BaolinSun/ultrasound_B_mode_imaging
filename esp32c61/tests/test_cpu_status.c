#include "cpu_status_cache.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
static cpu_status_message_t sample(uint32_t seq,uint32_t control) {
    cpu_status_message_t m={seq,1,CPU_MODE_IDLE,CPU_REASON_NONE,23,77578541U,9,control};return m;
}
static void feed(cpu_status_message_t m,uint64_t now) {
    char p[CPU_STATUS_LINE_BYTES];size_t n=cpu_status_format(p,sizeof p,&m);assert(n==98);
    cpu_status_cache_feed((uint8_t *)p,n,now);
}
static void crc(char *p) {snprintf(p+88,CPU_STATUS_LINE_BYTES-88,"%08" PRIx32 "\r\n",temperature_crc((const uint8_t *)p,87));}
static void protocol(void) {
    char p[CPU_STATUS_LINE_BYTES];cpu_status_message_t m=sample(9,34),r;
    assert(cpu_status_format(p,sizeof p,&m)==98&&cpu_status_parse(p,96,&r));
    assert(r.seq==m.seq&&r.wait_us==m.wait_us&&r.cpu_control==34);
    assert(!cpu_status_format(NULL,sizeof p,&m)&&!cpu_status_format(p,97,&m));
    assert(!cpu_status_format(p,sizeof p,NULL));
    assert(!cpu_status_parse(NULL,96,&r)&&!cpu_status_parse(p,96,NULL)&&!cpu_status_parse(p,95,&r));
    for(unsigned flag=0;flag<3;flag++)for(unsigned mode=0;mode<5;mode++)for(unsigned reason=0;reason<10;reason++) {
        m.flags=flag;m.mode=mode;m.reason=reason;
        bool expected=flag<=1 && mode<=3 && reason<=8 && ((mode==3)==(flag==0)) &&
            (mode==0?reason==1:mode==1?reason==0:mode==2?(reason>=2&&reason<=6):(reason==7||reason==8));
        assert(cpu_status_valid(&m)==expected);
        size_t n=cpu_status_format(p,sizeof p,&m);assert((n!=0)==expected);
        if(n)assert(cpu_status_parse(p,n-2,&r));
    }
    assert(!cpu_status_valid(NULL));
    m=sample(UINT32_MAX,UINT32_MAX);m.entries=UINT32_MAX;m.timer_irqs=UINT32_MAX;m.wait_us=UINT64_MAX;
    assert(cpu_status_format(p,sizeof p,&m)==98&&cpu_status_parse(p,96,&r)&&r.wait_us==UINT64_MAX);
    for(unsigned i=7;i<87;i++)if(p[i]>='a'&&p[i]<='f')p[i]=(char)(p[i]-'a'+'A');
    crc(p);
    for(unsigned i=88;i<96;i++)if(p[i]>='a'&&p[i]<='f')p[i]=(char)(p[i]-'a'+'A');
    assert(cpu_status_parse(p,96,&r));
    char good[CPU_STATUS_LINE_BYTES];memcpy(good,p,sizeof good);
    p[88]='g';assert(!cpu_status_parse(p,96,&r));memcpy(p,good,sizeof p);
    p[89]=p[89]=='0'?'1':'0';assert(!cpu_status_parse(p,96,&r));memcpy(p,good,sizeof p);
    p[0]='x';assert(!cpu_status_parse(p,96,&r));memcpy(p,good,sizeof p);
    p[87]='x';assert(!cpu_status_parse(p,96,&r));memcpy(p,good,sizeof p);
    p[7]='g';crc(p);assert(!cpu_status_parse(p,96,&r));memcpy(p,good,sizeof p);
    p[15]='_';crc(p);assert(!cpu_status_parse(p,96,&r));memcpy(p,good,sizeof p);
    p[23]='2';crc(p);assert(!cpu_status_parse(p,96,&r)); /* Flags 2 with valid CRC. */
    assert(!strcmp(cpu_status_mode_name(99),"unknown")&&!strcmp(cpu_status_reason_name(99),"unknown"));
}
static cJSON *json(uint64_t now) {cJSON *j=cJSON_CreateObject();cpu_status_add_json(j,now);return j;}
static cJSON *get(cJSON *j,const char *key){cJSON *p=cJSON_GetObjectItemCaseSensitive(j,key);assert(p);return p;}
static void cache_and_json(void) {
    cpu_status_snapshot_t s;cpu_status_cache_get(NULL,0);cpu_status_cache_feed(NULL,8,0);
    cpu_status_cache_get(&s,0);assert(!s.has_sample&&!s.stale);
    cJSON *j=json(0),*c=get(j,"cpus");assert(!strcmp(get(c,"status")->valuestring,"waiting"));
    assert(cJSON_IsNull(get(get(c,"cpu1"),"reset")));cJSON_Delete(j);
    char p[CPU_STATUS_LINE_BYTES];cpu_status_message_t m=sample(9,34);assert(cpu_status_format(p,sizeof p,&m)==98);
    cpu_status_cache_feed((uint8_t *)"中文\nnormal\r\n",strlen("中文\nnormal\r\n"),1);
    cpu_status_cache_feed((uint8_t *)p,40,100);cpu_status_cache_get(&s,100);assert(!s.has_sample);
    cpu_status_cache_feed((uint8_t *)p+40,58,100);cpu_status_cache_get(&s,100);assert(s.has_sample&&s.message.seq==9);
    feed(m,29999);m.seq=8;feed(m,29999);cpu_status_cache_get(&s,30101);assert(s.stale&&s.age_ms==30001);
    j=json(30101);c=get(j,"cpus");assert(cJSON_IsNull(get(get(c,"cpu0"),"entries")));cJSON_Delete(j);
    m.seq=10;feed(m,31000);cpu_status_cache_get(&s,31000);assert(!s.stale);
    cpu_status_cache_get(&s,30999);assert(s.stale); /* Never show an age that wrapped negative. */
    const uint32_t controls[]={0,2,32,34};
    for(unsigned i=0;i<4;i++) {
        m=sample(11+i,controls[i]);feed(m,32000+i);
        j=json(32000+i);c=get(j,"cpus");cJSON *a=get(c,"cpu0"),*b=get(c,"cpu1");
        assert(!strcmp(get(c,"status")->valuestring,"ok"));assert(!strcmp(get(a,"mode")->valuestring,"idle"));
        assert(cJSON_IsTrue(get(a,"wfi_enabled"))&&get(a,"entries")->valuedouble==23);
        assert(fabs(get(a,"wait_ms")->valuedouble-77578.541)<0.001);
        assert(cJSON_IsTrue(get(b,"reset"))==((controls[i]&2U)!=0));
        assert(cJSON_IsTrue(get(b,"clock_stopped"))==((controls[i]&32U)!=0));
        assert(cJSON_IsTrue(get(b,"disabled"))==(controls[i]==34));cJSON_Delete(j);
    }
    m.seq=15;m.flags=0;m.mode=CPU_MODE_DISABLED;m.reason=CPU_REASON_DISABLED;feed(m,33000);
    j=json(33000);c=get(get(j,"cpus"),"cpu0");assert(cJSON_IsFalse(get(c,"wfi_enabled"))&&!strcmp(get(c,"mode")->valuestring,"disabled"));cJSON_Delete(j);
    const char *boot="@UART_LOG UART0 boot\n";
    cpu_status_cache_feed((uint8_t *)boot,strlen(boot),34000);cpu_status_cache_get(&s,34000);assert(!s.has_sample&&!s.stale);
    m=sample(0,34);feed(m,34000);cpu_status_cache_uart_lost();cpu_status_cache_get(&s,34001);assert(!s.has_sample&&s.stale);
    m.seq=UINT32_MAX;feed(m,35000);m.seq=0;feed(m,35001);cpu_status_cache_get(&s,35001);assert(s.message.seq==0&&!s.stale);
    char large[256];memset(large,'x',sizeof large);cpu_status_cache_feed((uint8_t *)large,sizeof large,36000);
    m.seq=1;feed(m,36000);cpu_status_cache_get(&s,36000);assert(s.message.seq==0);
    feed(m,36001);cpu_status_cache_get(&s,36001);assert(s.message.seq==1);
    m.seq=2;assert(cpu_status_format(p,sizeof p,&m)==98);p[40]^=1;
    cpu_status_cache_feed((uint8_t *)p,98,37000);cpu_status_cache_get(&s,37000);assert(s.message.seq==1);
    cpu_status_cache_uart_lost();j=json(38000);c=get(j,"cpus");assert(!strcmp(get(c,"status")->valuestring,"stale"));cJSON_Delete(j);
}
int main(void){protocol();cache_and_json();puts("PASS CPU protocol/cache/JSON: CRC, fragments, boot/loss/stale, duplicate/order/wrap, CPU modes and real reset/clock bits");return 0;}
