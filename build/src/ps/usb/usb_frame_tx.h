#ifndef USB_FRAME_TX_H
#define USB_FRAME_TX_H
#include <stdint.h>

#define USB_IMAGE_BYTES 787456U
#define USB_IMAGE_CHUNK 16384U
#define USB_IMAGE_TIMEOUT_MS 4000U
typedef struct {
    const uint8_t *data;
    uint32_t bytes, down, id, token, slot;
} usb_frame_view;
typedef struct {
    int (*acquire)(usb_frame_view *); /* 0 acquired, 1 empty, -1 fault */
    int (*release)(usb_frame_view *);
    int (*send)(const uint8_t *, uint32_t); /* 0 queued, 1 busy, -1 fault */
    int (*idle)(void); /* hardware DMA idle, not just software submission */
    int (*abort)(void); /* bounded flush + proof of hardware idle */
    unsigned (*down)(void);
} usb_frame_ops;
enum { USB_IMAGE_IDLE, USB_IMAGE_WAIT, USB_IMAGE_SEND, USB_IMAGE_FAULT };
typedef struct {
    usb_frame_ops ops;
    usb_frame_view frame;
    uint64_t deadline;
    volatile uint32_t offset, pending; /* final-IN -> next-OUT request race */
    uint32_t completed, errors, rejected;
    volatile unsigned state; /* read by request ISR; written only in foreground */
    volatile unsigned inflight;
    unsigned blocked;
    const char *error;
} usb_frame_tx;
void usb_frame_init(usb_frame_tx *, const usb_frame_ops *);
int usb_frame_request(usb_frame_tx *, uint64_t now);
void usb_frame_poll(usb_frame_tx *, uint64_t now);
void usb_frame_complete(usb_frame_tx *, int success); /* foreground only */
int usb_frame_cancel(usb_frame_tx *, const char *reason);
int usb_frame_new_session(usb_frame_tx *); /* only after real USB reset/config */
int usb_frame_busy(const usb_frame_tx *);
#endif
