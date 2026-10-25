/******************************************************************************
 *
 * Copyright (C) 2010 - 2015 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Use of the Software is limited solely to applications:
 * (a) running on a Xilinx device, or
 * (b) that interact with a Xilinx device through a bus or interconnect.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * XILINX CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 *
 ******************************************************************************/

#include "xusbps_COMMS.h"

#include "xusbps.h"
#include "xusbps_ch9.h"
#include "xusbps_ch9_winusb.h"

#include "xusbps_Ringbuffer.h"
#include "xil_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DEBUG.h"
#include "../main.h"
#include "USB_CONFIG.h"
#include "usb_transport.h"
/************************** Constant Definitions *****************************/
#define EP_PACKAGE_SIZE USB_EP_MaxPacketSize

#define MEMORY_SIZE (128 * 1024)
#ifdef __ICCARM__
#pragma data_alignment = 32
u8 Buffer[MEMORY_SIZE];
#pragma data_alignment = 4
#else
u8 Buffer[MEMORY_SIZE] ALIGNMENT_CACHELINE;
#endif

static volatile int NumIrqs = 0;
static volatile int NumReceivedFrames = 0;

RingBuffer xusb_rx_buffer;

XUsbPs_Local usb_local_data;

int xusb_COMMS_init(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
		UINTPTR UsbBaseAddress, u16 UsbIntrId, u32 rx_buf_size) {
	int Status;
	u8 *MemPtr = NULL;
	int ReturnStatus = XST_FAILURE;

	/* For this example we only configure 2 endpoints:
	 *   Endpoint 0 (default control endpoint)
	 *   Endpoint 1 (BULK data endpoint)
	 *   Endpoint 2 (BULK data endpoint)
	 */
	const u8 NumEndpoints = USB_EP_NUMS;

	XUsbPs_Config *UsbConfigPtr;
	XUsbPs_DeviceConfig DeviceConfig = {0};

	/* Initialize the USB driver so that it's ready to use,
	 * specify the controller ID that is generated in xparameters.h
	 */
	UsbConfigPtr = XUsbPs_LookupConfig((u32) UsbBaseAddress);
	if (NULL == UsbConfigPtr) {
		goto out;
	}

	/* We are passing the physical base address as the third argument
	 * because the physical and virtual base address are the same in our
	 * example.  For systems that support virtual memory, the third
	 * argument needs to be the virtual base address.
	 */
	Status = XUsbPs_CfgInitialize(UsbInstancePtr, UsbConfigPtr,
			UsbConfigPtr->BaseAddress);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	/* Set up the interrupt subsystem.
	 */
	Status = UsbSetupIntrSystem(IntcInstancePtr, UsbInstancePtr, UsbIntrId);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	/* Configuration of the DEVICE side of the controller happens in
	 * multiple stages.
	 *
	 * 1) The user configures the desired endpoint configuration using the
	 * XUsbPs_DeviceConfig data structure. This includes the number of
	 * endpoints, the number of Transfer Descriptors for each endpoint
	 * (each endpoint can have a different number of Transfer Descriptors)
	 * and the buffer size for the OUT (receive) endpoints.  Each endpoint
	 * can have different buffer sizes.
	 *
	 * 2) Request the required size of DMAable memory from the driver using
	 * the XUsbPs_DeviceMemRequired() call.
	 *
	 * 3) Allocate the DMAable memory and set up the DMAMemVirt and
	 * DMAMemPhys members in the XUsbPs_DeviceConfig data structure.
	 *
	 * 4) Configure the DEVICE side of the controller by calling the
	 * XUsbPs_ConfigureDevice() function.
	 */

	/*
	 * For this example we only configure Endpoint 0 and Endpoint 1.
	 *
	 * Bufsize = 0 indicates that there is no buffer allocated for OUT
	 * (receive) endpoint 0. Endpoint 0 is a control endpoint and we only
	 * receive control packets on that endpoint. Control packets are 8
	 * bytes in size and are received into the Queue Head's Setup Buffer.
	 * Therefore, no additional buffer space is needed.
	 */
	DeviceConfig.EpCfg[0].Out.Type = XUSBPS_EP_TYPE_CONTROL;
	DeviceConfig.EpCfg[0].Out.NumBufs = 2;
	DeviceConfig.EpCfg[0].Out.BufSize = 64;
	DeviceConfig.EpCfg[0].Out.MaxPacketSize = 64;
	DeviceConfig.EpCfg[0].In.Type = XUSBPS_EP_TYPE_CONTROL;
	DeviceConfig.EpCfg[0].In.NumBufs = 2;
	DeviceConfig.EpCfg[0].In.MaxPacketSize = 64;

	DeviceConfig.EpCfg[1].Out.Type = XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[1].Out.NumBufs = EP_PACKAGE_SIZE / 32;
	DeviceConfig.EpCfg[1].Out.BufSize = EP_PACKAGE_SIZE;
	DeviceConfig.EpCfg[1].Out.MaxPacketSize = EP_PACKAGE_SIZE;
	DeviceConfig.EpCfg[1].In.Type = XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[1].In.NumBufs = EP_PACKAGE_SIZE / 32;
	DeviceConfig.EpCfg[1].In.MaxPacketSize = EP_PACKAGE_SIZE;

	DeviceConfig.EpCfg[2].Out.Type = XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[2].Out.NumBufs = 16;
	DeviceConfig.EpCfg[2].Out.BufSize = 512;
	DeviceConfig.EpCfg[2].Out.MaxPacketSize = 512;
	DeviceConfig.EpCfg[2].In.Type = XUSBPS_EP_TYPE_BULK;
	DeviceConfig.EpCfg[2].In.NumBufs = 16;
	DeviceConfig.EpCfg[2].In.MaxPacketSize = 512;

	DeviceConfig.NumEndpoints = NumEndpoints;

	MemPtr = (u8 *) &Buffer[0];
//	MemPtr = (u8 *) (0x30000000);
	memset(MemPtr, 0, MEMORY_SIZE);
	Xil_DCacheFlushRange((unsigned int) MemPtr, MEMORY_SIZE);

	/* Finish the configuration of the DeviceConfig structure and configure
	 * the DEVICE side of the controller.
	 */
	DeviceConfig.DMAMemPhys = (u32) MemPtr;

	Status = XUsbPs_ConfigureDevice(UsbInstancePtr, &DeviceConfig);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	/* Set the handler for receiving frames. */
	// 配置错误处理
	Status = XUsbPs_IntrSetHandler(UsbInstancePtr, UsbIntrHandler,
			UsbInstancePtr,
			XUSBPS_IXR_UE_MASK | XUSBPS_IXR_UR_MASK);
	if (XST_SUCCESS != Status) {
		goto out;
	}

	/* Set the handler for handling endpoint 0 events. This is where we
	 * will receive and handle the Setup packet from the host.
	 */
	Status = XUsbPs_EpSetHandler(UsbInstancePtr, 0,
	XUSBPS_EP_DIRECTION_OUT, XUsbPs_Ep0EventHandler, UsbInstancePtr);

	/* Set the handler for handling endpoint 1 events.
	 *
	 * IN completion is mandatory: DDR slots and persistent reply buffers
	 * remain owned by DMA until their descriptor has completed successfully.
	 */
	Status = XUsbPs_EpSetHandler(UsbInstancePtr, 1,
	XUSBPS_EP_DIRECTION_OUT, XUsbPs_Ep1EventHandler, UsbInstancePtr);
	if (Status != XST_SUCCESS) goto out;
	Status = XUsbPs_EpSetHandler(UsbInstancePtr, 1,
	XUSBPS_EP_DIRECTION_IN, XUsbPs_Ep1EventHandler, UsbInstancePtr);
	if (Status != XST_SUCCESS) goto out;

	Status = XUsbPs_EpSetHandler(UsbInstancePtr, 2,
	XUSBPS_EP_DIRECTION_OUT, XUsbPs_Ep2EventHandler, UsbInstancePtr);

	/* Enable the interrupts. */
	XUsbPs_IntrEnable(UsbInstancePtr, XUSBPS_IXR_UR_MASK | XUSBPS_IXR_UI_MASK | XUSBPS_IXR_UE_MASK);

//	Status = xusb_Ringbuffer_init(&xusb_rx_buffer, rx_buf_size);
	if (Status != XST_SUCCESS) {
		goto out;
	}
	/* Start the USB engine */
	XUsbPs_Start(UsbInstancePtr);

	DEBUG_PRINT("Cleanup\r\n");

	/* Set return code to indicate success and fall through to clean-up
	 * code.
	 */
	return XST_SUCCESS;

	out: DEBUG_PRINT("usb  init out fail\r\n");
	/* Clean up. It's always safe to disable interrupts and clear the
	 * handlers, even if they have not been enabled/set. The same is true
	 * for disabling the interrupt subsystem.
	 */
	XUsbPs_Stop(UsbInstancePtr);
	XUsbPs_IntrDisable(UsbInstancePtr, XUSBPS_IXR_ALL);
	(int) XUsbPs_IntrSetHandler(UsbInstancePtr, NULL, NULL, 0);

	UsbDisableIntrSystem(IntcInstancePtr, UsbIntrId);

	/* Free allocated memory.
	 */
	if (NULL != UsbInstancePtr->UserDataPtr) {
		free(UsbInstancePtr->UserDataPtr);
	}
	return ReturnStatus;
}

