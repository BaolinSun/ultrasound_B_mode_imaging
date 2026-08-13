//#include "tx7332.h"
//
//#include "../data_transfer/axidma.h"
//#include "../GPIO/gpio.h"
//#include "ps_2_pl_ctrl.h"
//#include "../main.h"
//#include "../uart/uart.h"
//#include "../usb/usb.h"
//#include "../data_transfer/data_recv.h"
//#include "../linux_wifi/linux_wifi.h"
//
//XTime begin_time;
//XTime end_time;
//
//u32 reg_addr[17] = { 0x01, 0x06, 0x0B, 0x0C, 0x0F, 0x14, 0x15, 0x16, 0x17, 0x18,
//		0x19, 0x1a, 0x1b, 0x1e, 0x1f, 0x120, 0x121 };
//
//#ifdef USE_0_2_PITCH
//static u16 tx7332_0_delay_profile[PLANE_WAVE_ANGLE_NUMS][32] =
//{
//		{162,160,158,155,153,151,149,146,144,142,140,137,135,133,131,128,126,124,121,119,117,115,112,110,108,106,103,101,99,97,94,92},
//{134,132,130,128,127,125,123,121,119,118,116,114,112,110,109,107,105,103,101,100,98,96,94,92,90,89,87,85,83,81,80,78},
//{106,104,103,101,100,99,97,96,95,93,92,91,89,88,87,85,84,82,81,80,78,77,76,74,73,72,70,69,68,66,65,63},
//{77,76,75,74,73,73,72,71,70,69,68,67,66,65,64,63,63,62,61,60,59,58,57,56,55,54,54,53,52,51,50,49},
//{49,48,48,47,47,46,46,45,45,44,44,44,43,43,42,42,41,41,40,40,39,39,39,38,38,37,37,36,36,35,35,35},
//{20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20},
//{20,20,21,21,22,22,23,23,24,24,25,25,25,26,26,27,27,28,28,29,29,30,30,30,31,31,32,32,33,33,34,34},
//{20,21,22,23,24,25,25,26,27,28,29,30,31,32,33,34,34,35,36,37,38,39,40,41,42,43,44,44,45,46,47,48},
//{20,21,23,24,25,27,28,30,31,32,34,35,36,38,39,40,42,43,44,46,47,49,50,51,53,54,55,57,58,59,61,62},
//{20,22,24,25,27,29,31,33,34,36,38,40,42,43,45,47,49,51,53,54,56,58,60,62,63,65,67,69,71,72,74,76},
//{20,22,25,27,29,31,34,36,38,40,43,45,47,49,52,54,56,58,61,63,65,67,70,72,74,76,79,81,83,85,88,90}
//};
//static u16 tx7332_1_delay_profile[PLANE_WAVE_ANGLE_NUMS][32] =
//{{90,88,85,83,81,79,76,74,72,70,67,65,63,61,58,56,54,52,49,47,45,43,40,38,36,34,31,29,27,25,22,20},
//{76,74,72,71,69,67,65,63,62,60,58,56,54,53,51,49,47,45,43,42,40,38,36,34,33,31,29,27,25,24,22,20},
//{62,61,59,58,57,55,54,53,51,50,49,47,46,44,43,42,40,39,38,36,35,34,32,31,30,28,27,25,24,23,21,20},
//{48,47,46,45,44,44,43,42,41,40,39,38,37,36,35,34,34,33,32,31,30,29,28,27,26,25,25,24,23,22,21,20},
//{34,34,33,33,32,32,31,31,30,30,30,29,29,28,28,27,27,26,26,25,25,25,24,24,23,23,22,22,21,21,20,20},
//{20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20},
//{35,35,35,36,36,37,37,38,38,39,39,39,40,40,41,41,42,42,43,43,44,44,44,45,45,46,46,47,47,48,48,49},
//{49,50,51,52,53,54,54,55,56,57,58,59,60,61,62,63,63,64,65,66,67,68,69,70,71,72,73,73,74,75,76,77},
//{63,65,66,68,69,70,72,73,74,76,77,78,80,81,82,84,85,87,88,89,91,92,93,95,96,97,99,100,101,103,104,106},
//{78,80,81,83,85,87,89,90,92,94,96,98,100,101,103,105,107,109,110,112,114,116,118,119,121,123,125,127,128,130,132,134},
//{92,94,97,99,101,103,106,108,110,112,115,117,119,121,124,126,128,131,133,135,137,140,142,144,146,149,151,153,155,158,160,162}
//};
//
//#endif
//
//#ifdef USE_0_4_PITCH
//// 0.4pitch
// static u16 tx7332_0_delay_profile[PLANE_WAVE_ANGLE_NUMS][32] =
// {
////	{360,355,349,344,339,333,328,322,317,312,306,301,295,290,285,279,274,268,263,258,252,247,241,236,231,225,220,214,209,204,198,193},
//	{304,300,295,291,286,282,277,273,268,264,259,255,250,246,241,236,232,227,223,218,214,209,205,200,196,191,187,182,178,173,169,164},
//	{248,244,241,237,233,230,226,222,219,215,212,208,204,201,197,194,190,186,183,179,175,172,168,165,161,157,154,150,147,143,139,136},
//	{191,188,186,183,180,177,175,172,169,167,164,161,158,156,153,150,148,145,142,139,137,134,131,129,126,123,120,118,115,112,110,107},
//	{134,132,131,129,127,125,123,121,120,118,116,114,112,111,109,107,105,103,102,100,98,96,94,92,91,89,87,85,83,82,80,78},
//	{77,76,75,74,73,73,72,71,70,69,68,67,66,65,64,64,63,62,61,60,59,58,57,56,55,54,54,53,52,51,50,49},
//	{20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20},
//	{20,21,22,23,24,25,25,26,27,28,29,30,31,32,33,34,35,35,36,37,38,39,40,41,42,43,44,44,45,46,47,48},
//	{20,22,24,25,27,29,31,33,34,36,38,40,42,44,45,47,49,51,53,54,56,58,60,62,63,65,67,69,71,73,74,76},
//	{20,23,25,28,31,34,36,39,42,44,47,50,53,55,58,61,63,66,69,72,74,77,80,82,85,88,91,93,96,99,101,104},
//	{20,24,27,31,34,38,42,45,49,53,56,60,63,67,71,74,78,81,85,89,92,96,100,103,107,110,114,118,121,125,128,132},
//	{20,25,29,34,38,43,47,52,56,61,65,70,74,79,83,88,92,97,101,106,110,115,119,124,128,133,137,142,146,151,155,160}
////	{20,25,31,36,42,47,52,58,63,69,74,79,85,90,96,101,106,112,117,123,128,133,139,144,150,155,160,166,171,177,182,187}
// };
// static u16 tx7332_1_delay_profile[PLANE_WAVE_ANGLE_NUMS][32] =
// {
////	{187,182,177,171,166,160,155,150,144,139,133,128,123,117,112,106,101,96,90,85,79,74,69,63,58,52,47,42,36,31,25,20},
//	{160,155,151,146,142,137,133,128,124,119,115,110,106,101,97,92,88,83,79,74,70,65,61,56,52,47,43,38,34,29,25,20},
//	{132,128,125,121,118,114,110,107,103,100,96,92,89,85,81,78,74,71,67,63,60,56,53,49,45,42,38,34,31,27,24,20},
//	{104,101,99,96,93,91,88,85,82,80,77,74,72,69,66,63,61,58,55,53,50,47,44,42,39,36,34,31,28,25,23,20},
//	{76,74,73,71,69,67,65,63,62,60,58,56,54,53,51,49,47,45,44,42,40,38,36,34,33,31,29,27,25,24,22,20},
//	{48,47,46,45,44,44,43,42,41,40,39,38,37,36,35,35,34,33,32,31,30,29,28,27,26,25,25,24,23,22,21,20},
//	{20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20},
//	{49,50,51,52,53,54,54,55,56,57,58,59,60,61,62,63,64,64,65,66,67,68,69,70,71,72,73,73,74,75,76,77},
//	{78,80,82,83,85,87,89,91,92,94,96,98,100,102,103,105,107,109,111,112,114,116,118,120,121,123,125,127,129,131,132,134},
//	{107,110,112,115,118,120,123,126,129,131,134,137,139,142,145,148,150,153,156,158,161,164,167,169,172,175,177,180,183,186,188,191},
//	{136,139,143,147,150,154,157,161,165,168,172,175,179,183,186,190,194,197,201,204,208,212,215,219,222,226,230,233,237,241,244,248},
//	{164,169,173,178,182,187,191,196,200,205,209,214,218,223,227,232,236,241,246,250,255,259,264,268,273,277,282,286,291,295,300,304}
////	{193,198,204,209,214,220,225,231,236,241,247,252,258,263,268,274,279,285,290,295,301,306,312,317,322,328,333,339,344,349,355,360}
// };
//
//#endif
//u32 delay_channel_to_reg[32] = { 32, 30, 28, 26, 24, 22, 20, 18, 31, 29, 27, 25,
//		23, 21, 19, 17, 16, 14, 12, 10, 8, 6, 4, 2, 15, 13, 11, 9, 7, 5, 3, 1 };
//
//u32 channel_to_reg[32] = { 16, 24, 17, 25, 18, 26, 19, 27, 20, 28, 21, 29, 22,
//		30, 23, 31, 0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15 };
//
//u32 reg_data[32] = { 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x8c0002,
//		0x00, 0x00, 0x00, 0x00, 0x00, 0x800078, 0x700068, 0x600058, 0x500048,
//		0x7c0074, 0x6c0064 };
//
//u32 tx7332_io_init(void) {
//	//#define TX7332_DSEL_0_PIN 72		//配置选择引脚0，输出
//	XGpioPs_SetDirectionPin(&gpiops, TX7332_DSEL_0_PIN, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, TX7332_DSEL_0_PIN, 1);
//	XGpioPs_WritePin(&gpiops, TX7332_DSEL_0_PIN, 1);
//	//#define TX7332_DSEL_1_PIN 73		//配置选择引脚1，输出
//	XGpioPs_SetDirectionPin(&gpiops, TX7332_DSEL_1_PIN, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, TX7332_DSEL_1_PIN, 1);
//	XGpioPs_WritePin(&gpiops, TX7332_DSEL_1_PIN, 1);
//	//#define TX7332_STDY_PIN 70		//等待引脚 高电平有效，输出
//	XGpioPs_SetDirectionPin(&gpiops, TX7332_STDY_PIN, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, TX7332_STDY_PIN, 1);
//	XGpioPs_WritePin(&gpiops, TX7332_STDY_PIN, 0);
//	//#define TX7332_CW_EN_PIN 71		//CW模式使能引脚，输出
//	XGpioPs_SetDirectionPin(&gpiops, TX7332_CW_EN_PIN, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, TX7332_CW_EN_PIN, 1);
//	XGpioPs_WritePin(&gpiops, TX7332_CW_EN_PIN, 0);
//	//#define TX7332_SHUTZ_PIN 74		//过热关闭信号，低电平有效，输入
//	XGpioPs_SetDirectionPin(&gpiops, TX7332_SHUTZ_PIN, 0);
//	//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_0_TR_EN1, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_0_TR_EN1, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN1, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_0_TR_EN2, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_0_TR_EN2, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN2, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_0_TR_EN3, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_0_TR_EN3, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN3, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_0_TR_EN4, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_0_TR_EN4, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN4, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_1_TR_EN1, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_1_TR_EN1, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_1_TR_EN1, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_1_TR_EN2, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_1_TR_EN2, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_1_TR_EN2, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_1_TR_EN3, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_1_TR_EN3, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_1_TR_EN3, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, EMIO_TX7332_1_TR_EN4, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, EMIO_TX7332_1_TR_EN4, 1);
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_1_TR_EN4, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, TX7332_RESET_PIN, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, TX7332_RESET_PIN, 1);
//	XGpioPs_WritePin(&gpiops, TX7332_RESET_PIN, 0);
//	return XST_SUCCESS;
//}
//
//u32 tx7332_init(void) {
//	tx7332_io_init();
//	XGpioPs_WritePin(&gpiops, TX7332_RESET_PIN, 1);
//	usleep(20);
//	XGpioPs_WritePin(&gpiops, TX7332_RESET_PIN, 0);
//	usleep(1500);
//	// tx7332 芯片级初始化配置
//	tx7332_reg_config(TX7332_0_BASE_ADDRESS);
//	tx7332_reg_config(TX7332_1_BASE_ADDRESS);
//
//	return XST_SUCCESS;
//}
//
//void TX7332_task(void *pvParameters) {
//	int sig = 0;
//	uint32_t read_data = 0;
//	u32 pre_buffer_cnt = 50;
//	u32 now_buffer_cnt = 0;
//	u32 cnt = 0;
//
//	DEBUG_PRINT("TX7332_0 1 ready\r\n");
//	while (1) {
//		// 第一次按键启动
//
////		DEBUG_PRINT("TX7332 :  sig %d\r\n", sig);
//		// debug 程序暂停
//		while (system_run_state.pulse_ctrl == SYSTEM_PULSE_GEN_DISABLE)
//			;
//		now_buffer_cnt = (pre_buffer_cnt == 50) ? 0 : 50;
//		// 启动脉冲
////		tx7332_start_pulse(0, TX7332_0_PULSE_BIT);
////		XTime_GetTime(&begin_time);
////		data_recv(MEM_BASE_ADDR + BUFFER_LENGTH_HEX * now_buffer_cnt, 4096, 50);
////		XTime_GetTime(&end_time);
////		get_run_time(&begin_time, &end_time);
////		for (int i = 0; i <8; i++) {
////			for (int j = 0; j < 1024*14 ; j++) {
////				xil_printf("%d", read_rx_buffer_ptr[i][j]);
////				if ((j + 1) % 32 == 0) {
////					xil_printf("\r\n");
////				} else {
////					xil_printf(",");
////				}
////			}
////		}
//		send_thur_wifi(pre_buffer_cnt, now_buffer_cnt, 1024, 50,
//				rx_buffer_ptr[0]);
//		pre_buffer_cnt = now_buffer_cnt;
////		xTaskNotify(usb_taskhandle, 0x01, eSetValueWithOverwrite);
//		XGpioPs_WritePin(&gpiops, LED_2_EMIO_PIN, cnt);
//		cnt = cnt == 0 ? 1 : 0;
//		//等待脉冲完全产生
////		vTaskDelay(5 / portTICK_PERIOD_MS);
//		read_data = tx7332_overTemp();
////		DEBUG_PRINT("TEMP : %d\r\n", read_data);
//		if (!read_data) {
//			DEBUG_PRINT("TEMP ALERT!!!!!!!! ");
//			sig = 0;
////			break;
//		}
//	}
//}
////tx7332初始化配置
//void tx7332_reg_config(u32 base_addr) {
//	uint32_t cfg = 0;
//	uint32_t mdta = 0;
//	tx7332_spi_write_data(REGISTER_0H, 0x00000001, base_addr);
//	usleep(10000);
//	//配置驱动能力为0.3A
//	XGpioPs_WritePin(&gpiops, TX7332_DSEL_0_PIN, 1);
//	XGpioPs_WritePin(&gpiops, TX7332_DSEL_1_PIN, 0);
//	//写17号寄存器，驱动设置驱动能力由DSEL_0,DSEL_1两个引脚控制。
//	tx7332_spi_write_data(REGISTER_17H, cfg | (0x00 << 2) | 0x01 << 4,
//			base_addr);
//	//使用默认电源模式
//	tx7332_set_PowMode(DEFAULT_POWER_MODE, base_addr);
//	u32 tx_start_delay = 10;
//	// 26-18位：TX_START_DEL 设为10  0x02<<16 :关闭CW mode 	0x01 :on-chip
//	mdta = (tx_start_delay << 18) | (0x03 << 16) | (0x00 << 3) | 0x03;
//	tx7332_spi_write_data(REGISTER_18H, mdta, base_addr);
//	mdta = tx7332_spi_read_data(REGISTER_18H, base_addr);
//	DEBUG_PRINT("TX7332 READ %x\r\n", mdta);
//
//	tx7332_set_tail_cnt(6, base_addr);
//	//设置输出模式
//	tx7332_set_channel_pattern_mode(base_addr);
//	//选择输出模式配置0
//	tx7332_select_pattern_profile(0, base_addr);
//	//设置延时
//	tx7332_set_channel_delay(0, base_addr);
//	tx7332_select_delay_profile(0, base_addr);
////	tx7332_spi_write_data(REGISTER_0H, 0x01 << 3, base_addr);
//	// 2us - 1.75us
//	tx7332_set_TR_SW_OFF_DEL(4, base_addr);
//
//	//选择配置文件1
//
//	tx7332_set_repeat_cnt(0, base_addr);
//	tx7332_set_TR_SW_DEL(5, base_addr);
//	tx7332_channel_enable(0, 31, 1, base_addr);
//	tx7332_TR_SW_ENable(1, base_addr);
////
////	for (int j = 0; j < 17; j++) {
////		u32 data = tx7332_spi_read_data(reg_addr[j], base_addr);
////		DEBUG_PRINT("ADDR %3x  :%8x\r\n", reg_addr[j], data);
////	}
////	for (int j = 0; j < 10; j++) {
////		u32 data = tx7332_spi_read_data(0x20 + j, base_addr);
////		DEBUG_PRINT("ADDR %3x  :%8x\r\n", 0x20 + j, data);
////	}
//	tx7332_spi_write_data(REGISTER_0H, 0x00000000, base_addr);
//}
////产生脉冲信号
//// powerMode : 1:动态电源模式
////						  0:默认电源模式
//
//// reg3 bit 0:chip 1
//// reg3 bit 4: chip 2
//void tx7332_start_pulse(u8 powerMode, u32 chip_sel) {
//	if (powerMode == 1) {
//		XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN1, 1);
//		XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN3, 1);
//		usleep(10);
//	}
//	// 启动脉冲
//	PS_2_PL_CTRL_mWriteReg(PS_2_PL_CTRL_BASEADDR,
//			PS_2_PL_CTRL_S00_AXI_SLV_REG3_OFFSET, chip_sel);
//	delay_spi_nop();
//	PS_2_PL_CTRL_mWriteReg(PS_2_PL_CTRL_BASEADDR,
//			PS_2_PL_CTRL_S00_AXI_SLV_REG3_OFFSET, 0x00000000);
//	if (powerMode == 1) {
//		XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN1, 0);
//		XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN3, 0);
//		usleep(10);
//	}
//}
//
////动态电源模式控制，0打开，1关闭
//void tx7332_set_PowMode(u32 power_mode, u32 base_addr) {
//	u32 reg = 0;
//	if (power_mode == DEFAULT_POWER_MODE) {
//		tx7332_spi_write_data(REGISTER_6H, 0x0001 << 4, base_addr);
//		tx7332_spi_write_data(REGISTER_0FH, 0x0001 << 4, base_addr);
//		tx7332_spi_write_data(REGISTER_14H, 0x00000000 << 22, base_addr);
//		tx7332_spi_write_data(REGISTER_0BH, 0x00000000 << 22, base_addr);
//	} else if (power_mode == DYNAMIC_POWER_MODE) {
//		tx7332_spi_write_data(REGISTER_6H, 0 << 4, base_addr);
//		tx7332_spi_write_data(REGISTER_0FH, 0 << 4, base_addr);
//		tx7332_spi_write_data(REGISTER_14H, 0x0000000F << 22, base_addr);
//		tx7332_spi_write_data(REGISTER_0BH, 0x0000000F << 22, base_addr);
//	} else {
//		return;
//	}
//	reg = tx7332_spi_read_data(REGISTER_6H, base_addr);
//
//	DEBUG_PRINT("Read 0X06 : %x\r\n", reg);
//
//	reg = tx7332_spi_read_data(REGISTER_0FH, base_addr);
//
//	DEBUG_PRINT("Read 0X0F : %x\r\n", reg);
//
//}
//
////1 :ON-CHIP   2:OFF-CHIP
//void tx7332_set_work_mode(u32 mode, u32 base_addr) {
//	u32 read_data = 0;
//
//	if (mode == 1) {
//		tx7332_spi_write_data(REGISTER_18H, read_data | 0x00000001, base_addr);
//	} else {
//		tx7332_spi_write_data(REGISTER_18H, read_data & 0xFFFFFFFE, base_addr);
//	}
//	read_data = tx7332_spi_read_data(REGISTER_18H, base_addr);
//	DEBUG_PRINT("set work mode read %x\r\n", read_data);
//}
//
////等待模式设置  0 ：退出等待模式
////						 1：进入等待模式
//void tx7332_Enter_standBy(u32 mode) {
//	XGpioPs_WritePin(&gpiops, TX7332_STDY_PIN, mode);
//}
//
////判断7332是否进入过热模式
//u32 tx7332_overTemp(void) {
//	u32 data = 0;
//	data = XGpioPs_ReadPin(&gpiops, TX7332_SHUTZ_PIN);
//	return data;
//}
//
////设置重复次数
//void tx7332_set_repeat_cnt(u32 cnt, u32 base_addr) {
//	u32 read_data = 0;
//	read_data = tx7332_spi_read_data(REGISTER_19H, base_addr);
//	read_data &= 0xFFFFFFC1;
//	tx7332_spi_write_data(REGISTER_19H, read_data | (cnt << 1), base_addr);
//}
//
////选择模式配置文件，默认为配置文件0
//void tx7332_select_pattern_profile(u32 m_profile_num, u32 base_addr) {
//	if (m_profile_num < 16 && m_profile_num >= 0) {
//		tx7332_spi_write_data(REGISTER_1EH, m_profile_num, base_addr);
//		tx7332_spi_write_data(REGISTER_1FH, m_profile_num, base_addr);
//	} else {
//		tx7332_spi_write_data(REGISTER_1EH, 0, base_addr);
//		tx7332_spi_write_data(REGISTER_1FH, 0, base_addr);
//	}
//}
//
////设置输出模式即波形样式
//
////每个通道可以设置输出波形的电平和持续时间
////LVL_x ： 输出电平，011为0，x10为高电平 x01为负电平 111为终止    x为输出顺序，
////PER_x:	持续时间 ，0：2个BF_CLK周期，2：4个周期
//void tx7332_set_channel_pattern_mode(u32 base_addr) {
//// 7MHz 1个周期
////	tx7332_spi_write_data(0x120, 0x003F2922, base_addr);
////	tx7332_spi_write_data(0x121, 0x0007512A, base_addr);
////	tx7332_spi_write_data(0x122, 0x00000000, base_addr);
////	tx7332_spi_write_data(0x123, 0x00000000, base_addr);
//	// 7MHz 2个周期
//	tx7332_spi_write_data(0x120, 0x21272122, base_addr);
//	tx7332_spi_write_data(0x121, 0x292A2927, base_addr);
//	tx7332_spi_write_data(0x122, 0x29222927, base_addr);
//	tx7332_spi_write_data(0x123, 0x00000000, base_addr);
//	tx7332_spi_write_data(0x124, 0x292A292A, base_addr);
//	tx7332_spi_write_data(0x125, 0x292A292A, base_addr);
//	tx7332_spi_write_data(0x126, 0x29222927, base_addr);
//	tx7332_spi_write_data(0x127, 0x00000000, base_addr);
//	// 2.5MHZ
////	tx7332_spi_write_data(0x120, 0x21278182, base_addr);
////	tx7332_spi_write_data(0x121, 0x292A2927, base_addr);
////	tx7332_spi_write_data(0x122, 0x29222927, base_addr);
////	tx7332_spi_write_data(0x123, 0x00000000, base_addr);
////	tx7332_spi_write_data(0x124, 0x292A292A, base_addr);
////	tx7332_spi_write_data(0x125, 0x292A292A, base_addr);
////	tx7332_spi_write_data(0x126, 0x29222927, base_addr);
////	tx7332_spi_write_data(0x127, 0x00000000, base_addr);
//
//}
//
////设置通道波形产生的延迟时间
////m_profile:0-15
//void tx7332_set_channel_delay(u32 m_profile, u32 base_addr) {
//	static u32 base_address[PLANE_WAVE_ANGLE_NUMS] = { 0x20, 0x30, 0x40, 0x50,
//			0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0 };
////	,0xd0,0xe0
//
//	u32 reg = 0;
//	if (base_addr == TX7332_0_BASE_ADDRESS) {
//		for (u8 t = 0; t < PLANE_WAVE_ANGLE_NUMS; t++) {
//			for (u32 cnt = 0; cnt <= 30; cnt += 2) {
//				//地址换算
//				tx7332_spi_write_data(base_address[t] + cnt / 2,
//						0x0000
//								| ((tx7332_0_delay_profile[t][delay_channel_to_reg[cnt]
//										- 1]) << 16)
//								| (tx7332_0_delay_profile[t][delay_channel_to_reg[cnt
//										+ 1] - 1] ), base_addr);
//			}
//		}
//	} else {
//		for (u8 t = 0; t < PLANE_WAVE_ANGLE_NUMS; t++) {
//			for (u32 cnt = 0; cnt <= 30; cnt += 2) {
//				//地址换算
//				tx7332_spi_write_data(base_address[t] + cnt / 2,
//						0x0000
//								| ((tx7332_1_delay_profile[t][delay_channel_to_reg[cnt]
//										- 1] ) << 16)
//								| (tx7332_1_delay_profile[t][delay_channel_to_reg[cnt
//										+ 1] - 1] ), base_addr);
//			}
//		}
//	}
////	reg = tx7332_spi_read_data(base_address + m_profile * 0x10 + 0, base_addr);
//
////	DEBUG_PRINT("reg 0x20 : %x\r\n", reg);
//}
//
//void tx7332_select_delay_profile(u32 m_profile, u32 base_addr) {
//	if (m_profile >= 0 && m_profile < 16) {
//		u32 read_data = 0;
//		read_data = tx7332_spi_read_data(REGISTER_16H, base_addr);
//		read_data &= 0x0FFF0FFF;
//		tx7332_spi_write_data(REGISTER_16H,
//				read_data | (m_profile << 28 | m_profile << 12), base_addr);
//	}
//}
//
//void tx7332_enable_CW_mode(u32 base_addr) {
//	u32 read_data = 0;
//	read_data = tx7332_spi_read_data(REGISTER_18H, base_addr);
//	//使能CW_mode并将CW_mode由pin控制
//	read_data = read_data & 0xFFFC7FFF;
//	read_data = read_data | (0x03 << 13);
//
//	tx7332_spi_write_data(REGISTER_18H, read_data, base_addr);
//	XGpioPs_WritePin(&gpiops, TX7332_CW_EN_PIN, 1);
//}
////1-31 有TAIL COUNT 即末尾是平的
//// 0 没有COUNT 缓慢回复到零电平
//void tx7332_set_tail_cnt(int cnt, u32 base_addr) {
//	u32 reg = 0;
//	reg = tx7332_spi_read_data(REGISTER_19H, base_addr);
//	reg &= ~(0x0000001F << 6);
//	reg |= (cnt << 6);
//	tx7332_spi_write_data(REGISTER_19H, reg, base_addr);
//}
//// tx7332通道使能
//// channel_start :
//// enable :0 :关闭 1：使能
//void tx7332_channel_enable(int channel_start, int channel_end, int enable,
//		u32 base_addr) {
//
//	u32 reg = 0;
//	int i = 0;
//	reg = tx7332_spi_read_data(REGISTER_1BH, base_addr);
//	if (enable) {
//		for (i = channel_start; i <= channel_end; i++) {
//			reg = reg | (0x00 << channel_to_reg[i - 1]);
//		}
//	} else {
//		for (i = channel_start; i <= channel_end; i++) {
//			reg = reg | (0x01 << channel_to_reg[i - 1]);
//		}
//	}
//	tx7332_spi_write_data(REGISTER_1BH, reg, base_addr);
//}
//void tx7332_set_TR_SW_DEL(uint32_t cnt, u32 base_addr) {
//	u32 reg = 0;
//	tx7332_spi_write_data(REGISTER_15H, reg | (cnt << 16) | (cnt), base_addr);
//	reg = tx7332_spi_read_data(REGISTER_16H, base_addr);
//	reg &= 0xF000F000;
//	tx7332_spi_write_data(REGISTER_16H, reg | (cnt << 16) | (cnt), base_addr);
//}
//
//void tx7332_set_TR_SW_OFF_DEL(uint32_t cnt, u32 base_addr) {
//	u32 reg = 0;
//	reg |= ((cnt & 0x3F) << 24);
//	reg |= ((cnt & 0x3F) << 16);
//	reg |= ((cnt & 0x3F) << 8);
//	reg |= (cnt & 0x3F);
//	tx7332_spi_write_data(REGISTER_0CH, reg, base_addr);
//}
//
//void tx7332_TR_SW_ENable(u32 enable, u32 base_addr) {
//	u32 reg = 0;
//
//	tx7332_spi_write_data(REGISTER_1AH, 0x00000000, base_addr);
//	tx7332_spi_write_data(REGISTER_1BH, 0x00000000, base_addr);
//
//	reg = tx7332_spi_read_data(REGISTER_1AH, base_addr);
//
////	DEBUG_PRINT("Read 0X1A : %x\r\n", reg);
//
//	reg = tx7332_spi_read_data(REGISTER_1BH, base_addr);
//
////	DEBUG_PRINT("Read 0X1B : %x\r\n", reg);
//
//	//#define EMIO_TX7332_TR_EN1 55	//
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN1, 0);
//	//#define EMIO_TX7332_TR_EN2 56
//
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN2, 1);
//	//#define EMIO_TX7332_TR_EN3 57
//
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN3, 0);
//	//#define EMIO_TX7332_TR_EN4 58
//
//	XGpioPs_WritePin(&gpiops, EMIO_TX7332_0_TR_EN4, 1);
//}
//
//u32 tx7332_gbl_power_down(void) {
//	u32 reg = 0;
//	reg = tx7332_spi_read_data(REGISTER_18H, TX7332_0_BASE_ADDRESS);
//	reg &= 0xFFFFFFFE;
//	tx7332_spi_write_data(REGISTER_18H, reg, TX7332_0_BASE_ADDRESS);
//	reg = tx7332_spi_read_data(REGISTER_19H, TX7332_0_BASE_ADDRESS);
//	reg |= 0x80000000;
//	tx7332_spi_write_data(REGISTER_19H, reg, TX7332_0_BASE_ADDRESS);
//
//	reg = tx7332_spi_read_data(REGISTER_18H, TX7332_1_BASE_ADDRESS);
//	reg &= 0xFFFFFFFE;
//	tx7332_spi_write_data(REGISTER_18H, reg, TX7332_1_BASE_ADDRESS);
//	reg = tx7332_spi_read_data(REGISTER_19H, TX7332_1_BASE_ADDRESS);
//	reg |= 0x80000000;
//	tx7332_spi_write_data(REGISTER_19H, reg, TX7332_1_BASE_ADDRESS);
//
//}
//
//u32 tx7332_gbl_power_up(void) {
//	u32 reg = 0;
//	reg = tx7332_spi_read_data(REGISTER_18H, TX7332_0_BASE_ADDRESS);
//	reg |= 0x00000001;
//	tx7332_spi_write_data(REGISTER_18H, reg, TX7332_0_BASE_ADDRESS);
//	reg = tx7332_spi_read_data(REGISTER_19H, TX7332_0_BASE_ADDRESS);
//	reg &= 0x7FFFFFFF;
//	tx7332_spi_write_data(REGISTER_19H, reg, TX7332_0_BASE_ADDRESS);
//
//	reg = tx7332_spi_read_data(REGISTER_18H, TX7332_1_BASE_ADDRESS);
//	reg |= 0x00000001;
//	tx7332_spi_write_data(REGISTER_18H, reg, TX7332_1_BASE_ADDRESS);
//	reg = tx7332_spi_read_data(REGISTER_19H, TX7332_1_BASE_ADDRESS);
//	reg &= 0x7FFFFFFF;
//	tx7332_spi_write_data(REGISTER_19H, reg, TX7332_1_BASE_ADDRESS);
//}
//
//void tx7332_TR_SW_Ctrl(u32 base_addr, u8 en, u32 channel_list) {
//	if (en) {
//		tx7332_spi_write_data(REGISTER_1AH, ~channel_list, base_addr);
//	} else {
//		tx7332_spi_write_data(REGISTER_1AH, channel_list, base_addr);
//	}
//}
