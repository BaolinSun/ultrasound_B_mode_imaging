#include "usb.h"
#include "xusbps_COMMS.h"
#include "xinterrupt_wrap.h"
#include "../GPIO/gpio.h"
#include "sleep.h"
#include "stdlib.h"
#include "../imaging_config.h"
#include "../uart/uart.h"
#include "../system_ctrl/temp.h"
#include "usb_transport.h"
#include <string.h>
#define USB_PACK_HEADER 0xef
#define USB_CFG_REQ_TYPE 0xC0
#define USB_CFG_RESP_TYPE 0xC1
#define USB_CFG_LINE_MAX 160U
#define USB_CFG_LINE_DEPTH 4U
#define USB_CFG_RESP_MAX 160U
#define USB0_PHY_RST_PIN 49U

/*
 * SDT encodes the interrupt specifier from the device tree.  For a GIC SPI,
 * XGet_IntrId() is the SPI number (21 for USB0), not the GIC interrupt ID
 * expected by XScuGic_Connect().  Add the SPI offset (32) to obtain 53,
 * which is also the legacy XPS_USB0_INT_ID value.
 */
#define USB0_GIC_INTR_ID \
	((u16)(XGet_IntrId(XPAR_XUSBPS_0_INTERRUPTS) + \
		XGet_IntrOffset(XPAR_XUSBPS_0_INTERRUPTS)))

u8 usb_serial_ok = 0;

u8 *ep_buffer[10];
volatile u8 recv_new_data = 0;
volatile u32 bufferLength_to_send = 0;

volatile u8 buffer_use_pre = 0;
volatile u8 host_state = 0;

volatile u8 ep2_process_done = 0;
#define TOTAL_PACKET USB_TOTAL_PACKET
volatile u32 remain_packet = TOTAL_PACKET;

static volatile char usb_cfg_lines[USB_CFG_LINE_DEPTH][USB_CFG_LINE_MAX];
static volatile u32 usb_cfg_line_wr_idx = 0;
static volatile u32 usb_cfg_line_rd_idx = 0;
static volatile u32 usb_cfg_line_count = 0;
static volatile u8 usb_cfg_frame_error = 0;
static volatile u8 usb_cfg_overflow = 0;

XUsbPs UsbInstance; /* The instance of the USB Controller */
u8 usb_ready_data[4] = { 0xef, 0xef, 0x01, 0x90 };
u32 usb_init(void) {
	u32 Status;
	u32 Timeout = XUSBPS_TIMEOUT_COUNTER;
	usb_transport_init();

	/* The new PCB routes the active-low USB0 PHY reset to PS GPIO MIO49. */
	XGpioPs_SetDirectionPin(&gpiops, USB0_PHY_RST_PIN, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, USB0_PHY_RST_PIN, 1);
	XGpioPs_WritePin(&gpiops, USB0_PHY_RST_PIN, 0);
	usleep(2000);
	XGpioPs_WritePin(&gpiops, USB0_PHY_RST_PIN, 1);

	XUsbPs_WriteReg(XPAR_XUSBPS_0_BASEADDR, XUSBPS_CMD_OFFSET,
			0x00080000U | XUSBPS_CMD_RST_MASK);
	while ((XUsbPs_ReadReg(XPAR_XUSBPS_0_BASEADDR, XUSBPS_CMD_OFFSET) &
			XUSBPS_CMD_RST_MASK) != 0U && Timeout > 0U) {
		Timeout--;
	}
	if (Timeout == 0U) {
		DEBUG_PRINT("usb controller reset timeout\r\n");
		return XST_FAILURE;
	}

	Status = xusb_COMMS_init(&xInterruptController, &UsbInstance,
			XPAR_XUSBPS_0_BASEADDR, USB0_GIC_INTR_ID, 0);
	if (Status != XST_SUCCESS) {
		DEBUG_PRINT("usb init fail\r\n");
		return XST_FAILURE;
	}

	DEBUG_PRINT("Interrupt USB success\r\n");
	return XST_SUCCESS;
}

void usb_printf(char *str, u32 len) {
	u8 str_temp[30];

	if (usb_serial_ok) {
		for (int i = 0; i < 30; i++) {
			if (i < len) {
				str_temp[i] = str[i];
			} else {
				str_temp[i] = ' ';
			}
		}
		XUsbPs_EpBufferSend(&UsbInstance, 2, str_temp, 30);
	}
}
void usb_send_adc_data(u8* buffer_ptr, u32 nums, u32 package_of) {
    (void)buffer_ptr;(void)nums;(void)package_of;
    usb_transport_poll();
}

void usb_buffer_init(void) {
	for (int i = 0; i < USB_BUFFER_NUM; i++) {
		ep_buffer[i] = (u8 *) (USB_BUFFER_BASE_ADDR + i * USB_BUFFER_SIZE);
	}
}

void ero_handle() {
	while (1)
		;
}

static void usb_send_config_response(const char *text)
{
	usb_transport_reply(text);
}

void usb_reset_command_queue_isr(void)
{
    usb_cfg_line_count = usb_cfg_line_wr_idx = usb_cfg_line_rd_idx = 0;
    usb_cfg_frame_error = usb_cfg_overflow = 0;
}

