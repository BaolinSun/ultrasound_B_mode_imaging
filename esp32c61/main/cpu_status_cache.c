#include "cpu_status_cache.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
#define LOCK() portENTER_CRITICAL(&lock)
#define UNLOCK() portEXIT_CRITICAL(&lock)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif
/* One UART task owns framing; HTTP snapshots do not send commands to PS. */
static char line[CPU_STATUS_LINE_BYTES];
static size_t used;
static bool dropping,seen,forced_stale;
static uint64_t received_ms;
static cpu_status_message_t message;
static void accept(uint64_t now) {
    if(used>=10 && !memcmp(line,"@UART_LOG ",10)) {
        LOCK();seen=false;forced_stale=false;received_ms=0;UNLOCK();return;
    }
    cpu_status_message_t candidate;
    if(!cpu_status_parse(line,used,&candidate))return;
    LOCK();
    uint32_t delta=candidate.seq-message.seq;
    if(!seen || (delta && delta<0x80000000U)) {
        message=candidate;received_ms=now;seen=true;forced_stale=false;
    }
    UNLOCK();
}
void cpu_status_cache_feed(const uint8_t *p,size_t n,uint64_t now) {
    if(!p)return;
    while(n--) {
        uint8_t c=*p++;
        if(c=='\r'||c=='\n') {
            if(!dropping && used)accept(now);
            used=0;dropping=false;continue;
        }
        if(dropping)continue;
        if(used>=sizeof(line)-1){used=0;dropping=true;continue;}
        line[used++]=(char)c;
    }
}
void cpu_status_cache_uart_lost(void) {
    used=0;dropping=false;
    LOCK();seen=false;forced_stale=true;received_ms=0;UNLOCK();
}
void cpu_status_cache_get(cpu_status_snapshot_t *out,uint64_t now) {
    if(!out)return;
    LOCK();
    out->has_sample=seen;out->message=message;
    out->age_ms=seen?(now>=received_ms?now-received_ms:UINT64_MAX):0;
    out->stale=forced_stale || (seen && out->age_ms>CPU_STATUS_STALE_MS);
    UNLOCK();
}
