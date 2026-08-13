#ifndef _AFE5832_H
#define _AFE5832_H

/****************寄存器宏定义************************/
#include "../main.h"
#include "afe5832_spi.h"

#define REGISTER_0H 0x00
#define	DTGC_WR_EN_BIT_MASK 0x10
#define REG_READ_EN_BIT_MASK 0x02
#define SOFTWARE_RESET_BIT_MASK 0x01

#define AFE5832_REG_MAP_NORMAL 0U
#define AFE5832_REG_MAP_GLOBAL 0U
#define AFE5832_REG_MAP_ADC    1U
#define AFE5832_REG_MAP_VCA    2U
#define AFE5832_REG_MAP_DTGC   3U

// 测试波形模式设置
#define PAT_MODE_NORMAL 0x00 //正常工作模式
#define PAT_MODE_SYNC 0x01	//half frame 1, half frame 0
#define PAT_MODE_DESKEW 0x02   //010101010101
#define PAT_MODE_CUSTOM 0x03	//输出写入到CUSTOM_PATTERN寄存器的值
#define PAT_MODE_ALL_1 0x04		//111111111111
#define PAT_MODE_TOGGLE_MODE 0x05	//111111111111000000000000111111111111
#define PAT_MODE_ALL_0 0x06		//000000000000
#define PAT_MODE_RAMP 0x07		//递增

#define PAT_MODES_FCLK_2_0 13
#define PAT_MODES_2_0 7

//lvds序列化速率
#define SER_DATA_RATE_12X 0x00
#define SER_DATA_RATE_14X 0x01
#define SER_DATA_RATE_16X 0x04
#define SER_DATA_RATE_10X 0x03

#define SER_DATA_RATE_BIT 13

//ADC精度位数
#define ADC_12_BITS 0x00
#define ADC_10_BITS 0x03

#define ADC_RES_BIT 0
#define AFE5832_RESET_PIN_EMIO 65
#define AFE5832_TGC_PROF_1_PIN_EMIO 68
#define AFE5832_TGC_PROF_2_PIN_EMIO 69
//#define AFE5832_TGC_SLOPE_PIN_EMIO 85
//#define AFE5832_TGC_UP_DN_PIN_EMIO 86
#define AFE5832_PDN_FAST_PIN_EMIO 70
#define AFE5832_PDN_GBL_PIN_EMIO 71
#define AFE5832_TX_TRIG_PIN_EMIO 72
#define AFE5832_DIS_HPF_PIN_EMIO 67
#define AFE_TGC_UP_DOWN_MODE 0x01
#define AFE_TGC_EX_NON_UNIFORM_MODE 0x00
#define AFE_TGC_FIXED_MODE 0x02
#define AFE_TGC_IN_NON_UNIFORM_MODE 0x03


#define LPF_20MHZ 0x00
#define LPF_25MHZ 0x01
#define LPF_10MHZ 0x02
#define LPF_15MHZ 0x03


#define PGA_GAIN_24dB 0x00
#define PGA_GAIN_27dB 0x01
#define PGA_GAIN_21dB 0x02

#define LNA_GAIN_18dB 0x00
#define LNA_GAIN_21dB 0x01
#define LNA_GAIN_15dB 0x03

// 是否校准完成
extern u32 ADC_Data_Cali;

void AFE5832_init(void);
u32 AFE5832_SetRegMap(u32 map);
u32 AFE5832_WriteRegMap(u32 map, u32 addr, u32 data);
u32 AFE5832_ReadRegMap(u32 map, u32 addr, u32 *data);
void AFE5832_Start_TGC(void);
void AFE5832_Stop_TGC(void);
void ADC_cali_intr_handler(void *callback);
void setup_cali_intr_system(void);
u32 afe5832_io_init(void);

#endif