static void usb_queue_config_frame(u8 *buffer, u32 buffer_length)
{
	u32 payload_len;
	u32 idx;
	u32 i;

	if (buffer_length < 4U || buffer[0] != USB_PACK_HEADER ||
			buffer[1] != USB_CFG_REQ_TYPE) {
		usb_cfg_frame_error = 1U;
		return;
	}

	payload_len = ((u32)buffer[2]) | (((u32)buffer[3]) << 8);
	if (payload_len == 0U || payload_len >= USB_CFG_LINE_MAX ||
			(payload_len + 4U) != buffer_length) {
		usb_cfg_frame_error = 1U;
		return;
	}
	if (usb_cfg_line_count >= USB_CFG_LINE_DEPTH) {
		usb_cfg_overflow = 1U;
		return;
	}

	idx = usb_cfg_line_wr_idx;
	for (i = 0; i < payload_len; i++) {
		usb_cfg_lines[idx][i] = (char)buffer[4U + i];
	}
	usb_cfg_lines[idx][payload_len] = '\0';
	usb_cfg_line_wr_idx = (usb_cfg_line_wr_idx + 1U) % USB_CFG_LINE_DEPTH;
	usb_cfg_line_count++;
}

static int usb_pop_config_line(char *line)
{
	u32 idx;
	u32 i;

	Xil_ExceptionDisableMask(XIL_EXCEPTION_IRQ);
	if (usb_cfg_line_count == 0U) {
		Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
		return 0;
	}
	idx = usb_cfg_line_rd_idx;
	for (i = 0; i < USB_CFG_LINE_MAX; i++) {
		line[i] = usb_cfg_lines[idx][i];
		if (line[i] == '\0') {
			break;
		}
	}
	line[USB_CFG_LINE_MAX - 1U] = '\0';
	usb_cfg_line_rd_idx = (usb_cfg_line_rd_idx + 1U) % USB_CFG_LINE_DEPTH;
	usb_cfg_line_count--;
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
	return 1;
}

void usb_process_commands(void)
{
	char line[USB_CFG_LINE_MAX];
	u8 frame_error;
	u8 overflow;
	usb_transport_poll();

	Xil_ExceptionDisableMask(XIL_EXCEPTION_IRQ);
	frame_error = usb_cfg_frame_error;
	overflow = usb_cfg_overflow;
	usb_cfg_frame_error = 0U;
	usb_cfg_overflow = 0U;
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);

	if (frame_error != 0U) {
		usb_send_config_response("@NACK 0 USB_FRAME");
	}
	if (overflow != 0U) {
		usb_send_config_response("@NACK 0 USB_OVERFLOW");
	}
	while (usb_pop_config_line(line)) {
		uart_handle_usb_line(line, usb_send_config_response);
	}
}

// ������յ�������
u32 xusbps_ep_buffer_handle(XUsbPs *usb, u8 endpoint, u8 *buffer,
		u32 buffer_length) {

	switch (endpoint) {
	case 1: {
		if (buffer_length >= 4U && buffer[0] == USB_PACK_HEADER &&
				buffer[1] == USB_CFG_REQ_TYPE) {
			usb_queue_config_frame(buffer, buffer_length);
			break;
		}

		if (buffer_length == 4U && buffer[0] == USB_PACK_HEADER) {
			// �������յ�������
			// byte0 0xef byte1 数据类型
			switch (buffer[1]) {
			case 0x01: {
				host_state = HOST_READY_TO_RECV;
				bufferLength_to_send = USB_TRANSFER_WINDOW_BYTES;
			}
				break;
			case 0x03: {
				host_state = HOST_STOP;
			}
				break;
			case 0x04: {
				host_state = HOST_READY_TO_RECV;
				// 从暂停中恢复
			}
				break;
			case 0x02: {
				host_state = HOST_RECVING;
			}
				break;
			default: {

			}
				break;
			}
			// 切换缓冲区
			buffer_use_pre = buffer_use_pre == 9 ? 0 : buffer_use_pre + 1;
			recv_new_data = 1;
			usb_transport_request_isr(buffer[1]);
		} else {
			DEBUG_PRINT("unsupported usb ep1 packet length: %d\r\n", buffer_length);
			usb_cfg_frame_error = 1U;
		}
	}
		break;
	case 2: {
		usb_serial_ok = 1;
	}
		break;
	default: {
		DEBUG_PRINT("other ep not supported\r\n");
	}
		break;
	}

	return XST_SUCCESS;
}
// ����ep2 ������
u32 xusbps_ep2_buffer_handle(XUsbPs *usb, u8 endpoint, u8 *buffer,
		u32 buffer_length) {
	static u8 usb_ep2_buffer[512];

// 端点2 并且上一个处理已经结束才响应中断
	if (endpoint == 2 && ep2_process_done == 1) {
		if (buffer_length >= 4) {
			// 拷贝接收到的数据
			memcpy(usb_ep2_buffer, buffer, buffer_length);
			if (usb_ep2_buffer[0] == 0xef && usb_ep2_buffer[1] == 0xee) {

				ep2_process_done = 0;

				DEBUG_PRINT("ep2 recv bf length : %d\r\n",
						bufferLength_to_send);
			}
		}
	}
	return XST_SUCCESS;
}

//获取USB包长度
u32 get_USB_datalen(u8 *frame_in) {
	u32 datalen;

//前两个字节是USB包头0xEFEF
	if (frame_in[0] == USB_PACK_HEADER && frame_in[1] == USB_PACK_HEADER) {
//接下来是4个字节的包长度
		datalen = (frame_in[2] << 8) | frame_in[3];
	} else
		datalen = 0;

	return datalen;
}

//Ep = &InstancePtr->DeviceConfig.Ep[EpNum].In;
//EpType = InstancePtr->DeviceConfig.EpCfg[EpNum].In.Type;
//do {
//
//	XUsbPs_dQHInvalidateCache(Ep->dQH);
//	Token = XUsbPs_ReaddQH(Ep->dQH, XUSBPS_dQHdTDTOKEN);
//
//} while (Token & XUSBPS_dTDTOKEN_ACTIVE_MASK);
//
//Xil_DCacheFlushRange((unsigned int) BufferPtr, BufferLen);
