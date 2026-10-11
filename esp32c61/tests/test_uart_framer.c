#include "uart_log_framer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t captured[4096];
static size_t total,parts,last_part;
static void emit(const uint8_t *p,size_t n,void *ctx) {
    (void)ctx; assert(total+n<=sizeof(captured));
    memcpy(captured+total,p,n); total+=n;parts++;last_part=n;
}
static void reset(uart_log_framer_t *f) { uart_log_framer_reset(f);total=parts=last_part=0; }
int main(void) {
    uart_log_framer_t f;
    reset(&f);
    uart_log_framer_feed(&f,(const uint8_t *)"one\r",4,0,emit,NULL);
    uart_log_framer_feed(&f,(const uint8_t *)"\ntwo\n\r\n",7,1,emit,NULL);
    assert(total==6 && parts==3 && !memcmp(captured,"onetwo",6));
    reset(&f);
    static const uint8_t chinese[]={0xe4,0xb8,0xad,0xe6,0x96,0x87};
    uart_log_framer_feed(&f,chinese,1,0,emit,NULL);
    uart_log_framer_idle(&f,200,emit,NULL);assert(parts==0);
    uart_log_framer_feed(&f,chinese+1,5,201,emit,NULL);
    uart_log_framer_idle(&f,400,emit,NULL);assert(parts==0);
    uart_log_framer_idle(&f,401,emit,NULL);assert(parts==1 && total==6 && !memcmp(captured,chinese,6));
    reset(&f);
    uint8_t long_line[1600];memset(long_line,'x',sizeof(long_line));
    memcpy(long_line+511,chinese,6); /* Capacity splits in the middle of UTF-8. */
    for(size_t n=0;n<sizeof(long_line);n+=7) {
        size_t length=sizeof(long_line)-n;if(length>7)length=7;
        uart_log_framer_feed(&f,long_line+n,length,20,emit,NULL);
    }
    uart_log_framer_idle(&f,220,emit,NULL);
    assert(total==sizeof(long_line) && parts==4 && !memcmp(captured,long_line,total));
    reset(&f);
    uart_log_framer_feed(&f,(const uint8_t *)"lost",4,1,emit,NULL);
    uart_log_framer_reset(&f);
    uart_log_framer_feed(&f,(const uint8_t *)"next\n",5,2,emit,NULL);
    assert(total==4 && !memcmp(captured,"next",4));
    reset(&f);
    /* Four-byte and two-byte UTF-8 tails, tabs, and raw high bytes survive. */
    static const uint8_t other[]={0xf0,0x9f,0x98,0x80,0xc2,0xa3,'\t',0xff};
    for(size_t i=0;i<sizeof(other);i++) {
        uart_log_framer_feed(&f,other+i,1,i*201,emit,NULL);
        uart_log_framer_idle(&f,i*201+200,emit,NULL);
    }
    assert(total==sizeof(other) && !memcmp(captured,other,total));
    reset(&f);
    uart_log_framer_feed(&f,NULL,0,0,emit,NULL);
    uart_log_framer_idle(&f,300,emit,NULL);assert(parts==0);
    puts("PASS: UART framer CRLF/LF/empty/Chinese/UTF8 tail/long line/idle/loss boundary");
    return 0;
}