/*****************************************************************************/
/**
 *
 * This function is the handler which performs processing for the USB driver.
 * It is called from an interrupt context such that the amount of processing
 * performed should be minimized.
 *
 * This handler provides an example of how to handle USB interrupts and
 * is application specific.
 *
 * @param	CallBackRef is the Upper layer callback reference passed back
 *		when the callback function is invoked.
 * @param 	Mask is the Interrupt Mask.
 * @param	CallBackRef is the User data reference.
 *
 * @return
 * 		- XST_SUCCESS if successful
 * 		- XST_FAILURE on error
 *
 * @note	None.
 *
 ******************************************************************************/
static void UsbIntrHandler(void *CallBackRef, u32 Mask) {
	(void)CallBackRef;
	if (Mask & XUSBPS_IXR_UR_MASK) usb_transport_bus_isr(0);
	else if (Mask & XUSBPS_IXR_UE_MASK) usb_transport_error_isr();
	NumIrqs++;
}

/*****************************************************************************/
/**
 * This funtion is registered to handle callbacks for endpoint 0 (Control).
 *
 * It is called from an interrupt context such that the amount of processing
 * performed should be minimized.
 *
 *
 * @param	CallBackRef is the reference passed in when the function
 *		was registered.
 * @param	EpNum is the Number of the endpoint on which the event occured.
 * @param	EventType is type of the event that occured.
 *
 * @return	None.
 *
 ******************************************************************************/
