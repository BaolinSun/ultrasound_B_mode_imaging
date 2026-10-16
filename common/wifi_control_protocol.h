#ifndef WIFI_CONTROL_PROTOCOL_H
#define WIFI_CONTROL_PROTOCOL_H
/* Shared PS/ESP wire contract. CRC is IEEE/zlib, not authentication. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define WC_LINE 256U
#define WC_COMMAND 160U
#define WC_RESPONSE 128U
#define WC_BATCH 8U
typedef struct { uint32_t session, request; char text[WC_COMMAND]; } wc_message;
static inline uint32_t wc_crc(const char *p, size_t n) {
    uint32_t c=~0U;
    while(n--) { c^=(uint8_t)*p++; for(unsigned j=0;j<8;j++) c=(c>>1)^((0U-(c&1U))&0xedb88320U); }
    return ~c;
}
static inline bool wc_uint(const char *s, uint32_t *v) {
    if(!s || !*s) return false;
    unsigned base=10; uint32_t n=0;
    if(s[0]=='0' && (s[1]=='x'||s[1]=='X')) { base=16; s+=2; if(!*s) return false; }
    for(;*s;s++) {
        unsigned d=*s>='0'&&*s<='9'?(unsigned)(*s-'0'):
            *s>='a'&&*s<='f'?(unsigned)(*s-'a'+10):*s>='A'&&*s<='F'?(unsigned)(*s-'A'+10):99;
        if(d>=base || n>(UINT32_MAX-d)/base) return false;
        n=n*base+d;
    }
    *v=n; return true;
}
static inline bool wc_hex8(const char *s,uint32_t *v) {
    if(strlen(s)!=8) return false;
    char t[11]="0x"; memcpy(t+2,s,9); return wc_uint(t,v);
}
static inline int wc_encode(char *out,size_t cap,bool response,uint32_t session,uint32_t request,const char *text) {
    size_t len=strlen(text);
    if(!session || !request || !len || len>=WC_COMMAND || (response && len>=WC_RESPONSE)) return -1;
    for(size_t i=0;i<len;i++) if(text[i]<32 || text[i]>126 || text[i]=='*') return -1;
    int n=snprintf(out,cap,"@WF%c1 %08lx %08lx %u %s",response?'R':'C',
        (unsigned long)session,(unsigned long)request,(unsigned)len,text);
    if(n<0 || (size_t)n+11>cap) return -1;
    uint32_t crc=wc_crc(out,(size_t)n);
    int tail=snprintf(out+n,cap-(size_t)n,"*%08lx\n",(unsigned long)crc);
    return tail==10?n+tail:-1;
}
static inline bool wc_decode(const char *line,bool response,wc_message *m) {
    size_t len=strlen(line);
    if(len && line[len-1]=='\n') len--;
    if(len && line[len-1]=='\r') len--;
    if(len<36 || len>=WC_LINE || strncmp(line,response?"@WFR1 ":"@WFC1 ",6)) return false;
    char copy[WC_LINE]; memcpy(copy,line,len); copy[len]=0;
    char *star=strrchr(copy,'*'); uint32_t crc;
    if(!star || !wc_hex8(star+1,&crc) || crc!=wc_crc(copy,(size_t)(star-copy))) return false;
    *star=0;
    if(copy[14]!=' ' || copy[23]!=' ') return false;
    copy[14]=copy[23]=0;
    if(!wc_hex8(copy+6,&m->session)||!wc_hex8(copy+15,&m->request)||!m->session||!m->request) return false;
    char *end=strchr(copy+24,' '); uint32_t bytes;
    if(!end) return false;
    *end=0;
    for(char *q=copy+24;*q;q++) if(*q<'0'||*q>'9') return false;
    if(!wc_uint(copy+24,&bytes)||!bytes||bytes>=WC_COMMAND||(response && bytes>=WC_RESPONSE)||strlen(end+1)!=bytes) return false;
    for(uint32_t i=0;i<bytes;i++) if(end[1+i]<32||end[1+i]>126||end[1+i]=='*') return false;
    memcpy(m->text,end+1,bytes+1); return true;
}
/* Strict Wi-Fi-only grammar; legacy USB/UART keep their original parser. */
static inline const char *wc_validate(const char *line) {
    if(!line || !*line || strlen(line)>=WC_COMMAND) return "LINE_LENGTH";
    if(line[0]==' ' || line[strlen(line)-1]==' ' || strstr(line,"  ")) return "CANONICAL_SPACES_REQUIRED";
    char copy[WC_COMMAND]; strcpy(copy,line);
    char *t[14]; unsigned n=0; char *p=copy;
    while(*p) {
        if(*p==' ') { p++; continue; }
        if(n==14) return "ARGUMENTS";
        t[n++]=p;
        while(*p && *p!=' ') { if(*p<33||*p>126||*p=='*') return "CHARACTER"; p++; }
        if(*p) *p++=0;
    }
    if(!n) return "EMPTY";
    if(!strcmp(t[0],"STATUS")) return n==1?NULL:"ARGUMENTS";
    if(n<2) return "ARGUMENTS";
    uint32_t a=0,b=0;
    if(!strcmp(t[0],"CFG")) {
        if(!strcmp(t[1],"BEGIN")) return n==3&&wc_uint(t[2],&a)?NULL:"CFG_SEQ";
        return n==2&&(!strcmp(t[1],"END")||!strcmp(t[1],"ABORT"))?NULL:"CFG_OP";
    }
    if(!strcmp(t[0],"BEAM")) return n==2&&(!strcmp(t[1],"START")||!strcmp(t[1],"STOP"))?NULL:"BEAM_OP";
    if(!strcmp(t[0],"HV")) return n==2&&(!strcmp(t[1],"ENABLE")||!strcmp(t[1],"DISABLE"))?NULL:"HV_OP";
    if(!strcmp(t[0],"GAIN")) return n==3&&!strcmp(t[1],"SET")&&wc_uint(t[2],&a)&&a<=8?NULL:"GAIN_RANGE";
    if(!strcmp(t[0],"AFE")) {
        if(n<4 || (strcmp(t[2],"GLOBAL")&&strcmp(t[2],"ADC")&&strcmp(t[2],"VCA")&&strcmp(t[2],"DTGC")) ||
           !wc_uint(t[3],&a)||a>255) return "AFE_ARGS";
        if(!strcmp(t[1],"RD")) return n==4?NULL:"ARGUMENTS";
        return n==5&&!strcmp(t[1],"WR")&&wc_uint(t[4],&a)&&a<=65535?NULL:"AFE_ARGS";
    }
    if(!strcmp(t[0],"TX7332")&&!strcmp(t[1],"RD")) return n==3&&wc_uint(t[2],&a)&&a<=1023?NULL:"TX7332_RD_ARGS";
    if(strcmp(t[0],"BRAM")&&strcmp(t[0],"TX7332")) return "UNKNOWN_CMD";
    if(n<6||strcmp(t[1],"WR")||!wc_uint(t[3],&a)||!wc_uint(t[4],&b)||!b||b>8||n!=5+b) return "WRITE_ARGS";
    unsigned depth=0;
    const char *names[]={"fdemod_sin","fdemod_cos","log_table","x_element","hamming","dfilter","t_timing","sin_beta","delay_profile"};
    const unsigned depths[]={8192,8192,1024,64,64,64,10,64,16384};
    if(!strcmp(t[0],"TX7332")) { if(!strcmp(t[2],"tx7332_config_data")) depth=128; }
    else for(unsigned i=0;i<9;i++) if(!strcmp(t[2],names[i])) depth=depths[i];
    if(!depth || a>=depth || b>depth-a) return "WRITE_RANGE";
    for(unsigned i=0;i<b;i++) {
        uint32_t value;
        if(!wc_uint(t[5+i],&value)) return "WRITE_DATA";
        if(!strcmp(t[0],"BRAM")&&!strcmp(t[2],"t_timing")&&a+i==0&&value!=6144) return "FRAME_LAYOUT_6144_REQUIRED";
    }
    return NULL;
}
#endif
