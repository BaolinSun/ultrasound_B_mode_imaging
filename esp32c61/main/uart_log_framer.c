#include "uart_log_framer.h"
#include <string.h>

/* Keep an incomplete UTF-8 tail for the next read instead of inserting a
 * newline inside a multibyte character. Other bytes are not sanitized. */
static size_t complete_prefix(const uint8_t *p,size_t n)
{
    if(!n) return 0;
    size_t lead=n-1;
    while(lead && (p[lead]&0xc0U)==0x80U) lead--;
    uint8_t c=p[lead];
    size_t expected=c>=0xc2U && c<=0xdfU?2U:c>=0xe0U && c<=0xefU?3U:
                    c>=0xf0U && c<=0xf4U?4U:1U;
    return n-lead<expected?lead:n;
}
static void output(uart_log_framer_t *f,size_t n,uart_log_emit_t emit,void *ctx)
{
    emit(f->data,n,ctx);
    f->used-=n;
    memmove(f->data,f->data+n,f->used);
}
void uart_log_framer_reset(uart_log_framer_t *f) { memset(f,0,sizeof(*f)); }
void uart_log_framer_feed(uart_log_framer_t *f,const uint8_t *data,size_t length,
                          uint64_t now_ms,uart_log_emit_t emit,void *ctx)
{
    if(length) f->last_rx_ms=now_ms;
    for(size_t i=0;i<length;i++) {
        uint8_t c=data[i];
        if(c=='\r' || c=='\n') {
            if(c=='\r' || !f->previous_cr) output(f,f->used,emit,ctx);
            f->previous_cr=c=='\r';
            continue;
        }
        f->previous_cr=false;
        f->data[f->used++]=c;
        if(f->used==sizeof(f->data)) output(f,complete_prefix(f->data,f->used),emit,ctx);
    }
}
void uart_log_framer_idle(uart_log_framer_t *f,uint64_t now_ms,uart_log_emit_t emit,void *ctx)
{
    if(f->used && now_ms-f->last_rx_ms>=UART_LOG_IDLE_MS) {
        size_t n=complete_prefix(f->data,f->used);
        if(n) output(f,n,emit,ctx);
    }
}
