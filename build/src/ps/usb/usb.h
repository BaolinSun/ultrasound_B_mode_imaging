#ifndef __USB_H
#define __USB_H

#include "../main.h"
#include "xusbps.h"
#include "USB_CONFIG.h"

#define HOST_READY_TO_RECV 0x01
#define HOST_RECVING 0X02
#define HOST_STOP 0x03

#define USB_BUFFER_BASE_ADDR 0x1c000000
#define USB_BUFFER_NUM 10
#define USB_BUFFER_SIZE 0x1000

#define USB_EP_NUMS 0x03
extern volatile u8 send_done;
u32 usb_init(void);

void usb_buffer_init(void);
u32 xusbps_ep_buffer_handle(XUsbPs *usb, u8 endpoint, u8 *buffer,
		u32 buffer_length);
u32 get_USB_datalen(u8 *frame_in);

void usb_send_adc_data(u8* rx_buffer_ptr,u32 nums,u32 package_of);
void usb_process_commands(void);
void usb_reset_command_queue_isr(void);

void usb_printf(char *str, u32 len);

#endif
