#include "usb_transport.h"
#include "../system_ctrl/imaging_power.h"
#include "usb_frame_tx.h"
#include "usb.h"
#include "../data_transfer/output_config.h"
#include "../data_transfer/frame_store.h"
#include "../bram/bram.h"
#include "../system_ctrl/temp.h"
#include "xusbps_endpoint.h"
#include "xiltimer.h"
#include "xil_exception.h"
#include <string.h>

extern XUsbPs UsbInstance;
#define TX_MASK (1U << 17)
#define REPLIES 8U
#define REPLY_BYTES 256U /* aligned slots: no dirty cache-line sharing with DMA */
enum { TX_NONE, TX_IMAGE, TX_REPLY };
static usb_frame_tx image;
static frame_lease_t lease;
static uint8_t replies[REPLIES][REPLY_BYTES] __attribute__((aligned(32)));
static uint32_t lengths[REPLIES], reply_rd, reply_count;
static volatile unsigned initialized, configured, epoch, request, stop_request, io_error;
static volatile unsigned request_epoch;
static volatile unsigned tx_kind, tx_event, tx_ok;
static XUsbPs_dTD * volatile tx_desc;
static const uint8_t * volatile tx_buffer;
static uint64_t tx_deadline;
static volatile unsigned seen_epoch, faulted, config_blocked;
static uint32_t errors_logged;
static unsigned power_blocks; /* Independent AFE=1 / TX=2 votes; never clear a peer. */

static uint64_t ms(void) { XTime t; XTime_GetTime(&t); return (uint64_t)t * 1000U / (uint64_t)(COUNTS_PER_SECOND); }
static u32 lock(void) { u32 saved = mfcpsr(); Xil_ExceptionDisableMask(XIL_EXCEPTION_IRQ); return saved; }
static void unlock(u32 saved) { mtcpsr(saved); }
static int dma_idle(void)
{
    u32 base = UsbInstance.Config.BaseAddress;
    return ((XUsbPs_ReadReg(base, XUSBPS_EPRDY_OFFSET) |
             XUsbPs_ReadReg(base, XUSBPS_EPPRIME_OFFSET) |
             XUsbPs_ReadReg(base, XUSBPS_EPFLUSH_OFFSET)) & TX_MASK) == 0;
}
static int idle(void) { return tx_kind == TX_NONE && dma_idle(); }

