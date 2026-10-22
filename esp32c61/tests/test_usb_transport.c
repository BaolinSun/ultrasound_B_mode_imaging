#define __USB_H
#define _BRAM_H_
#include "../../build/src/ps/usb/usb_transport.c"
#include <assert.h>
#include <stdio.h>

XUsbPs UsbInstance;
int afe_power_allowed(void) { return 1; }
int tx_power_allowed(void) { return 1; }
unsigned mock_cpsr;
static XUsbPs_dTD descriptors[16];
static XUsbPs_dQH qh;
static u32 registers[6];
static uint64_t ticks;
static unsigned flush_fail,queue_fail,lease_held,release_fail,source_down,source_empty;
static unsigned acquisitions,releases,blocks,reply_blocks,auto_complete;
static unsigned change_epoch_on_flush;
static unsigned source_acquire_error;
static uint8_t pixels[USB_IMAGE_BYTES],capture[USB_IMAGE_BYTES];
static unsigned capture_bytes;
u32 XUsbPs_ReadReg(u32 base,u32 off) { (void)base;return registers[off/4]; }
void XUsbPs_WriteReg(u32 base,u32 off,u32 value) {
    (void)base;registers[off/4]=value;
    if(off==XUSBPS_CMD_OFFSET && !flush_fail) registers[0]=0;
    if(off==XUSBPS_EPFLUSH_OFFSET && !flush_fail) {
        registers[XUSBPS_EPRDY_OFFSET/4]=registers[XUSBPS_EPPRIME_OFFSET/4]=registers[off/4]=0;
        if(change_epoch_on_flush) { change_epoch_on_flush=0;usb_transport_bus_isr(1); }
    }
}
void XUsbPs_EpDisable(XUsbPs *u,unsigned ep,unsigned dir) { (void)u;assert(ep==1&&dir==1); }
void XUsbPs_EpEnable(XUsbPs *u,unsigned ep,unsigned dir) { (void)u;assert(ep==1&&dir==1); }
int XUsbPs_EpBufferSend(XUsbPs *u,unsigned ep,const u8 *p,u32 n) {
    assert(ep==1 && !registers[XUSBPS_EPRDY_OFFSET/4] && n && n<=16384);
    if(queue_fail) return -1;
    XUsbPs_EpIn *in=&u->DeviceConfig.Ep[1].In;
    in->dTDHead->token=(n<<16)|128;in->dTDHead->user=p;
    in->dTDHead=descriptors+(in->dTDHead-descriptors+1)%16;
    registers[XUSBPS_EPRDY_OFFSET/4]=TX_MASK;
    if(tx_kind==TX_IMAGE) { assert(lease_held);blocks++; }
    else { assert(!lease_held);reply_blocks++; }
    return 0;
}
void Xil_DCacheInvalidateRange(UINTPTR p,u32 n) { assert(lease_held&&p==(UINTPTR)pixels&&n==sizeof(pixels)); }
void XTime_GetTime(XTime *t) { *t=ticks*1000;ticks++; }
unsigned Beamform_GetDownsample(void) { return source_down; }
void usb_reset_command_queue_isr(void) { }
int xil_printf(const char *fmt,...) { (void)fmt;return 0; }
int frame_store_acquire(frame_lease_t *f,u32 watermark) {
    assert(!lease_held&&!watermark);
    if(source_empty) return 1;
    *f=(frame_lease_t){pixels,18,sizeof(pixels),source_down,123,1};lease_held=1;acquisitions++;
    return source_acquire_error?-1:0;
}
int frame_store_release(frame_lease_t *f) {
    assert(lease_held&&dma_idle());
    if(release_fail) return -1;
    f->lease=0;f->data=0;lease_held=0;releases++;return 0;
}
static void complete(u32 token) {
    assert(tx_desc);
    unsigned n=(tx_desc->token>>16)&0x7fff;
    if(tx_kind==TX_IMAGE && !token) {
        assert(capture_bytes+n<=sizeof(capture));memcpy(capture+capture_bytes,tx_desc->user,n);capture_bytes+=n;
    }
    tx_desc->token=token;
    registers[XUSBPS_EPRDY_OFFSET/4]=0;
    usb_transport_tx_isr((void*)tx_desc->user);
    UsbInstance.DeviceConfig.Ep[1].In.dTDTail=UsbInstance.DeviceConfig.Ep[1].In.dTDHead;
}
void temperature_service_poll(void) { if(auto_complete&&tx_kind==TX_IMAGE) complete(0); }
static void setup(void) {
    memset(&UsbInstance,0,sizeof(UsbInstance));memset(registers,0,sizeof(registers));memset(descriptors,0,sizeof(descriptors));
    initialized=configured=epoch=request=stop_request=io_error=tx_kind=tx_event=0;
    request_epoch=seen_epoch=faulted=config_blocked=errors_logged=reply_rd=reply_count=0;
    flush_fail=queue_fail=lease_held=release_fail=source_down=source_empty=0;
    acquisitions=releases=blocks=reply_blocks=auto_complete=capture_bytes=mock_cpsr=0;ticks=0;
    change_epoch_on_flush=0;
    source_acquire_error=0;
    lease=(frame_lease_t){0};
    UsbInstance.DeviceConfig.Ep[1].In=(XUsbPs_EpIn){descriptors,descriptors,descriptors,&qh};
    UsbInstance.DeviceConfig.EpCfg[1].In.NumBufs=16;
    registers[XUSBPS_PORTSCR1_OFFSET/4]=0x08000001;
    usb_transport_init();usb_transport_bus_isr(1);
}
int main(void) {
    for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(uint8_t)(i^(i>>11));
    setup();assert(usb_transport_boot_quiesce()==0);flush_fail=1;
    assert(usb_transport_boot_quiesce()==-1);flush_fail=0;
    initialized=0;usb_transport_poll();assert(!usb_image_config_begin());
    setup();usb_transport_request_isr(99);assert(!request);
    configured=0;usb_transport_request_isr(4);assert(!request);usb_transport_poll();
    setup();usb_transport_poll();assert(!idle()==0);
    tx_kind=TX_REPLY;assert(!idle()&&submit(pixels,1,TX_REPLY)==1);tx_kind=TX_NONE;
    registers[XUSBPS_EPRDY_OFFSET/4]=TX_MASK;assert(!idle());registers[XUSBPS_EPRDY_OFFSET/4]=0;
    configured=0;assert(submit(pixels,1,TX_REPLY)==-1);configured=1;
    faulted=1;assert(submit(pixels,1,TX_REPLY)==-1);faulted=0;
    epoch++;assert(submit(pixels,1,TX_REPLY)==-1);usb_transport_poll();
    usb_transport_tx_isr(pixels); /* no active descriptor */
    tx_kind=TX_IMAGE;tx_desc=0;usb_transport_tx_isr(pixels);tx_kind=TX_NONE;
    for(unsigned i=0;i<9;i++)usb_transport_reply("@ACK");assert(reply_count==8);
    reply_count=0;char big[200];memset(big,'z',199);big[199]=0;
    usb_transport_reply(big);assert(lengths[reply_rd]==164);reply_count=0;
    setup();change_epoch_on_flush=1;usb_transport_poll();assert(seen_epoch!=epoch);
    usb_transport_poll();assert(seen_epoch==epoch&&!faulted);
    setup();usb_transport_bus_isr(0);usb_transport_poll();assert(!configured);
    setup();usb_transport_poll();usb_transport_error_isr();usb_transport_poll();assert(faulted);
    setup();usb_transport_request_isr(1);usb_transport_poll();
#if DATA_OUTPUT_MODE == DATA_OUTPUT_USB
    assert(lease_held&&tx_kind==TX_IMAGE);usb_transport_init();assert(lease_held);
    usb_transport_request_isr(4);assert(!request);
    char ack[]="@ACK 0 CFG_END";usb_transport_reply(ack);memset(ack,'x',sizeof(ack)-1);
    usb_transport_tx_isr(pixels+1);assert(!tx_event); /* wrong/old buffer */
    UsbInstance.DeviceConfig.Ep[1].In.dTDTail=descriptors+2;
    usb_transport_tx_isr(pixels);assert(!tx_event);
    UsbInstance.DeviceConfig.Ep[1].In.dTDTail=descriptors;
    usb_transport_request_isr(1);assert(!request);
    while(image.state!=USB_IMAGE_IDLE) { complete(0);assert(!reply_blocks);usb_transport_poll(); }
    assert(blocks==49&&capture_bytes==sizeof(pixels)&&releases==1&&reply_blocks==1);
    assert(!memcmp(pixels,capture,sizeof(pixels)));
    assert(!memcmp(tx_buffer+4,"@ACK 0 CFG_END",13));complete(0);usb_transport_poll();assert(!reply_count);
    /* PC submits next READY on finishing final IN: before TX IRQ, after TX
     * IRQ, or after deferred core completion but before slot release. */
    for(unsigned order=0;order<3;order++) {
        setup();usb_transport_request_isr(1);usb_transport_poll();
        while(image.offset+image.pending<USB_IMAGE_BYTES) { complete(0);usb_transport_poll(); }
        usb_transport_request_isr(1);assert(!request); /* final descriptor still ACTIVE */
        if(order==0) {
            tx_desc->token=0;registers[XUSBPS_EPRDY_OFFSET/4]=0;
            usb_transport_request_isr(1);assert(request);
            complete(0);
        } else if(order==1) {
            complete(0);usb_transport_request_isr(1);assert(request);
        } else {
            complete(0);tx_kind=TX_NONE;tx_event=0;tx_desc=0;
            usb_frame_complete(&image,1);usb_transport_request_isr(1);assert(request);
        }
        usb_transport_poll();assert(releases==1&&request&&image.state==USB_IMAGE_IDLE);
        capture_bytes=0;usb_transport_poll();assert(acquisitions==2&&image.state==USB_IMAGE_SEND);
    }
    for(unsigned token=0;token<5;token++) {
        setup();usb_transport_request_isr(1);usb_transport_poll();
        complete(token==0?8U:token==1?32U:token==2?64U:token==3?128U:0x10000U);
        usb_transport_poll();assert(!lease_held&&faulted&&image.state==USB_IMAGE_FAULT);
    }
    setup();usb_transport_request_isr(1);usb_transport_poll();flush_fail=1;
    usb_transport_request_isr(3);usb_transport_poll();assert(lease_held&&faulted&&!releases);
    usb_transport_request_isr(1);assert(!request);
    flush_fail=0;usb_transport_bus_isr(1);usb_transport_poll();assert(!lease_held&&!faulted);
    setup();usb_transport_request_isr(1);usb_transport_poll();release_fail=1;
    usb_transport_request_isr(2);usb_transport_poll();assert(lease_held);
    assert(usb_image_config_begin()==-1);release_fail=0;
    usb_transport_bus_isr(1);usb_transport_poll();assert(!lease_held);
    setup();usb_transport_request_isr(1);usb_transport_poll();flush_fail=1;
    usb_transport_bus_isr(1);usb_transport_poll();assert(lease_held&&seen_epoch!=epoch);
    flush_fail=0;usb_transport_poll();assert(!lease_held);
    setup();usb_transport_request_isr(1);usb_transport_poll();complete(0);
    usb_transport_tx_isr(pixels); /* duplicate completion while pending */
    registers[XUSBPS_EPRDY_OFFSET/4]=TX_MASK;usb_transport_poll();assert(tx_event&&lease_held);
    registers[XUSBPS_EPRDY_OFFSET/4]=0;usb_transport_poll();assert(image.offset==USB_IMAGE_CHUNK);
    setup();usb_transport_request_isr(1);usb_transport_poll();registers[XUSBPS_PORTSCR1_OFFSET/4]=0;
    usb_transport_poll();assert(!lease_held&&faulted);
    setup();usb_transport_request_isr(1);usb_transport_poll();auto_complete=1;
    assert(!usb_image_config_begin()&&!lease_held&&config_blocked);
    usb_transport_request_isr(1);assert(!request);usb_image_config_end();assert(!config_blocked);
    setup();usb_transport_request_isr(1);usb_transport_poll();assert(usb_image_config_begin()==-1&&!lease_held&&!config_blocked);
    setup();source_down=1;usb_transport_request_isr(1);usb_transport_poll();assert(!acquisitions&&faulted);
    setup();registers[XUSBPS_PORTSCR1_OFFSET/4]=1;
    usb_transport_request_isr(1);usb_transport_poll();assert(faulted&&!acquisitions);
    setup();source_empty=1;usb_transport_request_isr(1);usb_transport_poll();ticks+=4001;
    usb_transport_poll();assert(!lease_held&&faulted);
    assert(usb_image_config_begin()==-1&&!config_blocked);
    setup();usb_transport_poll();usb_transport_reply("@ACK");usb_transport_request_isr(1);
    usb_transport_request_isr(1);assert(request);usb_transport_poll();assert(!acquisitions&&tx_kind==TX_REPLY);
    usb_transport_poll();assert(!acquisitions);complete(0);usb_transport_poll();assert(acquisitions==1);
    setup();usb_transport_poll();usb_transport_reply("@ACK");usb_transport_poll();
    usb_transport_bus_isr(1);usb_transport_poll();assert(!faulted&&!reply_count);
    setup();queue_fail=1;usb_transport_request_isr(1);usb_transport_poll();assert(!lease_held&&faulted);
    setup();source_acquire_error=release_fail=1;usb_transport_request_isr(1);usb_transport_poll();
    assert(lease_held&&image.frame.token==123&&faulted);
    release_fail=source_acquire_error=0;usb_transport_bus_isr(1);usb_transport_poll();assert(!lease_held);
    setup();usb_transport_request_isr(1);usb_transport_poll();usb_transport_bus_isr(1);
    usb_transport_request_isr(1);usb_transport_poll();assert(releases==1&&acquisitions==2&&lease_held);
#else
    assert(!acquisitions&&!lease_held&&!tx_kind);
    usb_transport_reply("@STATUS");usb_transport_poll();assert(tx_kind==TX_REPLY);
    complete(0);usb_transport_poll();assert(!reply_count&&!faulted);
#endif
    setup();usb_transport_poll();queue_fail=1;usb_transport_reply("@ACK");usb_transport_poll();assert(faulted);
    setup();usb_transport_poll();usb_transport_reply(NULL);usb_transport_poll();
    assert(tx_kind==TX_REPLY);ticks+=4001;usb_transport_poll();assert(faulted);
    setup();usb_transport_power_gate(1);usb_transport_power_vote(2,1);
    assert(power_blocks==3&&image.blocked);
    usb_transport_power_gate(0);assert(power_blocks==2&&image.blocked);
    usb_image_config_end();assert(power_blocks==2&&image.blocked);
    usb_transport_power_vote(2,0);assert(!power_blocks&&!image.blocked);
    usb_transport_power_vote(2,1);usb_transport_power_gate(1);usb_transport_power_vote(2,0);
    assert(power_blocks==1&&image.blocked);usb_transport_power_gate(0);
    puts("PASS real USB adapter: IN descriptors, persistent replies, stream isolation, flush/reset, config drain, independent power votes, macro mode");
}
