#include "afe5832_spi.h"

u32 afe5832_spi_write_byte(u32 addr, u32 data) {
	u32 data_temp = 0;
	// 首先清除标志位
	data_temp = data_temp | (addr & 0x0ff);
	data_temp = data_temp | (0x01 << 10);
	// 写数据
	AFE5832_CTRL_PS_mWriteReg(BASE_ADDRESS,
			AFE5832_CTRL_PS_S00_AXI_SLV_REG0_OFFSET, data);
	// 写地址，同时写启动位
	AFE5832_CTRL_PS_mWriteReg(BASE_ADDRESS,
			AFE5832_CTRL_PS_S00_AXI_SLV_REG1_OFFSET, data_temp);
	// 判断是否写完
	while ((AFE5832_CTRL_PS_mReadReg(BASE_ADDRESS,
			AFE5832_CTRL_PS_S00_AXI_SLV_REG1_OFFSET) & 0x00000800) == 0x00) {
		;
	}
	return XST_SUCCESS;
}

u32 afe5832_spi_read_byte(u32 addr, u32* data) {

	u32 data_temp = 0;
	u32 read_data = 0;
	// 首先设置允许写
//	afe5832_spi_write_byte(0x0000, 0x0002);

	data_temp = data_temp | (addr & 0x0ff);
	data_temp = data_temp | (0x01 << 21);
	// 写读取地址
	AFE5832_CTRL_PS_mWriteReg(BASE_ADDRESS,
			AFE5832_CTRL_PS_S00_AXI_SLV_REG3_OFFSET, data_temp);

	// 判断是否读完
	while ((AFE5832_CTRL_PS_mReadReg(BASE_ADDRESS,
			AFE5832_CTRL_PS_S00_AXI_SLV_REG3_OFFSET) & 0x00400000) == 0x00) {
//		xil_printf("wait for read finish\r\n");
		;
	}
	read_data = AFE5832_CTRL_PS_mReadReg(BASE_ADDRESS,
			AFE5832_CTRL_PS_S00_AXI_SLV_REG4_OFFSET);

	*data = read_data;
	// 关闭允许写
	afe5832_spi_write_byte(0x0000, 0x0000);
	return read_data;
}
