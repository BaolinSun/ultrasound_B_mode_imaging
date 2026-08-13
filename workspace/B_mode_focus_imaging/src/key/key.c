#include "key.h"
#include "xscugic.h"
#include "xinterrupt_wrap.h"
//#include "ps_2_pl_ctrl.h"
extern XScuGic xInterruptController;

#define GPIO_INT_IRQ_ID \
	((u32)(XGet_IntrId(XPAR_XGPIOPS_0_INTERRUPTS) + \
		XGet_IntrOffset(XPAR_XGPIOPS_0_INTERRUPTS)))

void key_init(void) {
	XGpioPs_SetDirectionPin(&gpiops, KEY_0_EMIO_PIN, 0);
	XGpioPs_SetIntrTypePin(&gpiops, KEY_0_EMIO_PIN,
	XGPIOPS_IRQ_TYPE_EDGE_FALLING);
	XGpioPs_IntrEnablePin(&gpiops, KEY_0_EMIO_PIN);
	XGpioPs_SetDirectionPin(&gpiops, KEY_1_EMIO_PIN, 0);
	XGpioPs_SetIntrTypePin(&gpiops, KEY_1_EMIO_PIN,
	XGPIOPS_IRQ_TYPE_EDGE_FALLING);
	XGpioPs_IntrEnablePin(&gpiops, KEY_1_EMIO_PIN);
	XGpioPs_SetDirectionPin(&gpiops, KEY_2_EMIO_PIN, 0);
	XGpioPs_SetIntrTypePin(&gpiops, KEY_2_EMIO_PIN,
	XGPIOPS_IRQ_TYPE_EDGE_FALLING);
	XGpioPs_IntrEnablePin(&gpiops, KEY_2_EMIO_PIN);
	key_intr_setup();

}

void key_intr_handler(void *CallBackRef, u32 Bank, u32 Status) {
	u32 keydata = 0;
	u32 key_read = 0;
	XGpioPs *gpio = (XGpioPs*) CallBackRef;
	key_read = XGpioPs_ReadPin(gpio, KEY_0_EMIO_PIN);
	keydata |= (key_read == 0x00 ? 0x01 : 0x00);
	key_read = XGpioPs_ReadPin(gpio, KEY_1_EMIO_PIN);
	keydata |= (key_read == 0x00 ? 0x02 : 0x00);
	key_read = XGpioPs_ReadPin(gpio, KEY_2_EMIO_PIN);
	keydata |= (key_read == 0x00 ? 0x04 : 0x00);

	DEBUG_PRINT("key intr %4x\r\n", keydata);
}

u32 key_intr_setup(void) {
	XScuGic_InterruptUnmapFromCpu(&xInterruptController, XSCUGIC_SPI_CPU0_MASK,
	GPIO_INT_IRQ_ID);
	XScuGic_InterruptMaptoCpu(&xInterruptController, XSCUGIC_SPI_CPU1_MASK,
	GPIO_INT_IRQ_ID);
	XScuGic_Connect(&xInterruptController,
			GPIO_INT_IRQ_ID,
			(Xil_ExceptionHandler) XGpioPs_IntrHandler, (void *) &gpiops);
	XGpioPs_SetCallbackHandler(&gpiops, (void *) &gpiops, key_intr_handler);

	XScuGic_Enable(&xInterruptController,
			GPIO_INT_IRQ_ID);
	return XST_SUCCESS;
}
