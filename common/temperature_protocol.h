#ifndef ULTRASOUND_TEMPERATURE_PROTOCOL_H
#define ULTRASOUND_TEMPERATURE_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#define TEMP_PERIOD_MS 10000U
#define TEMP_STALE_MS 30000U
#define TEMP_LINE_BYTES 96U
typedef struct { uint32_t seq; uint8_t mask; int32_t mc[3]; } temperature_message_t;
static inline uint32_t temperature_crc(const uint8_t *p,size_t n) {
    uint32_t c=UINT32_MAX;
    while(n--) { c^=*p++; for(unsigned i=0;i<8;i++) c=(c>>1)^((c&1U)?0xedb88320U:0U); }
    return ~c;
}
/* Two's complement, 12 bits left aligned. Round half millidegrees away from 0. */
static inline int32_t temperature_sensor_mc(uint16_t raw) {
    int32_t n=raw>>4;
    if(n&0x800) n-=4096;
    return n>=0?(n*125+1)/2:-((-n*125+1)/2);
}
/* UG480: 12-bit code * 503.975 / 4096 - 273.15 Celsius. */
static inline int32_t temperature_xadc_mc(uint16_t raw) {
    return (int32_t)(((int64_t)(raw&0xfff0U)*503975+32768)/65536)-273150;
}
static inline bool temperature_due(uint32_t now,uint32_t next) {
    return (uint32_t)(now-next)<0x80000000U;
}
static inline size_t temperature_format(char *p,size_t cap,const temperature_message_t *m) {
    if(!p || !m || m->mask>7 || cap<TEMP_LINE_BYTES) return 0;
    int n=snprintf(p,cap,"@TEMP1 %" PRIu32 " %u %" PRId32 " %" PRId32 " %" PRId32,
        m->seq,(unsigned)m->mask,m->mc[0],m->mc[1],m->mc[2]);
    if(n<0 || (size_t)n+12>cap) return 0;
    uint32_t crc=temperature_crc((const uint8_t *)p,(size_t)n);
    int tail=snprintf(p+n,cap-(size_t)n,"*%08" PRIx32 "\r\n",crc);
    return tail==11?(size_t)n+11:0;
}
static inline bool temperature_number(const char **p,bool sign,uint32_t limit,int64_t *value) {
    bool negative=sign && **p=='-';
    if(negative) (*p)++;
    const char *start=*p; uint64_t n=0;
    while(**p>='0' && **p<='9') {
        n=n*10+(unsigned)(**p-'0'); if(n>limit) return false; (*p)++;
    }
    if(*p==start) return false;
    *value=negative?-(int64_t)n:(int64_t)n; return true;
}
/* Input excludes CR/LF. Exact spaces, integer bounds, no suffix/float payload. */
static inline bool temperature_parse(const char *p,size_t n,temperature_message_t *out) {
    if(!p || !out || n<20 || n>=TEMP_LINE_BYTES || memcmp(p,"@TEMP1 ",7)) return false;
    const char *star=memchr(p,'*',n);
    if(!star || (size_t)(p+n-star)!=9) return false;
    uint32_t crc=0;
    for(unsigned i=1;i<=8;i++) {
        unsigned char c=(unsigned char)star[i]; unsigned d;
        if(c>='0' && c<='9') d=c-'0';
        else if(c>='a' && c<='f') d=c-'a'+10;
        else if(c>='A' && c<='F') d=c-'A'+10; else return false;
        crc=(crc<<4)|d;
    }
    if(temperature_crc((const uint8_t *)p,(size_t)(star-p))!=crc) return false;
    char text[TEMP_LINE_BYTES]; memcpy(text,p,(size_t)(star-p)); text[star-p]=0;
    const char *q=text+7; int64_t v; temperature_message_t m={0};
    if(!temperature_number(&q,false,UINT32_MAX,&v) || *q++!=' ') return false;
    m.seq=(uint32_t)v;
    if(!temperature_number(&q,false,7,&v) || *q++!=' ') return false;
    m.mask=(uint8_t)v;
    for(unsigned i=0;i<3;i++) {
        if(!temperature_number(&q,true,2147483648U,&v) || v>INT32_MAX || v<INT32_MIN) return false;
        m.mc[i]=(int32_t)v;
        if(i<2 && *q++!=' ') return false;
    }
    if(*q) return false;
    *out=m; return true;
}
#endif
