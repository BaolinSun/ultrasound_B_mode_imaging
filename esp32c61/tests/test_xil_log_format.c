#include "xil_printf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char output[1024];
static unsigned used;
void outbyte(char c) { assert(used+1<sizeof(output));output[used++]=c;output[used]=0; }
int main(void) {
    xil_printf("@SDIO DDR_TEST addr=%08x bytes=%u crc32=%08x compiled=SDIO\r\n",
               (unsigned)0x00151940,(unsigned)787456,(unsigned)0x3aa5aebf);
    puts(output);
    assert(!strcmp(output,"@SDIO DDR_TEST addr=00151940 bytes=787456 crc32=3AA5AEBF compiled=SDIO\r\n"));
    used=0;
    xil_printf("cmd=%08x norm=%04x err=%04x count=%u rc=%d",(unsigned)0x80000c08,
               (unsigned)0x06ff,(unsigned)0x33ff,(unsigned)0xffffffff,-4);
    puts(output);
    assert(!strcmp(output,"cmd=80000C08 norm=06FF err=33FF count=4294967295 rc=-4"));
    puts("PASS: actual SDK xil_printf prints 32-bit DDR/CMD/status/counters without l-modifier");
}