static int abort_dma(void)
{
    /* Disable new IN transactions before flushing. No ISR can submit another
     * descriptor; foreground owns the endpoint. Never reset OUT/config queues. */
    faulted = 1;
    XUsbPs_EpDisable(&UsbInstance, 1, XUSBPS_EP_DIRECTION_IN);
    XUsbPs_WriteReg(UsbInstance.Config.BaseAddress, XUSBPS_EPFLUSH_OFFSET, TX_MASK);
    uint64_t end = ms() + 100U;
    while (!dma_idle()) if (ms() >= end) return 0;
    u32 saved = lock();
    XUsbPs_EpIn *ep = &UsbInstance.DeviceConfig.Ep[1].In;
    /* Quiescence is proven before touching DMA-owned descriptors. Reset the
     * software ring too, so a late completion cannot recycle an old buffer. */
    for (unsigned i = 0; i < UsbInstance.DeviceConfig.EpCfg[1].In.NumBufs; i++) {
        XUsbPs_WritedTD(&ep->dTDs[i], XUSBPS_dTDTOKEN, 0);
        XUsbPs_dTDFlushCache(&ep->dTDs[i]);
    }
    ep->dTDHead = ep->dTDTail = ep->dTDs;
    XUsbPs_WritedQH(ep->dQH, XUSBPS_dQHdTDNLP, 1U);
    XUsbPs_WritedQH(ep->dQH, XUSBPS_dQHdTDTOKEN, 0);
    XUsbPs_dQHFlushCache(ep->dQH);
    XUsbPs_WriteReg(UsbInstance.Config.BaseAddress, XUSBPS_EPCOMPL_OFFSET, TX_MASK);
    tx_desc = 0; tx_buffer = 0; tx_kind = TX_NONE; tx_event = 0;
    unlock(saved);
    return 1;
}
static int acquire(usb_frame_view *v)
{
#if DATA_OUTPUT_MODE == DATA_OUTPUT_USB
    int rc = frame_store_acquire(&lease, 0); /* explicit request: latest, including stopped frame */
    /* An ABI/metadata failure can itself retain a lease after failed cleanup.
     * Mirror ownership even on failure; never hide that token from recovery. */
    if (lease.lease) {
        *v = (usb_frame_view){lease.data, lease.bytes, lease.decimation, lease.frame, lease.lease, lease.slot};
    }
    if (!rc) Xil_DCacheInvalidateRange((UINTPTR)lease.data, lease.bytes);
    return rc;
#else
    (void)v; return -1;
#endif
}
static int release(usb_frame_view *v)
{
    if (!idle() || frame_store_release(&lease)) return -1;
    v->token = 0; v->data = 0; return 0;
}
static int submit(const uint8_t *data, uint32_t n, unsigned kind)
{
    if (!idle()) return 1;
    u32 saved = lock();
    if (!configured || faulted || epoch != seen_epoch) { unlock(saved); return -1; }
    tx_desc = UsbInstance.DeviceConfig.Ep[1].In.dTDHead;
    tx_buffer = data; tx_kind = kind; tx_event = 0;
    tx_deadline = ms() + USB_IMAGE_TIMEOUT_MS;
    /* One <=16KiB descriptor; no per-block ZLP, no third frame allocation. */
    int rc = XUsbPs_EpBufferSend(&UsbInstance, 1, data, n);
    unlock(saved);
    return rc == XST_SUCCESS ? 0 : -1;
}
static int send_image(const uint8_t *p, uint32_t n) { return submit(p, n, TX_IMAGE); }
static unsigned down(void) { return Beamform_GetDownsample(); }

int usb_transport_boot_quiesce(void)
{
    u32 base = XPAR_XUSBPS_0_BASEADDR;
    u32 cmd = XUsbPs_ReadReg(base, XUSBPS_CMD_OFFSET);
    XUsbPs_WriteReg(base, XUSBPS_CMD_OFFSET, (cmd & ~XUSBPS_CMD_RS_MASK) | XUSBPS_CMD_RST_MASK);
    uint64_t end = ms() + 100U;
    do {
        if (!(XUsbPs_ReadReg(base, XUSBPS_CMD_OFFSET) & XUSBPS_CMD_RST_MASK) &&
            !((XUsbPs_ReadReg(base, XUSBPS_EPRDY_OFFSET) |
               XUsbPs_ReadReg(base, XUSBPS_EPPRIME_OFFSET)) & TX_MASK)) return 0;
    } while (ms() < end);
    return -1;
}

