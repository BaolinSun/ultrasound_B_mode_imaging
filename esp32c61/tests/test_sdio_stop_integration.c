#define main legacy_bridge_test_main
#include "test_sdio_bridge.c"
#undef main
void host_bridge_init(void);
void host_bridge_poll(void);
unsigned host_has_lease(void);
static bool stop_in_dma;
static uint32_t image_length=BR_FRAME_BYTES;
uint8_t peer_read(unsigned p) {assert(p<64);return regs[p];}
void peer_write(unsigned p,uint8_t v) {assert(p<64);regs[p]=v;}
const uint8_t *peer_image(void) {return reference;}
uint32_t peer_length(void) {return image_length;}
uint64_t peer_time_ms(void) {return (uint64_t)clock_us/1000U;}
void peer_send(const uint8_t *p,unsigned count) {
    assert(count<=BR_WINDOW_BLOCKS && count);
    if(stop_in_dma){stop_in_dma=false;sdio_bridge_stop();}
    for(unsigned i=0;i<count;i++) {
        assert(desc[i].loaded && !desc[i].filled);
        memcpy(desc[i].data,p+i*512,512);desc[i].filled=true;
    }
}
static void cycle(void) {host_bridge_poll();step();clock_us+=1000;}
static void stop_complete(uint32_t session,unsigned previous_resets,unsigned errors) {
    for(unsigned i=0;i<1000 && (status().running || status().stopping || host_has_lease());i++)cycle();
    assert(!host_has_lease() && !status().running && !status().stopping && status().host_sdio);
    assert(status().session==session && status().errors==errors && resets==previous_resets);
    assert(status().free_slots==2);
}
int main(void) {
    raw_pattern_fill(reference,sizeof reference);auto_stop_ack=false;
    assert(sdio_bridge_init()==ESP_OK);host_bridge_init();
    for(unsigned i=0;i<600;i++)cycle();assert(status().host_sdio);
    unsigned reset_baseline=resets,error_baseline=status().errors;
    for(unsigned test=0;test<20;test++) {
        image_length=((6144U>>(test%5))+8U)*128U;
        assert(sdio_bridge_start(0)==ESP_OK);step();uint32_t session=status().session;
        if(test%3==0) {
            host_bridge_poll(); /* BEGIN published; not yet accepted by ESP. */
            assert(host_has_lease());sdio_bridge_stop();step();
        } else if(test%3==1) {
            stop_in_dma=true;
            for(unsigned n=0;n<3000 && stop_in_dma;n++)cycle();
            assert(!stop_in_dma); /* Stop races with a synchronous CMD53 call. */
        } else {
            for(unsigned n=0;n<3000 && !udp_busy;n++)cycle();assert(udp_busy);
            for(unsigned n=0;n<4;n++)cycle();
            sdio_bridge_stop();step();
            for(unsigned n=0;n<50;n++){cycle();assert(resets==reset_baseline && status().stopping);}
            assert(!host_has_lease()); /* Real PS ACK preceded cancellation completion. */
            finish(RAW_CANCELLED);
        }
        stop_complete(session,reset_baseline,error_baseline);
    }
    puts("PASS: actual PS + ESP bridges, five dynamic lengths, raced BEGIN/CMD53 and delayed TCP callbacks; repeated stops retain SDIO with zero reset/error growth");
    return 0;
}
