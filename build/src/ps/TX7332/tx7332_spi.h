//#ifndef _TX7332_SPI_H
//#define _TX7332_SPI_H
//
//#include "tx7332_ps_ctrl.h"
//
//#include "../main.h"
//#define TX7332_0_BASE_ADDRESS 0x43c20000
//#define TX7332_1_BASE_ADDRESS 0x43c30000
//#define USE_TX7332 0
//
//// 仅测试使用
//#if (USE_TX7332 == 0)
//#define TX7332_BASE_ADDRESS TX7332_0_BASE_ADDRESS
//#elif
//
//#define TX7332_BASE_ADDRESS TX7332_1_BASE_ADDRESS
//#endif
////reg0:单次写数据
////reg1:bit9-0:单次写地址
//// reg1 bit20：启动初始化
////reg1 bit21:写开始
////reg2 bit0: 写完成信号
////reg3:bit9-0:单次读地址
////reg3:bit21:单次启动信号
////reg5:bit0:读完成信号
//u32 tx7332_spi_write_data(u32 addr, u32 data, u32 base_addr);
////u32 single_tx7332_spi_read_data(u32 die_num,u32 addr, u32 base_addr);
//u32 tx7332_spi_read_data(u32 addr, u32 base_addr);
//
//#endif
