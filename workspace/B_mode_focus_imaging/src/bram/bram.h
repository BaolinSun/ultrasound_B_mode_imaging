#ifndef _BRAM_H_
#define _BRAM_H_

#include "stdio.h"
#include "xil_printf.h"
#include "xbram.h"
#include "xparameters.h"

#define BRAM_CFG_STATUS_OK			0U
#define BRAM_CFG_STATUS_UNKNOWN		1U
#define BRAM_CFG_STATUS_RANGE		2U
#define BRAM_CFG_STATUS_ARGS		3U
#define BRAM_CFG_STATUS_BUSY		4U

#define BRAM_BASEADDR				XPAR_XBRAM_0_BASEADDR
#define BRAM_SPI_START_ADDR			(BRAM_BASEADDR + 0x00011528U)
#define BRAM_BEAMFORM_CTRL_ADDR		(BRAM_BASEADDR + 0x0001152CU)
#define BEAMFORM_CTRL_RUN_MASK		0x00000001U
/* Scan-timing word 12: receive digital gain shift applied in filter_iq.
 * Each step is +6.02 dB; 0 reproduces the original fixed >>19 bit-exactly.
 * The PL clamps to 8, which is the point where the 43-bit accumulator fills
 * the 59-bit pre-shift word. Digital gain amplifies quantisation noise along
 * with the signal, so prefer analog gain (AFE LNA/PGA/DTGC) first. */
#define BRAM_RX_GAIN_SHIFT_ADDR		(BRAM_BASEADDR + 0x00011530U)
#define RX_GAIN_SHIFT_MAX			8U

void para_down(void);
void Beamform_SetControl(u32 beamform_enable, u32 spi_start_enable);
/* PS-issued requests, NOT PL activity feedback. These scan-timing words are
 * write-only in the current bitstream; reading their addresses returns stale data. */
u32 Beamform_GetRunRequest(void);
u32 Beamform_GetSpiStartRequest(void);
u32 Rx_SetGainShift(u32 gain_shift);
void Beamform_Resume(void);
void Beamform_Pause(void);
void Beamform_Halt(void);
void Beamform_Start(void);
void Beamform_Stop(void);
u32 Bram_WriteSectionWords(const char *section, u32 offset, const u32 *words, u32 count);
u32 Tx7332_WriteSectionWords(const char *section, u32 offset, const u32 *words, u32 count);
u32 Tx7332_ReadRegister(u32 addr, u32 *data);


extern u32 fdemod_cos_array[8192];
extern u32 fdemod_sin_array[8192];
extern u32 log_table_hex[1024];
extern u32 x_element_hex[64];
extern u32 hamming_hex[64];
extern u32 dfilter_hex[64];
extern u32 T_timing_hex[10];
extern u32 sin_beta_hex[64];
extern u32 delay_profile_reg[32];
extern u32 tx7332_config_data[128];
extern u32 delay_profile[16384];

#endif
