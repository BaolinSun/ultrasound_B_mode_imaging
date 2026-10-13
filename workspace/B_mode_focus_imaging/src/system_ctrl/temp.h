#ifndef __TEMP_H
#define __TEMP_H

#include "../main.h"
#include "../iic/iic.h"
#define POWER_TEMP_ADDR 0x48
#define AFE_TEMP_ADDR 0x49


#define TEMP_DATA_REGISTER 	0x00
#define CONF_REGISTER 0x01
#define TH_REGISTER 0x02
#define TL_REGISTER 0x03

u32 temp_init();
float read_temp(u32 addr);


#endif
