#include "afe5832.h"
#include "sleep.h"
#include "xil_cache.h"
//#include "ps_2_pl_ctrl.h"
#include "xscugic.h"
#include "../GPIO/gpio.h"
#include "afe5832_spi.h"
#include "../main.h"

// ADC数据校准
u32 ADC_Data_Cali = 1;
u32 always_pulse = 0;

static u32 AFE5832_MapReg0Value(u32 map)
{
	return (map == AFE5832_REG_MAP_DTGC) ? DTGC_WR_EN_BIT_MASK : 0U;
}

u32 AFE5832_SetRegMap(u32 map)
{
	if (map > AFE5832_REG_MAP_DTGC) {
		return XST_INVALID_PARAM;
	}
	return afe5832_spi_write_byte(REGISTER_0H, AFE5832_MapReg0Value(map));
}

u32 AFE5832_WriteRegMap(u32 map, u32 addr, u32 data)
{
	u32 status;

	if (map > AFE5832_REG_MAP_DTGC || addr > 0xFFU || data > 0xFFFFU) {
		return XST_INVALID_PARAM;
	}

	status = AFE5832_SetRegMap(map);
	if (status != XST_SUCCESS) {
		return status;
	}

	status = afe5832_spi_write_byte(addr, data);
	if (map == AFE5832_REG_MAP_DTGC) {
		u32 restore=AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL);
		if(status==XST_SUCCESS) status=restore;
	}
	return status;
}

u32 AFE5832_ReadRegMap(u32 map, u32 addr, u32 *data)
{
	if (data == NULL || map > AFE5832_REG_MAP_DTGC || addr > 0xFFU) {
		return XST_INVALID_PARAM;
	}

	/* Register 0 controls both DTGC map selection and readback enable. */
	u32 status=afe5832_spi_write_byte(REGISTER_0H,AFE5832_MapReg0Value(map) | REG_READ_EN_BIT_MASK);
	if(status==XST_SUCCESS) status=afe5832_spi_read_byte(addr,data);
	u32 restore=AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL);
	return status==XST_SUCCESS?restore:status;
}
void setup_cali_intr_system(void) {
	/*
	 * pus_ctrl_system ties IRQ_F2P low. Keep this public entry point so the
	 * application initialization order is unchanged, but do not register
	 * the legacy hard-coded PL interrupt 62 when no interrupt is present.
	 */
}
void ADC_cali_intr_handler(void *callback) {

}