static void XUsbPs_Ep0EventHandler(void *CallBackRef, u8 EpNum, u8 EventType,
		void *Data) {
	XUsbPs *InstancePtr;
	int Status;
	XUsbPs_SetupData SetupData;
	u8 *BufferPtr;
	u32 BufferLen;
	u32 Handle;

	Xil_AssertVoid(NULL != CallBackRef);

	InstancePtr = (XUsbPs *) CallBackRef;

	switch (EventType) {

	/* Handle the Setup Packets received on Endpoint 0. */
	case XUSBPS_EP_EVENT_SETUP_DATA_RECEIVED:
//		xil_printf("EP0 setup\r\n");
		Status = XUsbPs_EpGetSetupData(InstancePtr, EpNum, &SetupData);
		if (XST_SUCCESS == Status) {
			/* Handle the setup packet. */
			(int) XUsbPs_Ch9HandleSetupPacket(InstancePtr, &SetupData);
		} else {
//			xil_printf("EP0 ERROR %d\r\n", Status);
		}
		break;

		/* We get data RX events for 0 length packets on endpoint 0. We receive
		 * and immediately release them again here, but there's no action to be
		 * taken.
		 */
	case XUSBPS_EP_EVENT_DATA_RX:
		/* Get the data buffer. */
//		xil_printf("EP0 setup \r\n");
		Status = XUsbPs_EpBufferReceive(InstancePtr, EpNum, &BufferPtr,
				&BufferLen, &Handle);
		if (XST_SUCCESS == Status) {
			/* Return the buffer. */
			XUsbPs_EpBufferRelease(Handle);
		} else {
			DEBUG_PRINT("EP0 RX ERROR %d\r\n", Status);
		}
		break;

	default:
		/* Unhandled event. Ignore. */
		break;
	}
#ifdef COMMS_DBG
	xil_printf("EP0\r\n");
#endif

}

