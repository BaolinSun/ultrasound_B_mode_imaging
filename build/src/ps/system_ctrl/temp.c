#include "temp.h"
#include "temp_hw.h"
#include "temperature_protocol.h"
#include <math.h>
static uint32_t next_sample,sequence;
static int initialized,busy;
static int zynq_valid;
static int32_t zynq_mc;
static uint32_t zynq_sample_ms;
int temp_read_checked(uint32_t address,int32_t *value) {
    uint16_t raw;
    if(!value || temp_hw_sensor_raw(address,&raw)!=0) return -1;
    *value=temperature_sensor_mc(raw); return 0;
}
float read_temp(uint32_t address) {
    int32_t mc;
    return temp_read_checked(address,&mc)==0?(float)mc/1000.0f:NAN;
}
uint32_t temp_init(void) { return temp_hw_init_xadc()==0?0U:1U; }
void temperature_service_init(void) {
    initialized=1; busy=0; sequence=0; zynq_valid=0;
    (void)temp_init();
    next_sample=temp_hw_now_ms(); /* First group on first foreground service. */
    temperature_service_poll();
}
void temperature_service_poll(void) {
    uint32_t now=temp_hw_now_ms();
    if(!initialized || busy || !temperature_due(now,next_sample)) return;
    busy=1;
    next_sample=now+TEMP_PERIOD_MS; /* Never replay missed periods. */
    temperature_message_t m={.seq=sequence++,.mask=0,.mc={0,0,0}};
    uint16_t raw;
    if(temp_hw_xadc_raw(&raw)==0) { m.mc[0]=temperature_xadc_mc(raw);m.mask|=1; }
    /* Cache independently of UART/monitor; failed samples invalidate old data. */
    zynq_valid=(m.mask&1U)!=0;zynq_mc=m.mc[0];zynq_sample_ms=now;
    if(temp_read_checked(POWER_TEMP_ADDR,&m.mc[1])==0) m.mask|=2;
    if(temp_read_checked(AFE_TEMP_ADDR,&m.mc[2])==0) m.mask|=4;
    char line[TEMP_LINE_BYTES]; size_t length=temperature_format(line,sizeof(line),&m);
    if(length) (void)temp_hw_send(line,length); /* Bounded; no retry/replay. */
    busy=0;
}
uint32_t temperature_service_remaining_ms(void) {
    uint32_t now=temp_hw_now_ms();
    return !initialized || busy || temperature_due(now,next_sample)?0U:next_sample-now;
}
int temperature_service_zynq_mc(int32_t *mc) {
    if(!mc || !initialized || !zynq_valid ||
       (uint32_t)(temp_hw_now_ms()-zynq_sample_ms)>TEMP_PERIOD_MS) return -1;
    *mc=zynq_mc;return 0;
}
uint32_t temperature_service_sequence(void) { return sequence; }
