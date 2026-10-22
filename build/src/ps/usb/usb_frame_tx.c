#include "usb_frame_tx.h"
#include <string.h>

void usb_frame_init(usb_frame_tx *t, const usb_frame_ops *ops)
{
    memset(t, 0, sizeof(*t));
    t->ops = *ops;
}
int usb_frame_busy(const usb_frame_tx *t)
{
    return t->state == USB_IMAGE_WAIT || t->state == USB_IMAGE_SEND || t->frame.token;
}
int usb_frame_request(usb_frame_tx *t, uint64_t now)
{
    if (t->blocked || t->state != USB_IMAGE_IDLE) { t->rejected++; return -1; }
    t->state = USB_IMAGE_WAIT;
    t->deadline = now + USB_IMAGE_TIMEOUT_MS;
    t->offset = t->pending = t->inflight = 0;
    t->error = 0;
    return 0;
}
int usb_frame_cancel(usb_frame_tx *t, const char *reason)
{
    t->state = USB_IMAGE_FAULT;
    t->error = reason;
    t->errors++;
    /* Even a failed queue call may have touched a descriptor. Never release
     * on the basis of its return code, or discard a lease after failed flush. */
    if (!t->ops.abort() || !t->ops.idle()) return -1;
    t->inflight = t->pending = 0;
    if (t->frame.token && t->ops.release(&t->frame)) return -1;
    return 0;
}
int usb_frame_new_session(usb_frame_tx *t)
{
    if (!t->ops.idle() || t->frame.token || t->inflight) return -1;
    t->state = USB_IMAGE_IDLE;
    t->offset = t->pending = 0;
    return 0;
}
void usb_frame_complete(usb_frame_tx *t, int success)
{
    if (t->state != USB_IMAGE_SEND || !t->inflight) return;
    if (!success) { (void)usb_frame_cancel(t, "DMA_DESCRIPTOR"); return; }
    t->offset += t->pending;
    t->pending = t->inflight = 0;
}
void usb_frame_poll(usb_frame_tx *t, uint64_t now)
{
    if (t->state != USB_IMAGE_WAIT && t->state != USB_IMAGE_SEND) return;
    if (now >= t->deadline) { (void)usb_frame_cancel(t, "TIMEOUT"); return; }
    if (t->state == USB_IMAGE_WAIT) {
        if (t->ops.down() != 0) { (void)usb_frame_cancel(t, "DOWNSAMPLE_UNSUPPORTED"); return; }
        int rc = t->ops.acquire(&t->frame);
        if (rc == 1) return;
        if (rc || !t->frame.token || !t->frame.data || t->frame.bytes != USB_IMAGE_BYTES ||
            t->frame.down != 0) {
            (void)usb_frame_cancel(t, "FRAME_DESCRIPTOR"); return;
        }
        t->state = USB_IMAGE_SEND;
    }
    if (t->inflight || !t->ops.idle()) return;
    if (t->offset == t->frame.bytes) {
        if (t->ops.release(&t->frame)) { (void)usb_frame_cancel(t, "LEASE_RELEASE"); return; }
        t->completed++;
        t->state = USB_IMAGE_IDLE;
        return;
    }
    uint32_t n = t->frame.bytes - t->offset;
    if (n > USB_IMAGE_CHUNK) n = USB_IMAGE_CHUNK;
    int rc = t->ops.send(t->frame.data + t->offset, n);
    if (rc < 0) { (void)usb_frame_cancel(t, "USB_QUEUE"); return; }
    if (!rc) { t->inflight = 1; t->pending = n; }
}
