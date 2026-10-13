#include "tmp.h"
#include "../iic/iic.h"


float read_temp(u32 addr)
{
	u8 data[2];
	IIC_polled_read(0, addr, TEMP_DATA_REGISTER, 2, data);
	DEBUG_PRINT("origin data : %d ,%d\r\n",data[0],data[1]);
	u32 temp = (data[0] <<4) | (data[1] >>4);
	DEBUG_PRINT("origin data : %x\r\n",temp);
	return temp*0.0625f;
}
