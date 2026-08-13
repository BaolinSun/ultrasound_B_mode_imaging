#include "hv.h"
#include "../GPIO/gpio.h"
#include "../main.h"
u32 hv_init(void) {
	// …Ë÷√Œ™ ‰≥ˆ

	hv_set_volt(50);
	XGpioPs_SetDirectionPin(&gpiops, HV_EN_PIN, 1);
	XGpioPs_WritePin(&gpiops, HV_EN_PIN, 0);
	XGpioPs_SetOutputEnablePin(&gpiops, HV_EN_PIN, 1);
	return XST_SUCCESS;
}
void hv_enable(void) {
	XGpioPs_WritePin(&gpiops, HV_EN_PIN, 1);
}
void hv_disable(void) {
	XGpioPs_WritePin(&gpiops, HV_EN_PIN, 0);
}
void hv_set_volt(float volt) {
	float r = 0.0f;
	r = volt / RATIO - 1.0f;
	r = HV_HR / r;
	r = r - HV_LR;
	// 100 10V
	// 110 15V
	// 115 18V
	// 120  25V
	// 123 32V
	u8 data[2] = { 0x00, 124 };
	IIC_polled_write(HV_TPL0401B_IIC, HV_TPL0401B_IIC_ADDR, 2, data);
	IIC_polled_read(HV_TPL0401B_IIC, HV_TPL0401B_IIC_ADDR, 0x00, 1, data);

	DEBUG_PRINT("HV : TPL0401B read %4x\r\n ",data[0]);
}