/*****************************************************************************/
/**
 * This funtion is registered to handle callbacks for endpoint 1 (Bulk data).
 *
 * It is called from an interrupt context such that the amount of processing
 * performed should be minimized.
 *
 *
 * @param	CallBackRef is the reference passed in when the function was
 *		registered.
 * @param	EpNum is the Number of the endpoint on which the event occured.
 * @param	EventType is type of the event that occured.
 *
 * @return	None.
 *
 * @note 	None.
 *
 ******************************************************************************/
static void XUsbPs_Ep1EventHandler(void *CallBackRef, u8 EpNum, u8 EventType,
		void *Data) {
#ifdef COMMS_DBG
	xil_printf("EP1\r\n");
#endif

	XUsbPs *usb;
	int status;
	u8 *buffer;
	u32 invalidate_length;
	u32 buffer_length;
	u32 handle;

	Xil_AssertVoid(NULL != CallBackRef);

	usb = (XUsbPs *) CallBackRef;

	switch (EventType) {
	// 接收完成后会触发这个中断
	case XUSBPS_EP_EVENT_DATA_RX: {
		/* Get the data buffer */
		status = XUsbPs_EpBufferReceive(usb, EpNum, &buffer, &buffer_length,
				&handle);
		if (status != XST_SUCCESS) { usb_transport_error_isr(); break; }
		/* Invalidate the buffer pointer */
		invalidate_length = buffer_length;
		/* Ensure alignment for invalidation */
		if (buffer_length % 32) {
			invalidate_length = (buffer_length / 32) * 32 + 32;
		}

		/* Invalidate the cache for the range of the received data buffer */
		Xil_DCacheInvalidateRange((unsigned int) buffer, invalidate_length);

		if (status == XST_SUCCESS) {
//			status = xusb_COMMS_handle_bulk_request(usb, EpNum, buffer,
//					buffer_length);
			status = xusbps_ep_buffer_handle(usb, EpNum, buffer, buffer_length);

#ifdef XUSB_CDC_DEBUG
			if (status != XST_SUCCESS) {
				DEBUG_PRINT("E: Unable to handle CDC bulk request: %d\n", status);
			}
#endif
			XUsbPs_EpBufferRelease(handle);
		}
	}
		break;
	case XUSBPS_EP_EVENT_DATA_TX: {
		usb_transport_tx_isr(Data);
	}
		break;
	default:
		/* Unhandled event */
#ifdef XUSB_CDC_DEBUG
		xil_printf("W: Unhandled event type %d received on EP1\n", event_type);
#endif
		break;
	}

}

static void XUsbPs_Ep2EventHandler(void *CallBackRef, u8 EpNum, u8 EventType,
		void *Data) {
	DEBUG_PRINT("EP2\r\n");
	XUsbPs *usb;
	int status;
	u8 *buffer;
	u32 invalidate_length;
	u32 buffer_length;
	u32 handle;

	Xil_AssertVoid(NULL != CallBackRef);

	usb = (XUsbPs *) CallBackRef;

	switch (EventType) {
	// 接收完成后会触发这个中断
	case XUSBPS_EP_EVENT_DATA_RX: {
		/* Get the data buffer */
		status = XUsbPs_EpBufferReceive(usb, EpNum, &buffer, &buffer_length,
				&handle);
		if (status != XST_SUCCESS) { usb_transport_error_isr(); break; }
		/* Invalidate the buffer pointer */
		invalidate_length = buffer_length;
		/* Ensure alignment for invalidation */
		if (buffer_length % 32) {
			invalidate_length = (buffer_length / 32) * 32 + 32;
		}

		/* Invalidate the cache for the range of the received data buffer */
		Xil_DCacheInvalidateRange((unsigned int) buffer, invalidate_length);

		if (status == XST_SUCCESS) {
//			status = xusb_COMMS_handle_bulk_request(usb, EpNum, buffer,
//					buffer_length);
			status = xusbps_ep_buffer_handle(usb, EpNum, buffer, buffer_length);

#ifdef XUSB_CDC_DEBUG
			if (status != XST_SUCCESS) {
				xil_printf("E: Unable to handle CDC bulk request: %d\n", status);
			}
#endif
			XUsbPs_EpBufferRelease(handle);
		}
	}
		break;
	case XUSBPS_EP_EVENT_DATA_TX: {
//		xil_printf("ep1 send OK\r\n ");
	}
		break;
	default:
		/* Unhandled event */
#ifdef XUSB_CDC_DEBUG
		xil_printf("W: Unhandled event type %d received on EP1\n", event_type);
#endif
		break;
	}
}

