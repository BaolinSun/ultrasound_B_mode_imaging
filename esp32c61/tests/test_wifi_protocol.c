#include "wifi_control_protocol.h"
#include <assert.h>
int main(int argc,char **argv) {
    if(argc==2) return wc_validate(argv[1])?1:0;
    assert(wc_crc("123456789",9)==0xcbf43926U);
    char line[WC_LINE];wc_message m;
    for(unsigned i=1;i<160;i++) {
        char text[WC_COMMAND];memset(text,'A',i);text[i]=0;
        int n=wc_encode(line,sizeof(line),false,0xfedcba98U,1,text);assert(n>0);
        assert(wc_decode(line,false,&m)&&m.session==0xfedcba98U&&!strcmp(m.text,text));
        assert(!wc_decode(line,true,&m));line[10]^=1;assert(!wc_decode(line,false,&m));
    }
    assert(wc_encode(line,sizeof(line),true,1,1,"@ACK 0 GAIN_SET 8")>0);
    assert(wc_decode(line,true,&m));
    char oversized[129];memset(oversized,'A',128);oversized[128]=0;
    assert(wc_encode(line,sizeof(line),true,1,1,oversized)<0);
    int prefix=snprintf(line,sizeof(line),"@WFC1 00000001 00000001 0x6 STATUS");
    uint32_t sum=wc_crc(line,(size_t)prefix);
    snprintf(line+prefix,sizeof(line)-(size_t)prefix,"*%08lx\n",(unsigned long)sum);
    assert(!wc_decode(line,false,&m)); /* Length must be decimal even with valid CRC. */
    const char *bad[]={"", "HV SET 1", "HV ENABLE extra", "GAIN SET 9", "GAIN SET -1", "GAIN SET 4294967296",
        "CFG END 1", "BRAM WR t_timing 0 1 2048", "BRAM WR x_element 63 2 0 0", "BRAM WR x_element 0 1 0 extra",
        "AFE WR ADC 256 0", "AFE WR ADC 1 65536", "TX7332 RD 1024", " HV ENABLE", "HV  ENABLE", "HV\nENABLE"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(wc_validate(bad[i]));
    const char *good[]={"STATUS","HV ENABLE","HV DISABLE","BEAM START","BEAM STOP","CFG BEGIN 0","CFG END","CFG ABORT",
        "AFE RD ADC 255","AFE WR DTGC 1 65535","AFE SLEEP","AFE WAKE","AFE STATUS","TX7332 RD 1023","GAIN SET 8","BRAM WR t_timing 0 1 6144"};
    for(unsigned i=0;i<sizeof(good)/sizeof(good[0]);i++) assert(!wc_validate(good[i]));
    puts("PASS protocol: boundary lengths/CRC/strict grammar/ranges/6144 layout");return 0;
}
