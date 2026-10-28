/* Reuse strict DMA ownership/async sender doubles, exercise the production task. */
#define main legacy_bridge_test_main
#include "test_sdio_bridge.c"
#undef main

static void assert_idle(uint32_t session,unsigned reset_baseline,unsigned error_baseline) {
    sdio_bridge_status_t s=status();
    assert(!s.running && !s.stopping && s.host_sdio && s.free_slots==2);
    assert(s.session==session && resets==reset_baseline && s.errors==error_baseline);
}
static void ack_stop(void) {
    assert(status().stopping && (regs[BR_STATUS]&BR_STOPPING) && regs[BR_STOP_REQ]);
    regs[BR_STOP_ACK]=regs[BR_STOP_REQ];step();
}
static void restart(void) {
    select_host();assert(sdio_bridge_start(0)==ESP_OK);step();
}
int main(void) {
    raw_pattern_fill(reference,sizeof reference);
    uint32_t checksum=br_crc32(0,reference,sizeof reference);
    assert(sdio_bridge_init()==ESP_OK);step();select_host();auto_stop_ack=false;
    uint32_t session=status().session;unsigned base=resets,errors=status().errors;
    for(unsigned i=0;i<100;i++){sdio_bridge_stop();step();}
    assert_idle(session,base,errors); /* Idle STOP is fully idempotent. */

    restart();session=status().session;sdio_bridge_stop();step();
    assert(status().stopping && sdio_bridge_start(0)!=ESP_OK);
    uint8_t token=regs[BR_STOP_REQ];assert(token);
    for(unsigned i=0;i<50;i++){sdio_bridge_stop();step();assert(regs[BR_STOP_REQ]==token && resets==base);}
    /* Late metadata committed before the host observes STOP is retired once. */
    regs[BR_CMD]=BR_BEGIN;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);
    regs[BR_STOP_ACK]=(uint8_t)(token-1);step();assert(status().stopping);
    ack_stop();assert(regs[BR_CMD_ACK]==regs[BR_CMD_SEQ] && regs[BR_RESULT]==BR_CANCELLED);
    assert_idle(session,base,errors);

    /* STOP with one SENDING lease plus partial SDIO data and delayed callback. */
    restart();session=status().session;begin(1,checksum);frame(true,false);assert(udp_busy);
    const uint8_t *leased=job_data;
    begin(2,checksum);
    for(unsigned i=0;i<2;i++){memcpy(desc[i].data,reference+i*512,512);desc[i].filled=true;}
    sdio_bridge_stop();step();assert(status().stopping && status().free_slots==0);
    for(unsigned i=0;i<80;i++){step();assert(resets==base&&!memcmp(leased,reference,BR_FRAME_BYTES));}
    ack_stop();assert(status().stopping && status().free_slots==1);
    for(unsigned i=0;i<80;i++){sdio_bridge_stop();step();assert(resets==base&&status().stopping);}
    finish(RAW_CANCELLED);assert_idle(session,base,errors);

    /* Callback before host confirmation cannot finish the stop early. */
    restart();session=status().session;begin(3,checksum);frame(true,false);
    sdio_bridge_stop();step();finish(RAW_CANCELLED);assert(status().stopping);
    ack_stop();assert_idle(session,base,errors);

    /* Stop arriving during an unlocked DMA event wait must reload the handle. */
    restart();session=status().session;wait_inject=1;stop_in_wait=true;
    begin(4,checksum);assert(!wait_inject && !stop_in_wait);step();
    assert(status().stopping && status().receive_offset==0);
    ack_stop();wait_delivered=0;assert_idle(session,base,errors);

    /* Final short window without its normal ACK can be cancelled via STOP_ACK. */
    restart();session=status().session;begin(5,checksum);frame(false,false);
    sdio_bridge_stop();step();ack_stop();finish(RAW_CANCELLED);
    assert_idle(session,base,errors);

    /* Non-cancel sender failure remains a real fault, never hidden by STOP. */
    restart();begin(6,checksum);frame(true,false);sdio_bridge_stop();step();
    finish(RAW_TIMEOUT);assert(!status().host_sdio && status().reset_count==1);
    assert(!strcmp(status().reset_reason,"sender_failure"));base=resets;errors=status().errors;

    /* A missing host ACK escalates once. No speculative release/replay. */
    restart();sdio_bridge_stop();step();clock_us+=BR_STOP_TIMEOUT_US;step();step();
    assert(resets==base+1 && status().reset_count==2 && !status().stopping);
    assert(!strcmp(status().reset_reason,"stop_host_timeout"));base=resets;errors=status().errors;
    for(unsigned i=0;i<100;i++)step();assert(resets==base);

    /* Callback timeout keeps SENDING ownership even while the driver is down. */
    restart();begin(7,checksum);frame(true,false);sdio_bridge_stop();step();ack_stop();
    clock_us+=BR_STOP_TIMEOUT_US;step();step();
    assert(status().stopping && !status().ready && status().free_slots==1 && resets==base+1);
    for(unsigned i=0;i<200;i++)step();assert(resets==base+1 && udp_busy);
    finish(RAW_CANCELLED);assert(status().ready && !status().stopping && status().reset_count==3);
    assert(!strcmp(status().reset_reason,"stop_callback_timeout"));base=resets;errors=status().errors;

    /* Genuine host ABORT also waits for the callback, rebuilding exactly once. */
    restart();begin(8,checksum);frame(true,false);
    regs[BR_CMD]=BR_ABORT;regs[BR_CMD_SEQ]=br_next_seq(regs[BR_CMD_ACK]);step();step();
    for(unsigned i=0;i<100;i++)step();assert(resets==base+1 && udp_busy);
    finish(RAW_CANCELLED);assert(status().ready && status().reset_count==4);
    assert(!strcmp(status().reset_reason,"host_abort"));base=resets;errors=status().errors;

    /* Receive/reload/register failures during STOP remain genuine faults. */
    restart();sdio_bridge_stop();fault="recv";step();fault="";step();
    assert(!strcmp(status().reset_reason,"stop_receive") && resets==base+1);base=resets;
    restart();begin(9,checksum);memcpy(desc[0].data,reference,512);desc[0].filled=true;
    sdio_bridge_stop();fault="load";step();fault="";step();
    assert(!strcmp(status().reset_reason,"stop_reload") && resets==base+1);base=resets;
    restart();sdio_bridge_stop();fault="write";step();fault="";step();
    assert(!strcmp(status().reset_reason,"register_write") && resets==base+1);base=resets;
    /* Raw selftest has no Zynq DMA lease and can stop without a host handshake. */
    assert(!status().host_sdio && sdio_bridge_selftest()==ESP_OK);
    session=status().session;sdio_bridge_stop();step();assert(status().stopping);
    finish(RAW_CANCELLED);assert(!status().stopping && status().session==session && resets==base);
    assert(!status().raw_busy && status().free_slots==2);errors=status().errors;

    /* Token wraps, repeated starts/stops preserve descriptors and exact link. */
    for(unsigned i=0;i<260;i++) {
        restart();session=status().session;sdio_bridge_stop();step();
        assert(regs[BR_STOP_REQ] && regs[BR_STOP_REQ]!=regs[BR_STOP_ACK]);
        ack_stop();assert_idle(session,base,errors);
    }
    puts("PASS: 260 stop/start cycles, token wrap, raced BEGIN/DMA, last short window, async callback ownership, expected cancellation, one-shot fault reset/timeouts");
    return 0;
}
