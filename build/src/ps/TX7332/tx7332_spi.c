//#include "tx7332_spi.h"
//
//u32 tx7332_spi_write_data(u32 addr, u32 data, u32 base_addr) {
//	u32 data_temp = 0;
//	// 首先清除标志位
//	data_temp = data_temp | (addr & 0x3ff) | (0x01 << 10);
//	// 写数据
//	TX7332_PS_CTRL_mWriteReg(base_addr, TX7332_PS_CTRL_S00_AXI_SLV_REG0_OFFSET,
//			data);
//	// 写地址，同时写启动位
//	TX7332_PS_CTRL_mWriteReg(base_addr, TX7332_PS_CTRL_S00_AXI_SLV_REG1_OFFSET,
//			data_temp);
//	// 判断是否写完
//	while ((TX7332_PS_CTRL_mReadReg(base_addr,
//			TX7332_PS_CTRL_S00_AXI_SLV_REG1_OFFSET) & 0x00000800) == 0x00) {
//	}
//	return XST_SUCCESS;
//}
////u32 tx7332_spi_read_data(u32 addr, u32 base_addr) {
////	u32 data_2 = 0;
////	data_2 = single_tx7332_spi_read_data(1,addr, base_addr);
////	return (data_2);
////}
//u32 tx7332_spi_read_data(u32 addr, u32 base_addr) {
//	u32 data_temp = 0;
//	u32 read_data = 0;
//
//	data_temp = data_temp | (addr & 0x3ff) | (0x01 << 21) ;
//	// 写读取地址，同时写启动位
//	TX7332_PS_CTRL_mWriteReg(base_addr, TX7332_PS_CTRL_S00_AXI_SLV_REG3_OFFSET,
//			data_temp);
//	// 判断是否读完
//	while ((TX7332_PS_CTRL_mReadReg(base_addr,
//			TX7332_PS_CTRL_S00_AXI_SLV_REG3_OFFSET) & 0x00400000) == 0x00) {
//	}
//	read_data = TX7332_PS_CTRL_mReadReg(base_addr,
//			TX7332_PS_CTRL_S00_AXI_SLV_REG4_OFFSET);
//	return read_data;
//}
