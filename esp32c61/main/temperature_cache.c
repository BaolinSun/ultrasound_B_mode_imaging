#include "temperature_cache.h"
#include "temperature_protocol.h"
#ifdef ESP_PLATFORM
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
static void accept_line(uint64_t now) {
    if(used>=10 && !memcmp(line,"@UART_LOG ",10)) {
        LOCK();seen=false;forced_stale=false;received_ms=0;UNLOCK();return;
    }
    temperature_message_t candidate;
    if(!temperature_parse(line,used,&candidate)) return;
    LOCK();
    /* Duplicate UART reports must not keep a disconnected source fresh. */
    if(!seen || candidate.seq!=message.seq) {
        message=candidate;received_ms=now;seen=true;forced_stale=false;
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
    LOCK();seen=false;forced_stale=true;received_ms=0;UNLOCK();
}
void temperature_cache_get(temperature_status_t *s,uint64_t now) {
    if(!s) return;
    LOCK();
    s->has_sample=seen;s->seq=message.seq;s->mask=message.mask;
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
