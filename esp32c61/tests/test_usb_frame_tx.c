#include "../../build/src/ps/usb/usb_frame_tx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t frame[USB_IMAGE_BYTES], received[USB_IMAGE_BYTES];
static unsigned available, held, dma, sends, releases, downsample, descriptor_down;
static int acquire_error, queue_error, flush_error, release_error, send_busy;
static unsigned idle_fail, bad_pointer, bad_token;
static uint32_t received_n, bytes = USB_IMAGE_BYTES;
static int acquire(usb_frame_view *v) {
    assert(!held);
    if(acquire_error) return -1;
    if(!available) return 1;
    held=!bad_token;*v=(usb_frame_view){bad_pointer?0:frame,bytes,descriptor_down,17,bad_token?0U:42U,1};return 0;
}
static int release(usb_frame_view *v) {
    assert(held && !dma && v->token==42);
    if(release_error) return -1;
    held=0;v->token=0;releases++;return 0;
}
static int send(const uint8_t *p,uint32_t n) {
    assert(held && !dma && n && n<=USB_IMAGE_CHUNK);
    if(send_busy) return 1;
    if(queue_error) return -1;
    assert(p==frame+received_n && received_n+n<=USB_IMAGE_BYTES);
    memcpy(received+received_n,p,n);received_n+=n;dma=1;sends++;
    return 0;
}
static int idle(void) { return !dma && !idle_fail; }
static int abort_dma(void) { if(flush_error) return 0;dma=0;return 1; }
static unsigned down(void) { return downsample; }
static void init(usb_frame_tx *t) {
    available=1;held=dma=sends=releases=downsample=descriptor_down=0;
    acquire_error=queue_error=flush_error=release_error=send_busy=0;
    idle_fail=bad_pointer=bad_token=0;
    received_n=0;bytes=USB_IMAGE_BYTES;
    const usb_frame_ops ops={acquire,release,send,idle,abort_dma,down};usb_frame_init(t,&ops);
}
int main(void) {
    usb_frame_tx t;
    for(unsigned i=0;i<sizeof(frame);i++)frame[i]=(uint8_t)(i^(i>>9));
    init(&t);assert(!usb_frame_busy(&t));usb_frame_poll(&t,0);usb_frame_complete(&t,1);
    t.blocked=1;assert(usb_frame_request(&t,0)==-1);
    t.blocked=0;assert(!usb_frame_request(&t,0));assert(usb_frame_busy(&t));assert(usb_frame_request(&t,1)==-1);
    while(t.state!=USB_IMAGE_IDLE) {
        usb_frame_poll(&t,10);
        if(t.inflight) {
            assert(held&&!releases&&usb_frame_busy(&t));usb_frame_poll(&t,11); /* submission != completion */
            dma=0;usb_frame_complete(&t,1);usb_frame_complete(&t,1); /* late/duplicate ignored */
        }
    }
    assert(sends==49 && releases==1 && t.completed==1 && received_n==USB_IMAGE_BYTES);
    assert(!memcmp(frame,received,sizeof(frame)));
    received_n=0;
    assert(!usb_frame_request(&t,30)); /* stopped last frame can be requested again */
    usb_frame_poll(&t,31);assert(held);assert(!usb_frame_cancel(&t,"STOP"));assert(!held&&releases==2);
    assert(usb_frame_request(&t,32)==-1);assert(!usb_frame_new_session(&t));
    init(&t);available=0;assert(!usb_frame_request(&t,100));usb_frame_poll(&t,4099);
    assert(t.state==USB_IMAGE_WAIT);usb_frame_poll(&t,4100);assert(t.state==USB_IMAGE_FAULT&&!held);
    init(&t);downsample=1;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(!held&&!sends);
    init(&t);descriptor_down=2;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(!held&&!sends);
    init(&t);bytes--;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(!held&&!sends);
    init(&t);acquire_error=1;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(t.state==USB_IMAGE_FAULT);
    init(&t);queue_error=1;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(!held&&!sends);
    init(&t);send_busy=1;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(held&&!dma);
    usb_frame_poll(&t,4000);assert(!held&&!sends);
    init(&t);assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);
    usb_frame_complete(&t,0);assert(!held&&t.state==USB_IMAGE_FAULT);
    init(&t);assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);flush_error=1;
    assert(usb_frame_cancel(&t,"DISCONNECT")==-1 && held&&dma&&!releases);
    assert(usb_frame_new_session(&t)==-1);flush_error=0;
    assert(!usb_frame_cancel(&t,"USB_RESET")&&!held);assert(!usb_frame_new_session(&t));
    init(&t);assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);release_error=1;
    assert(usb_frame_cancel(&t,"STOP")==-1 && held&&!dma);release_error=0;
    assert(!usb_frame_cancel(&t,"RESET")&&!held);
    init(&t);bad_token=1;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(!held&&!sends);
    init(&t);bad_pointer=1;assert(!usb_frame_request(&t,0));usb_frame_poll(&t,1);assert(!held&&!sends);
    init(&t);assert(!usb_frame_request(&t,0));idle_fail=1;usb_frame_poll(&t,1);assert(held&&!dma&&!sends);
    assert(usb_frame_cancel(&t,"IDLE_NOT_PROVEN")==-1&&held);
    idle_fail=0;assert(usb_frame_new_session(&t)==-1);assert(usb_frame_busy(&t));
    assert(!usb_frame_cancel(&t,"RECOVER"));t.inflight=1;assert(usb_frame_new_session(&t)==-1);t.inflight=0;
    assert(!usb_frame_new_session(&t));
    init(&t);assert(!usb_frame_request(&t,0));
    while(t.offset<USB_IMAGE_BYTES) { usb_frame_poll(&t,10);dma=0;usb_frame_complete(&t,1); }
    release_error=1;usb_frame_poll(&t,11);assert(held&&t.state==USB_IMAGE_FAULT);
    release_error=0;assert(!usb_frame_cancel(&t,"RECOVER"));
    puts("PASS USB frame state machine: 49 chunks, exact bytes, ownership, duplicate, stop, timeout, errors, reset");
}