void AFE5832_init(void) {
	u32 data = 0;
	//首先初始化IO
	afe5832_io_init();
	// 硬件复位
	XGpioPs_WritePin(&gpiops, AFE5832_RESET_PIN_EMIO, 1);
	usleep(1000);
	XGpioPs_WritePin(&gpiops, AFE5832_RESET_PIN_EMIO, 0);
	//然后软件复位
	/* 0x00 寄存器
	 * bit 4： 1：使能编程DTGC寄存器组    0：使能编程ADC,VCA寄存器组
	 * bit 1:  0：禁止读寄存器   1：允许读寄存器
	 * bit 0:  1:使能软件复位（该位自清除）   0：禁止软件复位
	 * */
	afe5832_spi_write_byte(0x0000, 0x0001);
	usleep(1000);
	//配置寄存器
	//配置ADC LVDS接口

	/* 0x0001寄存器
	 *bit 14:   0:LVDS 1X    1:LVDS 2X
	 *bit 5: 0:使能LVDS接口   1：关闭LVDS接口
	 *bit 0：0：ADCdie正常工作模式  1：ADC die进入掉电模式
	 * */
	afe5832_spi_write_byte(0x0001, 0x0000);
	afe5832_spi_write_byte(0x0003, 0x0000 | (0x01 << 4));
	afe5832_spi_read_byte(0x0003, &data);
	DEBUG_PRINT("AFE5832 READ 0x03 %x \r\n", data);


	// 以下两个寄存器特定位要求写1
	afe5832_spi_write_byte(0x00d1, 0x0000 | (0x07 << 0));
	afe5832_spi_write_byte(0x00d4, 0x0000 | (0x01 << 0));
	//先写1，再写0，固定的初始化顺序
	afe5832_spi_write_byte(0x0041, 0x8000);
	afe5832_spi_write_byte(0x0042, 0x8000);
	usleep(200);
	afe5832_spi_write_byte(0x0041, 0x0000);
	afe5832_spi_write_byte(0x0042, 0x0000);
	usleep(200);

	// TX_TRIG ���壬�̶��ĳ�ʼ������
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 0);
	usleep(100);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 1);
	usleep(200);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 0);
	usleep(100);
	afe5832_spi_write_byte(0x0001, 0x0000);
	//配置测试模式
	/*
	 * 0x0002寄存器
	 * bit 15-13 : FCLK测试   010
	 * bit 12:  0
	 * bit 11:  0
	 * bit 10:  0
	 * bit 9-7  010
	 * bit 6:		0
	 * bit 5-0:	0
	 * */
	afe5832_spi_write_byte(0x0002,
			0x0000 | (PAT_MODE_NORMAL << PAT_MODES_FCLK_2_0)
					| (PAT_MODE_NORMAL << PAT_MODES_2_0) | (0x00 << 12)
					| (0x00 << 11));

	afe5832_spi_write_byte(0x0003,
			0x0000 | (SER_DATA_RATE_14X << SER_DATA_RATE_BIT) | (0x01 << 4)
					| (0x00<< 12));


	// TX_TRIG 脉冲，固定的初始化流程
	usleep(100);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 0);
	usleep(100);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 1);
	usleep(200);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 0);
	usleep(100);

	// DHPF
	afe5832_spi_write_byte(0x15, (0x01 << 5) | (0x03 << 1) | (0x01 << 0));
	afe5832_spi_write_byte(0x21, (0x00 << 5) | (0x03 << 1) | (0x01 << 0));
	afe5832_spi_write_byte(0x2D, (0x01 << 5) | (0x03 << 1) | (0x01 << 0));
	afe5832_spi_write_byte(0x39, (0x00 << 5) | (0x03 << 1) | (0x01 << 0));

	/* Reg 4: bits[1:0] select ADC resolution and bit 4 selects MSB first.
	 * Keep this setting synchronized with afe5832_adc_14b_16lane.v: the
	 * serializer still transfers a 14-bit frame while the valid 12-bit sample
	 * occupies data14[13:2]. */
	afe5832_spi_write_byte(0x0004,
			0x0000 | (0x0001 << 4) | (ADC_12_BITS << ADC_RES_BIT));


	// bit 14: 15	Mhz��ͨ
	// bit 9: 100Khz��ͨ
	afe5832_spi_write_byte(0x00C7,
			0x0000 | (LPF_20MHZ << 14) | (0x14<< 9) | (LPF_20MHZ << 5)
					| (PGA_GAIN_27dB << 2) | (LNA_GAIN_21dB << 0));

	afe5832_spi_write_byte(0x00C6, 0x0000 | (0x00 << 2) | (0x00 << 3));
	afe5832_spi_read_byte(0x00C7, &data);

	DEBUG_PRINT("AFE5832 READ %x \r\n", data);


	/******************************* DTGC *******************************/
	// 1. Enable DTGC Register Map Programming
	afe5832_spi_write_byte(0x00, 0x0010);   // DTGC_WR_EN = 1

	// 2. Select Memory Bank 0, SLOPE_FAC = 0, External TGC_SLOPE trigger.
	// START_GAIN=0 preserves the full 36 dB DTGC ramp; lift the near-field
	// floor with ADC/analog gain before applying the runtime digital trim.
	afe5832_spi_write_byte(0xA1, 0x0090);   // START_GAIN=0 (12 dB), STOP_GAIN=144 (48 dB)
	afe5832_spi_write_byte(0xA2, 0x0147);   // POS_STEP=1 -> 0.25 dB/step, NEG_STEP=71 -> 9 dB/step
	afe5832_spi_write_byte(0xA3, 0x0047);   // MEM_WORD_0 ~ MEM_WORD_71
	afe5832_spi_write_byte(0xA4, 0x0513);   // DTGC starts at 2 cm
	afe5832_spi_write_byte(0xA5, 0x144A);   // Hold max gain from 10 cm to 18 cm

	afe5832_spi_write_byte(0xB5, 0x0000);   // Memory Bank 0, SLOPE_FAC=0, external trigger
	afe5832_spi_write_byte(0xB6, 0xC800);   // Internal Non-Uniform Mode, Profile 0
	afe5832_spi_write_byte(0xB7, 0x0000);

	//==============================================================
	// 3. Configure Memory Bank 0
	//
	// Target TGC profile:
	//
	// 0 ~ 5 cm : Hold minimum gain (12 dB)
	// 5 ~ 10 cm : Ramp up from 12 dB to 48 dB
	// 10 ~ 16 cm : Hold maximum gain (48 dB)
	// >16 cm : Fast ramp down for the next transmit event
	//==============================================================

	// MEM_WORD_0 ~ MEM_WORD_4: two positive events, each 37 clocks
	for (uint8_t addr = 0x01; addr <= 0x05; addr++) {
		afe5832_spi_write_byte(addr, 0x2525);
	}

	// MEM_WORD_5: low event 37 clocks, high event 36 clocks
	afe5832_spi_write_byte(0x06, 0x2425);

	// MEM_WORD_6 ~ MEM_WORD_71: two positive events, each 36 clocks
	for (uint8_t addr = 0x07; addr <= 0x48; addr++) {
		afe5832_spi_write_byte(addr, 0x2424);
	}

	// Clear unused MEM_WORD_72 ~ MEM_WORD_159
	for (uint8_t addr = 0x49; addr <= 0xA0; addr++) {
		afe5832_spi_write_byte(addr, 0x0000);
	}


	afe5832_spi_write_byte(0x00, 0x0000 | (0x00 << 4));

	/******************************* DTGC *******************************/


	// 设置电源模式
	afe5832_spi_write_byte(0x00C8, 0x0000 | (0x00 << 0));

	/* Deterministic TX_TRIG pulse after the AFE register state is complete. */
	usleep(100);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 0);
	usleep(100);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 1);
	usleep(200);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 0);
	usleep(100);

}
void AFE5832_Read_Temp(float *temp) {
	u32 data = 0;
	data = afe5832_spi_read_byte(0xD0, &data);
}

