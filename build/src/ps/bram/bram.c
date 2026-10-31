#include "stdio.h"
#include "xil_printf.h"
#include "xbram.h"
#include "xparameters.h"
#include "bram.h"
#include "focus_bank.h"
#include "xpseudo_asm.h"
#include "sleep.h"
#include <string.h>
#include "../HV/hv.h"
#include "../system_ctrl/imaging_power.h"
#include "../data_transfer/frame_store.h"

/*
���Ƶ��ϵ�� sin		0x40000000	0x40007FFC	8192
���Ƶ��ϵ�� cos		0x40008000	0x4000FFFC	8192
�����任			0x40010000	0x40010FFC	1024
���侲̬����		0x40011000	0x400110FC	64
��Ԫ����			0x40011100	0x400111FC	64
�伣����			0x40011200	0x400112FC	64
��̬�˲�ϵ��		0x40011300	0x400113FC	64
ɨ�����			0x40011400	0x400114FC	64
*/
#define BRAM_IQSIN_ADDR				BRAM_BASEADDR
#define BRAM_IQCOS_ADDR				BRAM_BASEADDR + 0x8000
#define BRAM_LOG_ADDR				BRAM_BASEADDR + 0x10000
#define BRAM_TRANS_STATIC_ADDR		BRAM_BASEADDR + 0x11000
#define BRAM_ELEPOS_ADDR			BRAM_BASEADDR + 0x11200
#define BRAM_APOD_ADDR				BRAM_BASEADDR + 0x11300
#define BRAM_DFIR_ADDR				BRAM_BASEADDR + 0x11400
#define BRAM_SCAN_ADDR				BRAM_BASEADDR + 0x11500
#define BRAM_TRANS_DYNAM_ADDR		BRAM_BASEADDR + 0x100000
#define BRAM_DELAY_ADDR				BRAM_BASEADDR + 0x110000
#define BRAM_TX_CFG_RD_REQ_ADDR		(BRAM_BASEADDR + 0x004C0110U)
#define BRAM_TX_CFG_RDDATA_ADDR		(BRAM_BASEADDR + 0x004C0114U)

#define BRAM_IQSIN_DEPTH			8192
#define BRAM_IQCOS_DEPTH			8192
#define BRAM_LOG_DEPTH				1024
#define BRAM_TRANS_DEPTH			128
#define BRAM_ELEPOS_DEPTH			64
#define BRAM_APOD_DEPTH				64
#define BRAM_DFIR_DEPTH				64
#define BRAM_SCAN_DEPTH				10
#define BRAM_DELAY_DEPTH			64
#define BRAM_DELAY_PROFILE_DEPTH	16384


#define BRAM_DATA_BYTE				4
#define BRAM_UART_MAX_WORDS			8U
#define TX7332_REG_ADDR_MAX			0x3FFU
#define TX7332_READ_SETTLE_US		10000U

typedef struct {
	const char *name;
	u32 base_addr;
	u32 depth;
} BramSectionInfo;

static const BramSectionInfo bram_sections[] = {
	{"fdemod_sin", BRAM_IQSIN_ADDR, BRAM_IQSIN_DEPTH},
	{"fdemod_cos", BRAM_IQCOS_ADDR, BRAM_IQCOS_DEPTH},
	{"log_table", BRAM_LOG_ADDR, BRAM_LOG_DEPTH},
	{"x_element", BRAM_ELEPOS_ADDR, BRAM_ELEPOS_DEPTH},
	{"hamming", BRAM_APOD_ADDR, BRAM_APOD_DEPTH},
	{"dfilter", BRAM_DFIR_ADDR, BRAM_DFIR_DEPTH},
	{"t_timing", BRAM_SCAN_ADDR, BRAM_SCAN_DEPTH},
	{"sin_beta", BRAM_DELAY_ADDR, BRAM_DELAY_DEPTH},
	{"delay_profile", BRAM_TRANS_DYNAM_ADDR, BRAM_DELAY_PROFILE_DEPTH},
};
void para_down(void);

static u32 beamform_run_request;
static u32 beamform_spi_start_request;

u32 Beamform_GetRunRequest(void) { return beamform_run_request; }
u32 Beamform_GetSpiStartRequest(void) { return beamform_spi_start_request; }
u32 Beamform_GetDownsample(void) { return T_timing_hex[9]; }

void Beamform_SetControl(u32 beamform_enable, u32 spi_start_enable)
{
    if(!imaging_power_allowed() && (beamform_enable || spi_start_enable)) return;
	XBram_WriteReg(BRAM_BEAMFORM_CTRL_ADDR, 0,
			beamform_enable ? BEAMFORM_CTRL_RUN_MASK : 0U);
	XBram_WriteReg(BRAM_SPI_START_ADDR, 0, spi_start_enable ? 1U : 0U);
	/* This records what PS requested, not whether PL accepted it or drained.
	 * Do not read these write-only timing addresses for status or busy checks. */
	beamform_run_request = beamform_enable ? 1U : 0U;
	beamform_spi_start_request = spi_start_enable ? 1U : 0U;
}