void xusb_COMMS_deinit(XScuGic *scugic, XUsbPs* usbps, u32 usb_intr_id) {
	XUsbPs_Stop(usbps);
	XUsbPs_IntrDisable(usbps, XUSBPS_IXR_ALL);
	UsbDisableIntrSystem(scugic, usb_intr_id);
}

static int UsbSetupIntrSystem(XScuGic *IntcInstancePtr, XUsbPs *UsbInstancePtr,
		u16 UsbIntrId) {
	int Status;

	XScuGic_InterruptUnmapFromCpu(IntcInstancePtr, 1U, /* Remove CPU1 target. */
			UsbIntrId);
	XScuGic_InterruptMaptoCpu(IntcInstancePtr, 0U, /* CPU0 number, not mask. */
			UsbIntrId);
	/*
	 * Connect the device driver handler that will be called when an
	 * interrupt for the device occurs, the handler defined above performs
	 * the specific interrupt processing for the device.
	 */
	Status = XScuGic_Connect(IntcInstancePtr, UsbIntrId,
			(Xil_ExceptionHandler) XUsbPs_IntrHandler, (void *) UsbInstancePtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}
	/* USB0 is an active-high, level-sensitive GIC SPI in the SDT. */
	XScuGic_SetPriorityTriggerType(IntcInstancePtr, UsbIntrId, 0xA0, 0x01);

	/*
	 * Enable the interrupt for the device.
	 */
	XScuGic_Enable(IntcInstancePtr, UsbIntrId);

	/*
	 * Enable interrupts in the Processor.
	 */
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);

	return XST_SUCCESS;
}

static void UsbDisableIntrSystem(XScuGic *IntcInstancePtr, u16 UsbIntrId) {
	/* Disconnect and disable the interrupt for the USB controller. */
	XScuGic_Disconnect(IntcInstancePtr, UsbIntrId);
}

void dump_buffer(u8 *buffer, u32 length) {
	int loop;

	for (loop = 0; loop < length; loop++) {
		xil_printf("%02X", buffer[loop]);
		if (loop < (length - 1)) {
			xil_printf("-");
		} else {
			xil_printf("\n\r");
		}
	}
}

int xusb_COMMS_handle_bulk_request(XUsbPs *usb, u8 endpoint, u8 *buffer,
		u32 buffer_length) {

#ifdef COMMS_DBG
	DEBUG_PRINT("EP%d received %d bytes\r\n ", endpoint, buffer_length);
	dump_buffer(buffer, buffer_length);
#endif
	if (endpoint == 1) {
		// 将usb写入的数据写入缓存
		xusb_Ringbuffer_write(&xusb_rx_buffer, buffer, buffer_length);
	}

	return XST_SUCCESS;
}

int xusb_COMMS_send_data(XUsbPs *usb, u8 *buffer, u32 length) {
	int status;
#ifdef COMMS_DBG
	DEBUG_PRINT("EP%d Sent %d bytes \r\n ", 1, length);
#endif

	status = XUsbPs_EpBufferSend(usb, 1, buffer, length);
	if (status == XST_SUCCESS) {
		return length;
	} else {
		return status;
	}
}

int xusb_COMMS_rx_bytes_available(void) {

	return xusb_Ringbuffer_available_bytes(&xusb_rx_buffer);
}

int xusb_COMMS_receive_data(u8 *buffer, u32 length) {

	return xusb_Ringbuffer_read(&xusb_rx_buffer, buffer, length);
}