void usb_transport_init(void)
{
    if (initialized) return; /* never erase a live DMA lease on reinitialization */
    const usb_frame_ops ops = {acquire, release, send_image, idle, abort_dma, down};
    usb_frame_init(&image, &ops);
    initialized = 1;
}
void usb_transport_bus_isr(int ready)
{
    configured = ready != 0; epoch++; request = 0; stop_request = 0;
    usb_reset_command_queue_isr();
}
void usb_transport_error_isr(void) { io_error = 1; }
static int final_image_consumed(void)
{
    /* Host can finish its IN read and submit READY before CPU0 has serviced
     * the final IN interrupt. Permit one next request, never an early replay.
     * Both RX-before-TX IRQ order and foreground release latency are valid. */
    if (image.state != USB_IMAGE_SEND) return 0;
    if (image.offset == USB_IMAGE_BYTES && !image.inflight) return 1;
    if (tx_kind != TX_IMAGE || !tx_desc || image.offset + image.pending != USB_IMAGE_BYTES) return 0;
    XUsbPs_dTDInvalidateCache(tx_desc);
    u32 token = XUsbPs_ReaddTD(tx_desc, XUSBPS_dTDTOKEN);
    return (token & (XUSBPS_dTDTOKEN_ACTIVE_MASK | XUSBPS_dTDTOKEN_XERR_MASK |
            XUSBPS_dTDTOKEN_BUFERR_MASK | XUSBPS_dTDTOKEN_HALT_MASK |
            XUSBPS_dTDTOKEN_LEN_MASK)) == 0;
}
void usb_transport_request_isr(unsigned cmd)
{
    if (cmd == 3 || cmd == 2) { request = 0; stop_request = 1; return; }
    if ((cmd == 1 || cmd == 4) && configured && !config_blocked && !request &&
        (epoch != seen_epoch || (!faulted && (image.state == USB_IMAGE_IDLE || final_image_consumed())))) {
        request_epoch = epoch; request = 1;
    }
}
void usb_transport_tx_isr(void *buffer)
{
    if (!tx_kind || tx_event || !tx_desc || buffer != tx_buffer) return;
    XUsbPs_dTD *td = UsbInstance.DeviceConfig.Ep[1].In.dTDTail;
    if (td != tx_desc) return;
    XUsbPs_dTDInvalidateCache(td);
    u32 token = XUsbPs_ReaddTD(td, XUSBPS_dTDTOKEN);
    tx_ok = (token & (XUSBPS_dTDTOKEN_ACTIVE_MASK | XUSBPS_dTDTOKEN_XERR_MASK |
             XUSBPS_dTDTOKEN_BUFERR_MASK | XUSBPS_dTDTOKEN_HALT_MASK |
             XUSBPS_dTDTOKEN_LEN_MASK)) == 0;
    tx_event = 1;
}
void usb_transport_reply(const char *text)
{
    if (!text) text = "@NACK 0 USB_RESPONSE";
    if (reply_count == REPLIES) { xil_printf("@USB ERROR reply queue full\r\n"); return; }
    unsigned i = (reply_rd + reply_count) % REPLIES;
    size_t n = strlen(text); if (n > 160U) n = 160U;
    replies[i][0] = 0xef; replies[i][1] = 0xc1;
    replies[i][2] = (uint8_t)n; replies[i][3] = (uint8_t)(n >> 8);
    memcpy(replies[i] + 4, text, n); lengths[i] = (uint32_t)n + 4U;
    reply_count++;
}
void usb_transport_poll(void)
{
    if (!initialized) return;
    unsigned fresh = epoch;
    if (fresh != seen_epoch) {
        /* SET_CONFIGURATION alone is also treated as a new stream session.
         * Always flush prior IN data before accepting another raw frame. */
        if (request_epoch != fresh) request = 0;
        int safe = (usb_frame_busy(&image) || tx_kind) ?
            usb_frame_cancel(&image, "USB_SESSION_RESET") == 0 : abort_dma();
        reply_count = reply_rd = 0;
        if (!safe) return;
        u32 saved = lock();
        if (fresh != epoch) { unlock(saved); return; }
        seen_epoch = fresh; io_error = 0;
        if (configured && usb_frame_new_session(&image) == 0) {
            faulted = 0;
            XUsbPs_EpEnable(&UsbInstance, 1, XUSBPS_EP_DIRECTION_IN);
        }
        unlock(saved);
    }
    if (!configured || !(XUsbPs_ReadReg(UsbInstance.Config.BaseAddress, XUSBPS_PORTSCR1_OFFSET) & XUSBPS_PORTSCR_CCS_MASK)) {
        if (!faulted && (usb_frame_busy(&image) || tx_kind)) (void)usb_frame_cancel(&image, "USB_DISCONNECT");
        return;
    }
    if (io_error || stop_request) {
        io_error = stop_request = request = 0;
        (void)usb_frame_cancel(&image, "USB_STOP_OR_ERROR");
    }
    if (tx_event) {
        unsigned kind = tx_kind, ok = tx_ok;
        tx_event = 0;
        if (!ok) { (void)usb_frame_cancel(&image, "DMA_DESCRIPTOR"); }
        else if (dma_idle()) {
            u32 saved = lock(); /* final state + descriptor retirement must be atomic vs next READY */
            tx_kind = TX_NONE; tx_desc = 0; tx_buffer = 0;
            if (kind == TX_IMAGE) usb_frame_complete(&image, 1);
            else if (kind == TX_REPLY && reply_count) {
                reply_rd = (reply_rd + 1U) % REPLIES; reply_count--;
            }
            unlock(saved);
        } else tx_event = 1;
    }
    uint64_t now = ms();
    if (tx_kind == TX_REPLY && now >= tx_deadline) (void)usb_frame_cancel(&image, "REPLY_TIMEOUT");
    if (faulted) goto report;
    if (request && image.state == USB_IMAGE_IDLE && !tx_kind && !reply_count && !config_blocked && !power_blocks && imaging_power_allowed()) {
#if DATA_OUTPUT_MODE == DATA_OUTPUT_USB
        u32 speed = XUsbPs_ReadReg(UsbInstance.Config.BaseAddress, XUSBPS_PORTSCR1_OFFSET) & XUSBPS_PORTSCR_PSPD_MASK;
        if (speed != 0x08000000U) {
            request = 0; (void)usb_frame_cancel(&image, "HIGH_SPEED_REQUIRED"); goto report;
        }
#endif
        u32 saved = lock(); /* claim request and transition atomically vs OUT ISR */
        request = 0;
#if DATA_OUTPUT_MODE == DATA_OUTPUT_USB
        (void)usb_frame_request(&image, now);
#else
        xil_printf("@USB IMAGE_DISABLED compiled=SDIO\r\n");
#endif
        unlock(saved);
    }
    usb_frame_poll(&image, now);
report:
    if (image.errors != errors_logged) {
        errors_logged = image.errors;
        xil_printf("@USB state=%u frame=%u sent=%u lease=%u error=%s; reopen USB session\r\n",
            image.state, image.frame.id, image.offset, image.frame.token, image.error ? image.error : "none");
    }
    /* Never interleave config bytes with any part of the raw frame. */
    if (!faulted && !usb_frame_busy(&image) && reply_count && !tx_kind) {
        if (submit(replies[reply_rd], lengths[reply_rd], TX_REPLY) < 0)
            (void)usb_frame_cancel(&image, "REPLY_QUEUE");
    }
}
int usb_image_config_begin(void)
{
    if (!initialized) return 0;
    config_blocked = image.blocked = 1; request = 0;
    uint64_t end = ms() + USB_IMAGE_TIMEOUT_MS;
    while (usb_frame_busy(&image)) {
        usb_transport_poll();
        temperature_service_poll();
        if (ms() >= end) {
            (void)usb_frame_cancel(&image, "CONFIG_DRAIN");
            config_blocked = image.blocked = 0; return -1;
        }
    }
    if (image.frame.token || image.state == USB_IMAGE_FAULT) { usb_image_config_end(); return -1; }
    return 0;
}
void usb_image_config_end(void) { config_blocked=0; image.blocked=power_blocks!=0; }
void usb_transport_power_vote(unsigned owner,int blocked) {
    if(blocked)power_blocks|=owner;else power_blocks&=~owner;
    if(!initialized)return;
    image.blocked=config_blocked || power_blocks;request=0;
}
void usb_transport_power_gate(int blocked) {usb_transport_power_vote(1U,blocked);}
int usb_transport_power_idle(void) {
    return !initialized || (!usb_frame_busy(&image) && !image.frame.token && !tx_kind);
}