/* Set the receive digital gain shift. Safe to call while imaging is running:
 * para_st latches word 12 on the parameter clock and filter_iq picks it up on
 * the next sample, so no beamforming restart is needed. Returns the value that
 * was actually programmed after clamping. */
u32 Rx_SetGainShift(u32 gain_shift)
{
	if (gain_shift > RX_GAIN_SHIFT_MAX)
	{
		gain_shift = RX_GAIN_SHIFT_MAX;
	}
	XBram_WriteReg(BRAM_RX_GAIN_SHIFT_ADDR, 0, gain_shift);
	return gain_shift;
}

int Beamform_Resume(void)
{
    if(!imaging_power_allowed() || !focus_bank_ready()) return -1;
    if(frame_store_resume()!=0) {
        hv_disable(); Beamform_Halt();
        xil_printf("@DDR ERROR resume/ABI; scan not started\r\n"); return -1;
    }
	/*
	 * tx_cfg_top starts its static-register configuration only on a rising
	 * edge of spi_start. scan_enable falling returns that FSM to CFG_IDLE, so
	 * writing spi_start=1 again after a pause is insufficient when the bit was
	 * already high. Force a real low-to-high transition before scanning.
	 */
	Beamform_SetControl(0U, 0U);
	usleep(100);
	Beamform_SetControl(0U, 1U);
	usleep(5000);
	Beamform_SetControl(1U, 1U);
	return 0;
}

void Beamform_Pause(void)
{
	/* Freeze scanning while keeping TX7332 configured and HV unchanged. */
	Beamform_SetControl(0U, 1U);
}

void Beamform_RequestStop(void)
{
    XBram_WriteReg(BRAM_BEAMFORM_CTRL_ADDR,0,0U);
    beamform_run_request=0; /* No SPI edge, no initialization, always safe. */
}

void Beamform_Halt(void)
{
	/* Full imaging stop: arm spi_start for a clean edge on next START. */
	Beamform_SetControl(0U, 0U);
}

int Beamform_Start(void)
{
    if(!imaging_power_allowed() || !focus_bank_ready()) return -1;
    if(frame_store_resume()!=0) {
        hv_disable(); Beamform_Halt();
        xil_printf("@DDR ERROR resume/ABI; scan not started\r\n"); return -1;
    }
	/* Reconfigure TX7332 while scanning and HV output are disabled. */
	Beamform_SetControl(0U, 1U);
	usleep(5000);
	hv_enable();
	Beamform_SetControl(1U, 1U);
	return 0;
}

void Beamform_Stop(void)
{
	/* Disable HV immediately, then allow PL to finish the current frame. */
	hv_disable();
	Beamform_Halt();
	sleep(1);
}
u32 Bram_WriteSectionWords(const char *section, u32 offset, const u32 *words, u32 count)
{
	u32 section_count = sizeof(bram_sections) / sizeof(bram_sections[0]);

	if (section == NULL || words == NULL || count == 0U || count > BRAM_UART_MAX_WORDS) {
		return BRAM_CFG_STATUS_ARGS;
	}

	for (u32 i = 0; i < section_count; i++) {
		const BramSectionInfo *info = &bram_sections[i];

		if (strcmp(section, info->name) != 0) {
			continue;
		}
		if (offset >= info->depth || count > (info->depth - offset)) {
			return BRAM_CFG_STATUS_RANGE;
		}
        if(strcmp(section,"t_timing")==0) {
            for(u32 j=0;j<count;j++) {
                if(((offset+j==0 || offset+j==8) && words[j]!=6144U) ||
                   (offset+j==9 && words[j]>4U)) return BRAM_CFG_STATUS_RANGE;
            }
        }
		if(!strcmp(section,"delay_profile")) focus_bank_invalidate();
		for (u32 j = 0; j < count; j++) {
			XBram_WriteReg(info->base_addr, (offset + j) * BRAM_DATA_BYTE, words[j]);
			/* Timing words are write-only: retain the successfully programmed
			 * values for STATUS and any subsequent parameter download. */
			if (strcmp(section, "t_timing") == 0) T_timing_hex[offset + j] = words[j];
		}
		return BRAM_CFG_STATUS_OK;
	}

	return BRAM_CFG_STATUS_UNKNOWN;
}
u32 Tx7332_WriteSectionWords(const char *section, u32 offset, const u32 *words, u32 count)
{
	if (section == NULL || words == NULL || count == 0U || count > BRAM_UART_MAX_WORDS) {
		return BRAM_CFG_STATUS_ARGS;
	}
	if (strcmp(section, "tx7332_config_data") != 0) {
		return BRAM_CFG_STATUS_UNKNOWN;
	}
	if (offset >= BRAM_TRANS_DEPTH || count > (BRAM_TRANS_DEPTH - offset)) {
		return BRAM_CFG_STATUS_RANGE;
	}
	for (u32 i = 0; i < count; i++) {
		XBram_WriteReg(BRAM_TRANS_STATIC_ADDR, (offset + i) * BRAM_DATA_BYTE, words[i]);
	}
	return BRAM_CFG_STATUS_OK;
}

