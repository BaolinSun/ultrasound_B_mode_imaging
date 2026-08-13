#include "gpio.h"

XGpioPs gpiops;

u32 gpio_init(void) {
	XGpioPs_Config *cfg;
	cfg = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_BASEADDR);
	if (cfg == NULL) {
		return XST_FAILURE;
	}
	XGpioPs_CfgInitialize(&gpiops, cfg, cfg->BaseAddr);
	return XST_SUCCESS;
}
