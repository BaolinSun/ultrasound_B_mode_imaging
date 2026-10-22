#ifndef USB_TRANSPORT_H
#define USB_TRANSPORT_H
#include <stdint.h>
/* All foreground calls are CPU0-only. ISR hooks record bounded events only. */
void usb_transport_init(void);
int usb_transport_boot_quiesce(void); /* before frame-store INIT can revoke old leases */
void usb_transport_poll(void);
void usb_transport_request_isr(unsigned command);
void usb_transport_bus_isr(int configured);
void usb_transport_error_isr(void);
void usb_transport_tx_isr(void *buffer);
void usb_transport_reply(const char *text);
int usb_image_config_begin(void);
void usb_image_config_end(void);
void usb_transport_power_gate(int blocked);
void usb_transport_power_vote(unsigned owner,int blocked);
int usb_transport_power_idle(void);
#endif
