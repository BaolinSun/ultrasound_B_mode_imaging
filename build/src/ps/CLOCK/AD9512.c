#include "AD9512.h"
#include "../GPIO/gpio.h"
#include "sleep.h"
XSpiPs xspips;
// 输入时钟 200MHz
static u32 ad9512_config_table[][2] = { //
		{ 0x0000, 0x10 }, //
//				{ 0x0002, 0x20 }, //
				{ 0x0034, 0x01 }, //
				{ 0x0035, 0x00 }, //
				{ 0x0036, 0x00 }, //
//				{ 0x0037, 0x04 }, //
				{ 0x003D, 0x08 }, //
				{ 0x003E, 0x08 }, //
				{ 0x003F, 0x08 }, //
				{ 0x0040, 0x02 }, //		// 打开OUT3
				{ 0x0041, 0x03 }, //  // 关闭OUT4
				{ 0x0044, 0x13 }, //
				{ 0x0045, 0x05 }, //
				{ 0x004A, 0x33 },	// OUT0 HIGH 1 LOW 1 tx7332
				{ 0x004B, 0x00 },	//
				{ 0x004C, 0x33 },	// OUT1 HIGH 1 LOW 1 tx7332
				{ 0x004D, 0x00 },	//
				{ 0x004E, 0x33 },		// OUT2 HIGH 1 LOW 1 fpga
				{ 0x004F, 0x00 },		//
				{ 0x0050, 0x77 },	// OUT3 HIGH 1 LOW 1 afe5832
				{ 0x0051, 0x00 },	//
				{ 0x0052, 0x55 }, //  OUT4
				{ 0x0053, 0x00 }, //
				{ 0x0058, 0x00 }, //
//				{ 0x0059, 0x00 }, //
				{ 0x005A, 0x01 } };

u32 hw_spi_init(void) {
	XSpiPs_Config *cfg;
	s32 status;
	cfg = XSpiPs_LookupConfig(SPI_BASEADDR);
	if (cfg == NULL) {
		return XST_FAILURE;
	}
	// 配置初始化
	status = XSpiPs_CfgInitialize(&xspips, cfg, cfg->BaseAddress);
	if (status != XST_SUCCESS) {
		return status;
	}

	DEBUG_PRINT("SPI INIT :cfg init success\r\n");

	status = XSpiPs_SelfTest(&xspips);
	if (status != XST_SUCCESS) {
		return status;
	}

	DEBUG_PRINT("SPI INIT :spi self test success\r\n");

	//配置SPI设备为主设备并且手动开始，手动片选
	XSpiPs_SetOptions(&xspips,
			XSPIPS_MANUAL_START_OPTION | XSPIPS_MASTER_OPTION
					| XSPIPS_FORCE_SSELECT_OPTION);
	//设置预分配系数为8，clk时钟频率为150M/8
	XSpiPs_SetClkPrescaler(&xspips, XSPIPS_CLK_PRESCALE_16);
	//创建队列
	//	dataex_Queue = xQueueCreate(10, sizeof(u8 *));
	return XST_SUCCESS;
}
u32 ad9512_init(void) {
	u8 data = 0;
	XGpioPs_SetDirectionPin(&gpiops, AD9512_FUNC_PIN, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AD9512_FUNC_PIN, 1);
	XGpioPs_WritePin(&gpiops, AD9512_FUNC_PIN, 1);
	ad9512_spi_read(0x00, &data);

	DEBUG_PRINT("ad9512 0x00 : 0x%2x\r\n", data);

	ad9512_spi_write(0x52, 0x11);
	ad9512_spi_read(0x52, &data);

	DEBUG_PRINT("ad9512 0x52 : 0x%2x\r\n", data);

	ad9512_spi_write(0x00, 0x30); // 复位
	usleep(100000);
	ad9512_spi_write(0x00, 0x10); // 结束复位
	for (int i = 0; i < 23; i++) {
		ad9512_spi_write(ad9512_config_table[i][0], ad9512_config_table[i][1]);
	}
//	for (int i = 0; i < 23; i++) {
//			ad9512_spi_read(ad9512_config_table[i][0], &data);
//			xil_printf("ad9512 0x%4x : 0x%2x\r\n",ad9512_config_table[i][0], data);
//		}
	return XST_SUCCESS;
}
u32 ad9512_spi_write(u32 addr, u8 data) {
	u8 temp_data[3] = { 0 };
	temp_data[0] = (addr >> 8) & 0xFF;
	temp_data[0] |= (0x00 << 7); // 写
	temp_data[0] |= (0x00 << 5); // 写1个byte
	temp_data[1] = addr & 0xFF;
	temp_data[2] = data;
	XSpiPs_SetSlaveSelect(&xspips, 0x00);
	XSpiPs_PolledTransfer(&xspips, temp_data, NULL, 3);
	XSpiPs_SetSlaveSelect(&xspips, 0x0f);
	return XST_SUCCESS;
}
u32 ad9512_spi_read(u32 addr, u8 *data) {
	u8 temp_data[3] = { 0 };
	temp_data[0] = (addr >> 8) & 0xFF;
	temp_data[0] |= 0x01 << 7;   // 读
	temp_data[0] |= 0x00 << 5;  // 读1个byte
	temp_data[1] = addr & 0xFF;
	temp_data[2] = 0x00;
	XSpiPs_SetSlaveSelect(&xspips, 0x00);
	XSpiPs_PolledTransfer(&xspips, temp_data, temp_data, 3);
	XSpiPs_SetSlaveSelect(&xspips, 0x0f);
	*data = temp_data[2];
	return XST_SUCCESS;
}

// 50 100 200
void tx7332_set_freq(u32 chip_id, u32 freq) {

	switch (freq) {
	case 50: {
		ad9512_spi_write(0x004a, 0x11);
		ad9512_spi_write(0x004b, 0x00);
		ad9512_spi_write(0x004c, 0x11);
		ad9512_spi_write(0x004d, 0x00);
	}
		break;
	case 100: {
		ad9512_spi_write(0x004a, 0x00);
		ad9512_spi_write(0x004b, 0x00);
		ad9512_spi_write(0x004c, 0x00);
		ad9512_spi_write(0x004d, 0x00);
	}
		break;
	case 200: {
		ad9512_spi_write(0x004a, 0x00);
		ad9512_spi_write(0x004b, 0x80);
		ad9512_spi_write(0x004c, 0x00);
		ad9512_spi_write(0x004d, 0x80);
	}
		break;
	default: {

	}
	}
	ad9512_spi_write(0x005a, 0x01);
}
void afe5832_set_freq(u32 chip_id, u32 freq) {
	switch (freq) {
	case 50: {
		ad9512_spi_write(0x0050, 0x11);
		ad9512_spi_write(0x0051, 0x00);

	}
		break;
	case 100: {
		ad9512_spi_write(0x0050, 0x00);
		ad9512_spi_write(0x0051, 0x00);

	}
		break;
	case 200: {
		ad9512_spi_write(0x0050, 0x00);
		ad9512_spi_write(0x0051, 0x80);
	}
		break;
	default: {

	}
	}
	ad9512_spi_write(0x005a, 0x01);
}
