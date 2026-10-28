#ifndef _AFE5832_SPI_H
#define _AFE5832_SPI_H

#include "afe5832_ctrl_ps.h"
#include "../main.h"

#define AFE_SPI_BASE_ADDR XPAR_XAFE5832_CTRL_PS_0_BASEADDR
#define BASE_ADDRESS AFE_SPI_BASE_ADDR
//reg0:单次写数据
//reg1:bit9-0:单次写地址
// reg1 bit20：启动初始化
//reg1 bit21:写开始
//reg2 bit0: 写完成信号
//reg3:bit9-0:单次读地址
//reg3:bit21:单次启动信号
//reg5:bit0:读完成信号
int afe5832_spi_healthy(void);
u32 afe5832_spi_write_byte(u32 addr, u32 data);
u32 afe5832_spi_read_byte(u32 addr,u32 *data);
#endif
