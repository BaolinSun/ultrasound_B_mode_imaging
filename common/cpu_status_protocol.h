#ifndef UB_CPU_STATUS_PROTOCOL_H
#define UB_CPU_STATUS_PROTOCOL_H
#include "temperature_protocol.h" /* Same IEEE CRC32 and existing wake period. */
#define CPU_STATUS_LINE_BYTES 128U
#define CPU_STATUS_PERIOD_MS TEMP_PERIOD_MS
#define CPU_STATUS_STALE_MS TEMP_STALE_MS
#define CPU_STATUS_WFI_ENABLED 1U
enum { CPU_MODE_ACTIVE, CPU_MODE_IDLE, CPU_MODE_BLOCKED, CPU_MODE_DISABLED };
enum { CPU_REASON_NONE, CPU_REASON_POWER_ACTIVE, CPU_REASON_CONFIG,
       CPU_REASON_TRANSFER, CPU_REASON_PAUSE, CPU_REASON_HARDWARE,
       CPU_REASON_POWER_STATE, CPU_REASON_DISABLED, CPU_REASON_TIMER };
typedef struct {
    uint32_t seq,flags,mode,reason,entries;
    uint64_t wait_us;
    uint32_t timer_irqs,cpu_control;
} cpu_status_message_t;
static inline bool cpu_status_valid(const cpu_status_message_t *m) {
    if(!m || m->flags>CPU_STATUS_WFI_ENABLED || m->mode>CPU_MODE_DISABLED || m->reason>CPU_REASON_TIMER)return false;
    if((m->mode==CPU_MODE_DISABLED)!=(m->flags==0))return false;
    if(m->mode==CPU_MODE_IDLE)return m->reason==CPU_REASON_NONE;
    if(m->mode==CPU_MODE_ACTIVE)return m->reason==CPU_REASON_POWER_ACTIVE;
    if(m->mode==CPU_MODE_DISABLED)return m->reason==CPU_REASON_DISABLED || m->reason==CPU_REASON_TIMER;
    return m->reason>=CPU_REASON_CONFIG && m->reason<=CPU_REASON_POWER_STATE;
}
static inline size_t cpu_status_format(char *p,size_t cap,const cpu_status_message_t *m) {
    if(!p || cap<CPU_STATUS_LINE_BYTES || !cpu_status_valid(m))return 0;
    int n=snprintf(p,cap,"@CPUS1 %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32
        " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32,
        m->seq,m->flags,m->mode,m->reason,m->entries,(uint32_t)m->wait_us,
        (uint32_t)(m->wait_us>>32),m->timer_irqs,m->cpu_control);
    if(n!=87)return 0;
    uint32_t crc=temperature_crc((const uint8_t *)p,(size_t)n);
    return snprintf(p+n,cap-(size_t)n,"*%08" PRIx32 "\r\n",crc)==11?(size_t)n+11:0;
}
static inline bool cpu_status_hex8(const char *p,uint32_t *out) {
    uint32_t v=0;
    for(unsigned i=0;i<8;i++) {
        unsigned c=(unsigned char)p[i],n;
        if(c>='0'&&c<='9')n=c-'0';
        else if(c>='a'&&c<='f')n=c-'a'+10;
        else if(c>='A'&&c<='F')n=c-'A'+10;
        else return false;
        v=(v<<4)|n;
    }
    *out=v;return true;
}
/* Fixed-width ASCII hex, excluding CR/LF. No optional fields or suffixes. */
static inline bool cpu_status_parse(const char *p,size_t n,cpu_status_message_t *out) {
    if(!p || !out || n!=96 || memcmp(p,"@CPUS1 ",7) || p[87]!='*')return false;
    uint32_t crc,word[9];
    if(!cpu_status_hex8(p+88,&crc) || temperature_crc((const uint8_t *)p,87)!=crc)return false;
    for(unsigned i=0;i<9;i++) {
        if(!cpu_status_hex8(p+7+i*9,&word[i]) || (i<8 && p[15+i*9]!=' '))return false;
    }
    cpu_status_message_t m={word[0],word[1],word[2],word[3],word[4],
        ((uint64_t)word[6]<<32)|word[5],word[7],word[8]};
    if(!cpu_status_valid(&m))return false;
    *out=m;return true;
}
static inline const char *cpu_status_mode_name(uint32_t mode) {
    static const char *const names[]={"active","idle","blocked","disabled"};
    return mode<=CPU_MODE_DISABLED?names[mode]:"unknown";
}
static inline const char *cpu_status_reason_name(uint32_t reason) {
    static const char *const names[]={"none","power_active","config","transfer","pause_unconfirmed",
        "hardware_unconfirmed","power_state","disabled","timer_unavailable"};
    return reason<=CPU_REASON_TIMER?names[reason]:"unknown";
}
#endif
