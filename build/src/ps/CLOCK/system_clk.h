#ifndef __SYSTEM_CLK_H
#define __SYSTEM_CLK_H



#include "../iic/iic.h"
#include "xparameters.h"
#include "AD9512.h"


#define CLK_61E2_IIC IIC_DEVICE_1
#define CLK_61E2_IIC_ADDR 0x58
#define CLK_61E2_OE_PIN 27

u32 clk_61e2_init(void);

u32 sys_clk_init(void);

#endif