u32 afe5832_io_init(void) {

	//1为输出，0为输入
	XGpioPs_SetDirectionPin(&gpiops, AFE5832_TGC_PROF_1_PIN_EMIO, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_TGC_PROF_1_PIN_EMIO, 1);
	XGpioPs_WritePin(&gpiops, AFE5832_TGC_PROF_1_PIN_EMIO, 0);

	XGpioPs_SetDirectionPin(&gpiops, AFE5832_TGC_PROF_2_PIN_EMIO, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_TGC_PROF_2_PIN_EMIO, 1);
	XGpioPs_WritePin(&gpiops, AFE5832_TGC_PROF_2_PIN_EMIO, 0);

//	XGpioPs_SetDirectionPin(&gpiops, AFE5832_TGC_SLOPE_PIN_EMIO, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_TGC_SLOPE_PIN_EMIO, 1);
//	XGpioPs_WritePin(&gpiops, AFE5832_TGC_SLOPE_PIN_EMIO, 0);
//
//	XGpioPs_SetDirectionPin(&gpiops, AFE5832_TGC_UP_DN_PIN_EMIO, 1);
//	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_TGC_UP_DN_PIN_EMIO, 1);
//	XGpioPs_WritePin(&gpiops, AFE5832_TGC_UP_DN_PIN_EMIO, 0);

	XGpioPs_SetDirectionPin(&gpiops, AFE5832_DIS_HPF_PIN_EMIO, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_DIS_HPF_PIN_EMIO, 1);
	XGpioPs_WritePin(&gpiops, AFE5832_DIS_HPF_PIN_EMIO, 0);

	XGpioPs_SetDirectionPin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 1);
	XGpioPs_WritePin(&gpiops, AFE5832_TX_TRIG_PIN_EMIO, 0);

	XGpioPs_SetDirectionPin(&gpiops, AFE5832_PDN_FAST_PIN_EMIO, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_PDN_FAST_PIN_EMIO, 1);
	XGpioPs_WritePin(&gpiops, AFE5832_PDN_FAST_PIN_EMIO, 0);

	XGpioPs_SetDirectionPin(&gpiops, AFE5832_PDN_GBL_PIN_EMIO, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_PDN_GBL_PIN_EMIO, 1);
	XGpioPs_WritePin(&gpiops, AFE5832_PDN_GBL_PIN_EMIO, 0);

	XGpioPs_SetDirectionPin(&gpiops, AFE5832_RESET_PIN_EMIO, 1);
	XGpioPs_SetOutputEnablePin(&gpiops, AFE5832_RESET_PIN_EMIO, 1);
	XGpioPs_WritePin(&gpiops, AFE5832_RESET_PIN_EMIO, 0);
	return XST_SUCCESS;
}

//void AFE5832_Start_TGC(void) {
//	XGpioPs_WritePin(&gpiops, AFE5832_TGC_SLOPE_PIN_EMIO, 1);
//	delay_spi_nop();
//	XGpioPs_WritePin(&gpiops, AFE5832_TGC_SLOPE_PIN_EMIO, 0);
//}
//void AFE5832_Stop_TGC(void) {
//	XGpioPs_WritePin(&gpiops, AFE5832_TGC_UP_DN_PIN_EMIO, 1);
//	delay_spi_nop();
//	XGpioPs_WritePin(&gpiops, AFE5832_TGC_UP_DN_PIN_EMIO, 0);
//}