u32 Tx7332_ReadRegister(u32 addr, u32 *data)
{
	if (data == NULL || addr > TX7332_REG_ADDR_MAX) {
		return BRAM_CFG_STATUS_ARGS;
	}
	if (Beamform_GetRunRequest() != 0U) {
		return BRAM_CFG_STATUS_BUSY;
	}

	/* Trigger the PL TX7332 readback FSM through para_reg. For a deterministic
	 * readback, stop beamforming before issuing TX7332 RD so tx_cfg_top is in
	 * CFG_IDLE and can accept the read request. */
	XBram_WriteReg(BRAM_TX_CFG_RD_REQ_ADDR, 0, addr & TX7332_REG_ADDR_MAX);
	usleep(TX7332_READ_SETTLE_US);
	*data = XBram_ReadReg(BRAM_TX_CFG_RDDATA_ADDR, 0);
	return BRAM_CFG_STATUS_OK;
}
// ���ϵ��
void fdemod_para(void)
{
	for (int i = 0; i < BRAM_IQSIN_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_IQSIN_ADDR, i*BRAM_DATA_BYTE, fdemod_sin_array[i]);
	}

	for (int i = 0; i < BRAM_IQCOS_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_IQCOS_ADDR, i*BRAM_DATA_BYTE, fdemod_cos_array[i]);
	}
}

// �����任
void log_para(void)
{
	for (int i = 0; i < BRAM_LOG_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_LOG_ADDR, i*BRAM_DATA_BYTE, log_table_hex[i]);
	}
}

// ��Ԫ����
void x_element_para(void)
{
	for (int i = 0; i < BRAM_ELEPOS_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_ELEPOS_ADDR, i*BRAM_DATA_BYTE, x_element_hex[i]);
	}
}

// �伣����
void hamming_para(void)
{
	for (int i = 0; i < BRAM_APOD_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_APOD_ADDR, i*BRAM_DATA_BYTE, hamming_hex[i]);
	}
}

// ��̬�˲�ϵ��
void dfilter_para(void)
{
	for (int i = 0; i < BRAM_DFIR_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_DFIR_ADDR, i*BRAM_DATA_BYTE, dfilter_hex[i]);
	}
}

// ɨ�����ʱ�����
void t_timing_para(void)
{
	for (int i = 0; i < BRAM_SCAN_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_SCAN_ADDR, i*BRAM_DATA_BYTE, T_timing_hex[i]);
	}
}

// �ӳ�����
void sin_beta_para(void)
{
	for (int i = 0; i < BRAM_DELAY_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_DELAY_ADDR, i*BRAM_DATA_BYTE, sin_beta_hex[i]);
	}
}


void tx7332_config_para(void)
{
	for (int i = 0; i < BRAM_TRANS_DEPTH; i++)
	{
		XBram_WriteReg(BRAM_TRANS_STATIC_ADDR, i*BRAM_DATA_BYTE, tx7332_config_data[i]);
	}
}

static int focus_write_word(uint32_t index,uint32_t value,void *ctx) {
    (void)ctx;XBram_WriteReg(BRAM_TRANS_DYNAM_ADDR,index*4U,value);return 0;
}
const char *Focus_Apply(uint32_t depth,uint32_t crc) {
    if(!imaging_power_allowed() || Beamform_GetRunRequest() || hv_is_enabled()) return "FOCUS_NOT_STOPPED";
    if(frame_store_pause_retained()!=0) return "FOCUS_DDR_NOT_DRAINED";
    const char *error=focus_bank_apply(depth,crc,focus_write_word,NULL);
    dsb();return error;
}
void delay_profile_para(void) {
    if(focus_bank_init()!=0) {xil_printf("@FOCUS ERROR DDR table integrity; scan disabled\r\n");return;}
    const focus_entry_t *entry=focus_bank_find(80);
    const char *error=focus_bank_apply(80,entry->crc,focus_write_word,NULL);
    dsb();
    if(error) xil_printf("@FOCUS ERROR default load\r\n");
}

void para_down(void)
{
	fdemod_para();
	log_para();
	x_element_para();
	hamming_para();
	dfilter_para();
	t_timing_para();
	tx7332_config_para();
	delay_profile_para();
	sin_beta_para();
	/*
	 * The desktop application owns the imaging lifecycle. Leave PL and HV in
	 * a deterministic stopped state after loading the default tables.
	 */
	hv_disable();
	Beamform_Halt();
}
