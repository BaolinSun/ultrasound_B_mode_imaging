#include "temperature_cache.h"
#include "temperature_protocol.h"
#include <stdarg.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
#define LOCK() portENTER_CRITICAL(&lock)
#define UNLOCK() portEXIT_CRITICAL(&lock)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif
/* UART task exclusively owns parser; HTTP takes only a short snapshot lock.
 * Fixed storage, no PSRAM, allocation or USB connection dependency. */
static char line[TEMP_LINE_BYTES];
static size_t used;
static bool dropping;
static temperature_message_t message;
static uint64_t received_ms;
static bool seen,forced_stale;
static temperature_record_t *history;
static unsigned history_start,history_count;
static uint32_t generation,source_gaps,parse_errors,uart_resets;
bool temperature_cache_init(void) {
    /* Called once before the UART task starts; no new internal-SRAM array. */
    if(history) return true;
#ifdef ESP_PLATFORM
    history=heap_caps_calloc(TEMP_HISTORY_POINTS,sizeof(*history),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    generation=esp_random();
#else
    static temperature_record_t host_history[TEMP_HISTORY_POINTS];
    history=host_history;generation=1;
#endif
    return history!=NULL;
}
static void clear_history(void) {
    history_start=history_count=0;
    if(!++generation) ++generation;
}
static void accept_line(uint64_t now) {
    if(used>=10 && !memcmp(line,"@UART_LOG ",10)) {
        LOCK();seen=false;forced_stale=false;received_ms=0;clear_history();UNLOCK();return;
    }
    temperature_message_t candidate;
    if(!temperature_parse(line,used,&candidate)) {
        if(used>=7 && !memcmp(line,"@TEMP1 ",7)) {LOCK();parse_errors++;UNLOCK();}
        return;
    }
    LOCK();
    /* Duplicate UART reports must not keep a disconnected source fresh. */
    if(!seen || candidate.seq!=message.seq) {
        if(seen) {
            uint32_t step=candidate.seq-message.seq;
            if(step>=0x80000000U || now<received_ms) clear_history();
            else if(step>1) source_gaps+=step-1;
        }
        message=candidate;received_ms=now;seen=true;forced_stale=false;
        if(history) {
            if(history_count==TEMP_HISTORY_POINTS) {
                history_start=(history_start+1)%TEMP_HISTORY_POINTS;history_count--;
            }
            temperature_record_t *r=&history[(history_start+history_count++)%TEMP_HISTORY_POINTS];
            r->received_ms=now;r->seq=candidate.seq;r->mask=candidate.mask;
            memcpy(r->mc,candidate.mc,sizeof(r->mc));
        }
    }
    UNLOCK();
}
void temperature_cache_feed(const uint8_t *p,size_t n,uint64_t now) {
    if(!p) return;
    while(n--) {
        uint8_t c=*p++;
        if(c=='\r' || c=='\n') {
            if(!dropping && used) accept_line(now);
            used=0;dropping=false;continue;
        }
        if(dropping) continue;
        if(used>=sizeof(line)-1) { used=0;dropping=true;continue; }
        line[used++]=(char)c;
    }
}
void temperature_cache_uart_lost(void) {
    used=0;dropping=false;
    LOCK();seen=false;forced_stale=true;received_ms=0;uart_resets++;clear_history();UNLOCK();
}
void temperature_cache_get(temperature_status_t *s,uint64_t now) {
    if(!s) return;
    LOCK();
    s->has_sample=seen;s->seq=message.seq;s->mask=message.mask;
    s->history_available=history!=NULL;
    memcpy(s->mc,message.mc,sizeof(s->mc));
    s->age_ms=seen?(now>=received_ms?now-received_ms:UINT64_MAX):0;
    s->stale=forced_stale || (seen && s->age_ms>TEMP_STALE_MS);
    UNLOCK();
}
const char *temperature_channel_status(const temperature_status_t *s,unsigned i) {
    if(!s || i>=3) return "waiting";
    if(s->stale) return "stale";
    if(!s->has_sample) return "waiting";
    return s->mask&(1U<<i)?"ok":"read_error";
}
void temperature_cache_history_get(temperature_history_t *out,uint64_t now) {
    if(!out) return;
    LOCK();
    out->now_ms=now;out->generation=generation;out->available=history!=NULL;
    out->source_gaps=source_gaps;out->parse_errors=parse_errors;out->uart_resets=uart_resets;
    out->count=0;
    for(unsigned i=0;i<history_count;i++) {
        const temperature_record_t *r=&history[(history_start+i)%TEMP_HISTORY_POINTS];
        if(now>=r->received_ms && now-r->received_ms<=TEMP_HISTORY_WINDOW_MS)
            out->records[out->count++]=*r;
    }
    UNLOCK();
}
static bool append_json(char *out,size_t cap,size_t *used_bytes,const char *format,...) {
    if(*used_bytes>=cap) return false;
    va_list args;va_start(args,format);
    int n=vsnprintf(out+*used_bytes,cap-*used_bytes,format,args);va_end(args);
    if(n<0 || (size_t)n>=cap-*used_bytes) return false;
    *used_bytes+=(size_t)n;return true;
}
size_t temperature_history_json(const temperature_history_t *h,char *out,size_t cap) {
    if(!h || !out || !h->available || h->count>TEMP_HISTORY_POINTS) return 0;
    size_t n=0;
    if(!append_json(out,cap,&n,"{\"version\":1,\"generation\":%" PRIu32 ",\"period_ms\":10000,\"source_gaps\":%" PRIu32 ",\"parse_errors\":%" PRIu32 ",\"uart_resets\":%" PRIu32 ",\"samples\":[",
        h->generation,h->source_gaps,h->parse_errors,h->uart_resets)) return 0;
    for(unsigned i=0;i<h->count;i++) {
        const temperature_record_t *r=&h->records[i];
        if(h->now_ms<r->received_ms) return 0;
        if(!append_json(out,cap,&n,"%s[%" PRIu32 ",%" PRIu64 ",%u,%" PRId32 ",%" PRId32 ",%" PRId32 "]",
            i?",":"",r->seq,h->now_ms-r->received_ms,(unsigned)r->mask,r->mc[0],r->mc[1],r->mc[2])) return 0;
    }
    return append_json(out,cap,&n,"]}")?n:0;
}
