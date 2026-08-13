//#ifndef _TX7332_H
//#define _TX7332_H
//
//#include "tx7332_spi.h"
//#include "../main.h"
//#define REGISTER_0H 0x00
//#define REGISTER_1H 0x01
//#define REGISTER_6H 0x06
//#define REGISTER_0BH 0x0B
//#define REGISTER_0CH 0x0C
//#define REGISTER_0FH 0x0F
//#define REGISTER_14H 0x14
//#define REGISTER_15H 0x15
//#define REGISTER_16H 0x16
//#define REGISTER_17H 0x17
//#define REGISTER_18H 0x18
//#define REGISTER_19H 0x19
//#define REGISTER_1EH 0x1E
//#define REGISTER_1FH 0x1F
//#define REGISTER_1AH 0x1A
//#define REGISTER_1BH 0x1B
//#define DEL_PROFILE_REGISTER_BEGIN 0x20
//#define DEL_PROFILE_REGISTER_END 0x2F
//#define PATTERN_PROFILE_REGISTER_BEGIN 0x120
//#define PATTERN_PROFILE_REGISTER_END 0x123
//
//#define TX7332_RESET_PIN 69
//#define TX7332_STDY_PIN 70		//等待引脚 高电平有效，输出
//#define TX7332_CW_EN_PIN 71		//CW模式使能引脚，输出
//#define TX7332_DSEL_0_PIN 72		//配置选择引脚0，输出
//#define TX7332_DSEL_1_PIN 73		//配置选择引脚1，输出
//#define TX7332_SHUTZ_PIN 74		//过热关闭信号，低电平有效，输入
//
//#define EMIO_TX7332_0_TR_EN1 75	//
//#define EMIO_TX7332_0_TR_EN2 76
//#define EMIO_TX7332_0_TR_EN3 77
//#define EMIO_TX7332_0_TR_EN4 78
//
//#define EMIO_TX7332_1_TR_EN1 79	//
//#define EMIO_TX7332_1_TR_EN2 80
//#define EMIO_TX7332_1_TR_EN3 81
//#define EMIO_TX7332_1_TR_EN4 82
//
//
//#define USE_0_2_PITCH
////#define USE_0_4_PITCH
///*
// * bitslip_enable         = slv_reg0[0];
// * cnt_value_in           = slv_reg1[4:0]
// * send_write_fifo_enable = slv_reg2[0];
// * tx7332_0_pulse_en      = slv_reg3[0];
// * tx7332_1_pulse_en      = slv_reg3[4];
// *
// * */
//#define ON_CHIP_BF_MODE 1
//#define OFF_CHIP_BF_MODE 0
//
//#define DEFAULT_POWER_MODE 0
//#define DYNAMIC_POWER_MODE 1
//
//#define TX7332_0_PULSE_BIT 0x01
//
//#define TX7332_1_PULSE_BIT 0x10
//
//extern u32 channel_to_reg[32];
//u32 tx7332_io_init(void);
//u32 tx7332_init(void);
////tx7332初始化配置
//void tx7332_reg_config(u32 base_addr);
//void tx7332_start_pulse(u8 powerMode, u32 chip_sel);
//void tx7332_set_PowMode(u32 power_mode, u32 base_addr);
//void tx7332_set_work_mode(u32 mode, u32 base_addr);
//void tx7332_Enter_standBy(u32 mode);
//u32 tx7332_overTemp(void);
//void tx7332_set_repeat_cnt(u32 cnt, u32 base_addr);
//void tx7332_select_pattern_profile(u32 m_profile_num, u32 base_addr);
//void tx7332_set_channel_pattern_mode(u32 base_addr);
//void tx7332_set_channel_delay(u32 m_profile, u32 base_addr);
//
//void tx7332_select_delay_profile(u32 m_profile, u32 base_addr);
//
//void tx7332_enable_CW_mode(u32 base_addr);
//void tx7332_set_tail_cnt(int cnt, u32 base_addr);
//void tx7332_channel_enable(int channel_start, int channel_end, int enable,
//		u32 base_addr);
//void tx7332_set_TR_SW_DEL(uint32_t cnt, u32 base_addr);
//
//void tx7332_set_TR_SW_OFF_DEL(uint32_t cnt, u32 base_addr);
//
//void tx7332_TR_SW_ENable(u32 enable, u32 base_addr);
//void tx7332_TR_SW_Ctrl(u32 base_addr,u8 en,u32 channel_list);
//u32 tx7332_gbl_power_down(void);
//u32 tx7332_gbl_power_up(void);
//
//#endif
