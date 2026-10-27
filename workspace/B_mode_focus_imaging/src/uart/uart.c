#include "uart.h"
#include "xscugic.h"
#include "xinterrupt_wrap.h"
#include "xstatus.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "../AFE5832/afe5832.h"
#include "../system_ctrl/imaging_power.h"
#include "../system_ctrl/cpu1_power.h"
#include "../system_ctrl/cpu0_idle.h"
#include "../bram/bram.h"
#include "../HV/hv.h"
#include "../data_transfer/frame_store.h"
#include "../usb/usb_transport.h"
#include "uart_log.h"
#include "xiltimer.h"
#include "../../../../common/wifi_control_protocol.h"

#define UART_BASEADDR       0xE0001000U /* Preserve UART1 after enabling UART0. */
#define UART_INT_IRQ_ID \
	((u32)(XGet_IntrId(uartps.Config.IntrId) + \
		XGet_IntrOffset(uartps.Config.IntrId)))

#define UART_LINE_MAX       160U
#define UART_LINE_DEPTH     4U
#define UART_BRAM_MAX_WORDS 8U

XUartPs uartps;
extern XScuGic xInterruptController;

u8 pulse_ctrl = 0;

static volatile char uart_lines[UART_LINE_DEPTH][UART_LINE_MAX];
static volatile u32 uart_line_wr_idx = 0;
static volatile u32 uart_line_rd_idx = 0;
static volatile u32 uart_line_count = 0;
static char uart_line_build[UART_LINE_MAX];
static u32 uart_line_len = 0;
static volatile u8 uart_line_overflow = 0;
static u32 uart_cfg_seq = 0;
static u8 uart_cfg_active = 0;
static u8 uart_cfg_done = 0;
int uart_config_active(void) { return uart_cfg_active!=0; }
int uart_idle_pending(void) {
    if(uart_cfg_active || uart_line_count || uart_line_overflow || uartps.Config.BaseAddress!=UART_BASEADDR) return 1;
    u32 sr=XUartPs_ReadReg(UART_BASEADDR,XUARTPS_SR_OFFSET);
    u32 mask=XUartPs_ReadReg(UART_BASEADDR,XUARTPS_IMR_OFFSET);
    return (sr&(XUARTPS_SR_RXEMPTY|XUARTPS_SR_TXEMPTY))!=(XUARTPS_SR_RXEMPTY|XUARTPS_SR_TXEMPTY) ||
        (mask&(XUARTPS_IXR_RXOVR|XUARTPS_IXR_TOUT))!=(XUARTPS_IXR_RXOVR|XUARTPS_IXR_TOUT);
}
static command_response_fn uart_response_fn = NULL;
enum { SOURCE_UART1, SOURCE_USB, SOURCE_WIFI };
static unsigned command_source, cfg_owner;
static u32 wifi_session, cfg_session;
static u64 cfg_activity;
static u64 command_ms(void) { XTime t; XTime_GetTime(&t); return (u64)t/(COUNTS_PER_SECOND/1000U); }
static int owns_cfg(void) { return cfg_owner==command_source && (cfg_owner!=SOURCE_WIFI || cfg_session==wifi_session); }
static void wifi_abort(void) {
    hv_disable(); Beamform_Halt(); uart_cfg_active=0; uart_cfg_done=0;
    usb_image_config_end();
    (void)AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL);
}

static void uart_emit_response(const char *text)
{
	if (uart_response_fn != NULL) {
		uart_response_fn(text);
	} else {
		xil_printf("%s\r\n", text);
	}
}

static void uart_emitf(const char *fmt, ...)
{
	char response[128];
	va_list args;

	va_start(args, fmt);
	vsnprintf(response, sizeof(response), fmt, args);
	va_end(args);
	response[sizeof(response) - 1U] = '\0';
	uart_emit_response(response);
}

static void uart_ack(u32 seq, const char *cmd)
{
	uart_emitf("@ACK %u %s", seq, cmd);
}

static void uart_nack(u32 seq, const char *err)
{
	if(uart_cfg_active && cfg_owner==SOURCE_WIFI && owns_cfg()) wifi_abort();
	uart_emitf("@NACK %u %s", seq, err);
}

static void uart_queue_line(void)
{
	u32 idx;
	u32 i;

	if (uart_line_len == 0) {
		return;
	}

	if (uart_line_count >= UART_LINE_DEPTH) {
		uart_line_overflow = 1;
		uart_line_len = 0;
		return;
	}

	idx = uart_line_wr_idx;
	for (i = 0; i < uart_line_len && i < (UART_LINE_MAX - 1U); i++) {
		uart_lines[idx][i] = uart_line_build[i];
	}
	uart_lines[idx][i] = '\0';
	uart_line_wr_idx = (uart_line_wr_idx + 1U) % UART_LINE_DEPTH;
	uart_line_count++;
	uart_line_len = 0;
}

static void uart_push_rx_byte(u8 data)
{
	if (data == '\r' || data == '\n') {
		uart_queue_line();
		return;
	}

	if (uart_line_len >= (UART_LINE_MAX - 1U)) {
		uart_line_len = 0;
		uart_line_overflow = 1;
		return;
	}

	uart_line_build[uart_line_len++] = (char)data;
}

static void uart_drain_rx_fifo(XUartPs *uart_ps_handle)
{
	u8 rxbuf[64];
	u32 got;
	u32 i;

	do {
		got = XUartPs_Recv(uart_ps_handle, rxbuf, sizeof(rxbuf));
		for (i = 0; i < got; i++) {
			uart_push_rx_byte(rxbuf[i]);
		}
	} while (got == sizeof(rxbuf));
}

static int uart_pop_line(char *line)
{
	u32 mask;
	u32 i;
	u32 idx;

	if (uart_line_count == 0) {
		return 0;
	}

	mask = XUartPs_ReadReg(uartps.Config.BaseAddress, XUARTPS_IMR_OFFSET);
	XUartPs_SetInterruptMask(&uartps, 0);

	idx = uart_line_rd_idx;
	for (i = 0; i < UART_LINE_MAX; i++) {
		line[i] = uart_lines[idx][i];
		if (line[i] == '\0') {
			break;
		}
	}
	line[UART_LINE_MAX - 1U] = '\0';
	uart_line_rd_idx = (uart_line_rd_idx + 1U) % UART_LINE_DEPTH;
	uart_line_count--;

	XUartPs_SetInterruptMask(&uartps, mask);
	return 1;
}

static char *uart_next_token(char **ctx)
{
	char *p = *ctx;
	char *start;

	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p == '\0') {
		*ctx = p;
		return NULL;
	}
	start = p;
	while (*p != '\0' && *p != ' ' && *p != '\t') {
		p++;
	}
	if (*p != '\0') {
		*p++ = '\0';
	}
	*ctx = p;
	return start;
}

static int uart_parse_u32(const char *text, u32 *value)
{
	if(command_source==SOURCE_WIFI) {
		uint32_t parsed; if(!wc_uint(text,&parsed)) return 0;
		*value=(u32)parsed; return 1;
	}
	char *endptr;
	unsigned long parsed;

	if (text == NULL || value == NULL || *text == '\0') {
		return 0;
	}
	parsed = strtoul(text, &endptr, 0);
	if (*endptr != '\0' || parsed > 0xFFFFFFFFUL) {
		return 0;
	}
	*value = (u32)parsed;
	return 1;
}

static int uart_parse_map(const char *text, u32 *map)
{
	if (text == NULL || map == NULL) {
		return 0;
	}
	if (strcmp(text, "GLOBAL") == 0) {
		*map = AFE5832_REG_MAP_GLOBAL;
		return 1;
	}
	if (strcmp(text, "ADC") == 0) {
		*map = AFE5832_REG_MAP_ADC;
		return 1;
	}
	if (strcmp(text, "VCA") == 0) {
		*map = AFE5832_REG_MAP_VCA;
		return 1;
	}
	if (strcmp(text, "DTGC") == 0) {
		*map = AFE5832_REG_MAP_DTGC;
		return 1;
	}
	return 0;
}

static void uart_handle_cfg(char **ctx)
{
	char *op = uart_next_token(ctx);
	char *arg = uart_next_token(ctx);
	u32 value;

	if (op == NULL) {
		uart_nack(uart_cfg_seq, "CFG_OP");
		return;
	}

	if (strcmp(op, "BEGIN") == 0) {
		if (!uart_parse_u32(arg, &value)) {
			uart_nack(0, "CFG_SEQ");
			return;
		}
		if (uart_cfg_active) {
			uart_nack(value, "CFG_ACTIVE");
			return;
		}
		uart_cfg_seq = value;
		if (usb_image_config_begin() != 0) {
			Beamform_Stop(); uart_nack(value, "USB_DRAIN"); return;
		}
		Beamform_Stop();
        if(frame_store_pause()!=0) {
            const volatile frame_store_pause_diag_t *d=frame_store_pause_diagnostic();
            xil_printf("@DDR PAUSE reason=%u version=%08x seq=%u ack=%u result=%u flags=%08x\r\n",
                (unsigned)d->reason,(unsigned)d->version,(unsigned)d->sequence,
                (unsigned)d->ack,(unsigned)d->result,(unsigned)d->flags);
            xil_printf("@DDR PAUSE_DIAG axi=%08x parser=%08x input=%08x samples=%u age=%u\r\n",
                (unsigned)d->axi,(unsigned)d->parser,(unsigned)d->input,(unsigned)d->samples,(unsigned)d->age);
            const char *why=d->reason==FS_DRAIN_NOT_READY?"DDR_NOT_READY":
                d->reason==FS_DRAIN_ABI?"DDR_ABI":
                d->reason==FS_DRAIN_TIMEOUT?((d->flags&4U)?"DDR_DRAIN_BUSY":"DDR_DRAIN_TIMEOUT"):"DDR_DRAIN_RESULT";
			usb_image_config_end(); uart_nack(value,why); return;
        }
		uart_cfg_active = 1;
		cfg_owner=command_source; cfg_session=wifi_session; cfg_activity=command_ms();
		uart_cfg_done = 0;
		if(AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL)!=XST_SUCCESS) {
			uart_nack(uart_cfg_seq,"AFE_MAP_FAIL"); return;
		}
		uart_ack(uart_cfg_seq, "CFG_BEGIN");
		return;
	}

	if (strcmp(op, "END") == 0) {
		if (!uart_cfg_active) {
			uart_nack(uart_cfg_seq, "NO_CFG");
			return;
		}
		/* Optional legacy argument is ignored; END commits the configuration transaction. */
		if(AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL)!=XST_SUCCESS) {
			uart_nack(uart_cfg_seq,"AFE_MAP_FAIL"); return;
		}
		uart_cfg_active = 0;
		usb_image_config_end();
		uart_cfg_done = 1;
		if(command_source==SOURCE_WIFI) { hv_disable(); Beamform_Halt(); }
		else if(Beamform_Start()!=0) { uart_nack(uart_cfg_seq,"DDR_RESUME_OR_ABI"); return; }
		uart_ack(uart_cfg_seq, "CFG_END");
		return;
	}

	if (strcmp(op, "ABORT") == 0) {
		AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL);
		uart_cfg_active = 0;
		usb_image_config_end();
		uart_cfg_done = 0;
		Beamform_Stop();
		uart_ack(uart_cfg_seq, "CFG_ABORT");
		return;
	}

	uart_nack(uart_cfg_seq, "CFG_UNKNOWN");
}

static void uart_handle_afe(char **ctx)
{
	char *op = uart_next_token(ctx);
	char *map_text = uart_next_token(ctx);
	char *addr_text = uart_next_token(ctx);
	char *data_text = uart_next_token(ctx);
	u32 map;
	u32 addr;
	u32 data;
	u32 status;

	if (!uart_cfg_active) {
		uart_nack(uart_cfg_seq, "NO_CFG");
		return;
	}
	if (op == NULL || !uart_parse_map(map_text, &map) ||
			!uart_parse_u32(addr_text, &addr)) {
		uart_nack(uart_cfg_seq, "AFE_ARGS");
		return;
	}

	if (strcmp(op, "WR") == 0) {
		if (!uart_parse_u32(data_text, &data)) {
			uart_nack(uart_cfg_seq, "AFE_DATA");
			return;
		}
		status = AFE5832_WriteRegMap(map, addr, data);
		if (status == XST_SUCCESS) {
			uart_ack(uart_cfg_seq, "AFE_WR");
		} else {
			uart_nack(uart_cfg_seq, "AFE_WR_FAIL");
		}
		return;
	}

	if (strcmp(op, "RD") == 0) {
		status = AFE5832_ReadRegMap(map, addr, &data);
		if (status == XST_SUCCESS) {
			uart_emitf("@RD %u 0x%02x 0x%04x", uart_cfg_seq, addr & 0xFFU, data & 0xFFFFU);
		} else {
			uart_nack(uart_cfg_seq, "AFE_RD_FAIL");
		}
		return;
	}

	uart_nack(uart_cfg_seq, "AFE_UNKNOWN");
}

static const char *uart_bram_status_to_error(u32 status)
{
	switch (status) {
	case BRAM_CFG_STATUS_UNKNOWN:
		return "BRAM_SECTION";
	case BRAM_CFG_STATUS_RANGE:
		return "BRAM_RANGE";
	case BRAM_CFG_STATUS_ARGS:
	default:
		return "BRAM_ARGS";
	}
}

static void uart_handle_bram(char **ctx)
{
	char *op = uart_next_token(ctx);
	char *section = uart_next_token(ctx);
	char *offset_text = uart_next_token(ctx);
	char *count_text = uart_next_token(ctx);
	u32 offset;
	u32 count;
	u32 words[UART_BRAM_MAX_WORDS];
	u32 status;

	if (!uart_cfg_active) {
		uart_nack(uart_cfg_seq, "NO_CFG");
		return;
	}
	if (op == NULL || strcmp(op, "WR") != 0 || section == NULL ||
			!uart_parse_u32(offset_text, &offset) ||
			!uart_parse_u32(count_text, &count) ||
			count == 0U || count > UART_BRAM_MAX_WORDS) {
		uart_nack(uart_cfg_seq, "BRAM_ARGS");
		return;
	}

	for (u32 i = 0; i < count; i++) {
		char *word_text = uart_next_token(ctx);
		if (!uart_parse_u32(word_text, &words[i])) {
			uart_nack(uart_cfg_seq, "BRAM_DATA");
			return;
		}
	}

	status = Bram_WriteSectionWords(section, offset, words, count);
	if (status == BRAM_CFG_STATUS_OK) {
		uart_ack(uart_cfg_seq, "BRAM_WR");
	} else {
		uart_nack(uart_cfg_seq, uart_bram_status_to_error(status));
	}
}

static const char *uart_tx7332_status_to_error(u32 status)
{
	switch (status) {
	case BRAM_CFG_STATUS_UNKNOWN:
		return "TX7332_SECTION";
	case BRAM_CFG_STATUS_RANGE:
		return "TX7332_RANGE";
	case BRAM_CFG_STATUS_BUSY:
		return "BEAM_ACTIVE_USE_BEAM_STOP";
	case BRAM_CFG_STATUS_ARGS:
	default:
		return "TX7332_ARGS";
	}
}

static void uart_handle_tx7332(char **ctx)
{
	char *op = uart_next_token(ctx);
	char *section = uart_next_token(ctx);
	char *offset_text = uart_next_token(ctx);
	char *count_text = uart_next_token(ctx);
	u32 offset;
	u32 count;
	u32 words[UART_BRAM_MAX_WORDS];
	u32 status;

	if (op != NULL && strcmp(op, "RD") == 0) {
		u32 addr;
		u32 data;

		if (section == NULL || !uart_parse_u32(section, &addr)) {
			uart_nack(uart_cfg_seq, "TX7332_RD_ARGS");
			return;
		}

		status = Tx7332_ReadRegister(addr, &data);
		if (status == BRAM_CFG_STATUS_OK) {
			uart_emitf("@RD %u TX7332 0x%03x 0x%08x", uart_cfg_seq, addr & 0x3FFU, data);
		} else {
			uart_nack(uart_cfg_seq, uart_tx7332_status_to_error(status));
		}
		return;
	}

	if (!uart_cfg_active) {
		uart_nack(uart_cfg_seq, "NO_CFG");
		return;
	}
	if (op == NULL || strcmp(op, "WR") != 0 || section == NULL ||
			!uart_parse_u32(offset_text, &offset) ||
			!uart_parse_u32(count_text, &count) ||
			count == 0U || count > UART_BRAM_MAX_WORDS) {
		uart_nack(uart_cfg_seq, "TX7332_ARGS");
		return;
	}

	for (u32 i = 0; i < count; i++) {
		char *word_text = uart_next_token(ctx);
		if (!uart_parse_u32(word_text, &words[i])) {
			uart_nack(uart_cfg_seq, "TX7332_DATA");
			return;
		}
	}

	status = Tx7332_WriteSectionWords(section, offset, words, count);
	if (status == BRAM_CFG_STATUS_OK) {
		uart_ack(uart_cfg_seq, "TX7332_WR");
	} else {
		uart_nack(uart_cfg_seq, uart_tx7332_status_to_error(status));
	}
}
static void uart_handle_gain(char **ctx)
{
	char *op = uart_next_token(ctx);
	char *value_text = uart_next_token(ctx);
	u32 value;

	if (op == NULL) {
		uart_nack(uart_cfg_seq, "GAIN_ARGS");
		return;
	}
	/* Deliberately NOT gated on uart_cfg_active. The point of this command is to
	 * trim receive gain while imaging keeps running: para_st latches scan-timing
	 * word 12 and filter_iq picks it up on the next sample, so nothing has to be
	 * restarted and HV stays up. Digital gain amplifies quantisation noise along
	 * with the signal, so raise the AFE LNA/PGA/DTGC first and use this for trim. */
	if (strcmp(op, "SET") == 0) {
		if (!uart_parse_u32(value_text, &value)) {
			uart_nack(uart_cfg_seq, "GAIN_VALUE");
			return;
		}
		value = Rx_SetGainShift(value);
		uart_emitf("@ACK %u GAIN_SET %u", uart_cfg_seq, value);
		return;
	}
	uart_nack(uart_cfg_seq, "GAIN_UNKNOWN");
}

static void uart_handle_beam(char **ctx)
{
	char *op = uart_next_token(ctx);

	if (op == NULL) {
		uart_nack(uart_cfg_seq, "BEAM_ARGS");
		return;
	}
	if (strcmp(op, "START") == 0) {
		if (uart_cfg_active) {
			uart_nack(uart_cfg_seq, "CFG_ACTIVE");
			return;
		}
		if(Beamform_Resume()!=0) { uart_nack(uart_cfg_seq,"DDR_RESUME_OR_ABI"); return; }
		uart_ack(uart_cfg_seq, "BEAM_START");
		return;
	}
	if (strcmp(op, "STOP") == 0) {
		Beamform_Pause();
		uart_ack(uart_cfg_seq, "BEAM_STOP");
		return;
	}
	uart_nack(uart_cfg_seq, "BEAM_UNKNOWN");
}

static void uart_handle_hv(char **ctx)
{
	char *op = uart_next_token(ctx);

	if (op == NULL) {
		uart_nack(uart_cfg_seq, "HV_ARGS");
		return;
	}
	if (strcmp(op, "ENABLE") == 0) {
		if (uart_cfg_active) {
			uart_nack(uart_cfg_seq, "CFG_ACTIVE");
			return;
		}
		hv_enable();
		if (!hv_is_enabled()) {
			uart_nack(uart_cfg_seq, "HV_ENABLE_FAILED");
			return;
		}
		uart_ack(uart_cfg_seq, "HV_ENABLE");
		return;
	}
	if (strcmp(op, "DISABLE") == 0) {
		hv_disable();
		/* Complete stop: the next BEAM START must create a spi_start edge. */
		Beamform_Halt();
		if (hv_is_enabled()) {
			uart_nack(uart_cfg_seq, "HV_DISABLE_FAILED");
			return;
		}
		uart_ack(uart_cfg_seq, "HV_DISABLE");
		return;
	}
	uart_nack(uart_cfg_seq, "HV_UNKNOWN");
}

static void uart_handle_line(char *line)
{
    if(!strcmp(line,"CPU1 DISABLE")) {
        if(command_source!=SOURCE_WIFI || uart_cfg_active){uart_nack(uart_cfg_seq,"CPU1_OWNER_OR_CFG");return;}
        const char *error=cpu1_power_disable();cpu0_idle_report_request();
        if(error)uart_emitf("@NACK %u CPU1_%s",uart_cfg_seq,error);
        else uart_emitf("@ACK %u CPU1_DISABLE cpu1_disabled=%u",uart_cfg_seq,cpu1_power_is_disabled());
        return;
    }
    if(!strcmp(line,"TX7332 STATUS")) {
        uart_emitf("@TX state=%s flags=%08x idle=%u error=%s",tx_power_state_name(),(unsigned)tx_power_flags(),(unsigned)tx_power_idle(),tx_power_error());return;
    }
    if(!strcmp(line,"TX7332 PREPARE")||!strcmp(line,"TX7332 SLEEP")||!strcmp(line,"TX7332 WAKE")) {
        if(command_source!=SOURCE_WIFI||uart_cfg_active){uart_nack(uart_cfg_seq,"TX_OWNER_OR_CFG");return;}
        if(tx_power_command(line+7,wifi_session)){uart_nack(uart_cfg_seq,tx_power_error());return;}
        uart_emitf("@ACK %u TX_%s %s",uart_cfg_seq,line+7,tx_power_state_name());return;
    }
    /* AFE status is independent of configuration ownership and never wakes it. */
    if(!strcmp(line,"AFE STATUS")) {
        uart_emitf("@AFE state=%s flags=%08x idle=%u error=%s",afe_power_state_name(),(unsigned)afe_power_flags(),(unsigned)afe_power_idle(),afe_power_error());return;
    }
    if(!strcmp(line,"AFE PREPARE") || !strcmp(line,"AFE SLEEP") || !strcmp(line,"AFE WAKE")) {
        if(command_source!=SOURCE_WIFI || uart_cfg_active) {uart_nack(uart_cfg_seq,"AFE_OWNER_OR_CFG");return;}
        const char *op=line+4;
        if(afe_power_command(op,wifi_session)!=0) {uart_nack(uart_cfg_seq,afe_power_error());return;}
        uart_emitf("@ACK %u AFE_%s %s",uart_cfg_seq,op,afe_power_state_name());return;
    }
    if(!imaging_power_allowed() && strcmp(line,"HV DISABLE") && strcmp(line,"BEAM STOP") && strcmp(line,"STATUS")) {
        uart_nack(uart_cfg_seq,afe_power_allowed()?"TX_NOT_READY":"AFE_NOT_READY");return;
    }
	/* Other inputs cannot enter this transaction. Stop/off remain available. */
	if(uart_cfg_active && !owns_cfg() && strcmp(line,"HV DISABLE") &&
	   strcmp(line,"BEAM STOP") && strcmp(line,"STATUS")) {
		uart_emitf("@NACK %u CFG_OWNER",uart_cfg_seq); return;
	}
	if(!strcmp(line,"STATUS")) {
		cpu0_idle_report_request();
		uart_emitf("@STATUS v=1 cfg=%u owner=%u seq=%u run=%u hv=%u run_src=ps spi=%u down=%u afe=%s tx=%s cpu1_disabled=%u wfi=%u",uart_cfg_active,
		    cfg_owner,uart_cfg_seq,(unsigned)Beamform_GetRunRequest(),(unsigned)hv_is_enabled(),
		    (unsigned)Beamform_GetSpiStartRequest(),(unsigned)Beamform_GetDownsample(),afe_power_state_name(),tx_power_state_name(),cpu1_power_is_disabled(),cpu0_idle_enabled());
		return;
	}
	char *ctx = line;
	char *cmd = uart_next_token(&ctx);

	if (cmd == NULL) {
		return;
	}
	if (strcmp(cmd, "CFG") == 0) {
		uart_handle_cfg(&ctx);
		return;
	}
	if (strcmp(cmd, "AFE") == 0) {
		uart_handle_afe(&ctx);
		return;
	}
	if (strcmp(cmd, "BRAM") == 0) {
		uart_handle_bram(&ctx);
		return;
	}
	if (strcmp(cmd, "TX7332") == 0) {
		uart_handle_tx7332(&ctx);
		return;
	}
	if (strcmp(cmd, "BEAM") == 0) {
		uart_handle_beam(&ctx);
		return;
	}
	if (strcmp(cmd, "HV") == 0) {
		uart_handle_hv(&ctx);
		return;
	}
	if (strcmp(cmd, "GAIN") == 0) {
		uart_handle_gain(&ctx);
		return;
	}
	uart_nack(uart_cfg_seq, "UNKNOWN_CMD");
}
void uart_handle_usb_line(const char *line, command_response_fn response_fn)
{
	char local_line[UART_LINE_MAX];
	command_response_fn previous_response_fn = uart_response_fn;
	u32 i = 0;

	uart_response_fn = response_fn;
	unsigned previous_source=command_source; command_source=SOURCE_USB;
	if (line == NULL) {
		uart_nack(uart_cfg_seq, "USB_LINE");
		uart_response_fn = previous_response_fn;
		command_source=previous_source;
		return;
	}

	while (line[i] != '\0' && i < (UART_LINE_MAX - 1U)) {
		local_line[i] = line[i];
		i++;
	}
	local_line[i] = '\0';
	if (line[i] != '\0') {
		uart_nack(uart_cfg_seq, "USB_LINE");
	} else {
		uart_handle_line(local_line);
	}
	uart_response_fn = previous_response_fn;
	command_source=previous_source;
}

void uart_handle_wifi_line(const char *line,u32 session,command_response_fn response_fn)
{
    command_response_fn previous=uart_response_fn;
    unsigned source=command_source;
    command_source=SOURCE_WIFI; wifi_session=session; uart_response_fn=response_fn;
    const char *error=wc_validate(line);
    if(error) uart_nack(uart_cfg_seq,error);
    else {
        char copy[UART_LINE_MAX]; strcpy(copy,line);
        uart_handle_line(copy);
        if(uart_cfg_active && owns_cfg()) cfg_activity=command_ms();
    }
    uart_response_fn=previous; command_source=source;
}

u32 uart_init(void) {
	u32 res;
	XUartPs_Config *cfg;
	cfg = XUartPs_LookupConfig(UART_BASEADDR);
	if (cfg == NULL || cfg->BaseAddress != UART_BASEADDR) {
		return XST_FAILURE;
	}
	res = XUartPs_CfgInitialize(&uartps, cfg, cfg->BaseAddress);
	if (res != XST_SUCCESS) {
		return XST_FAILURE;
	}
	res = XUartPs_SelfTest(&uartps);
	if (res != XST_SUCCESS) {
		return XST_FAILURE;
	}
	XUartPs_SetOperMode(&uartps, XUARTPS_OPER_MODE_NORMAL);
	XUartPs_SetBaudRate(&uartps, 460800);
	XUartPs_SetFifoThreshold(&uartps, 16);
	XUartPs_SetRecvTimeout(&uartps, 4);
	res = setup_uart_intr_system();
	if (res != XST_SUCCESS) {
		return XST_FAILURE;
	}
	if(zynq_uart_command_init()!=XST_SUCCESS) return XST_FAILURE;

	return XST_SUCCESS;
}

void uart_process_commands(void)
{
	afe_power_poll();
    tx_power_poll();
	char line[UART_LINE_MAX];
	if(uart_cfg_active && cfg_owner==SOURCE_WIFI && command_ms()-cfg_activity>=60000U) {
		wifi_abort(); xil_printf("@WIFI_CFG_TIMEOUT stopped; HV disabled\r\n");
	}
	zynq_uart_command_poll();

	if (uart_line_overflow) {
		uart_line_overflow = 0;
		uart_nack(uart_cfg_seq, "UART_OVERFLOW");
	}
	while (uart_pop_line(line)) {
		uart_handle_line(line);
	}
}

int uart_wait_config_done(void)
{
	uart_cfg_done = 0;
	xil_printf("@READY CONFIG\r\n");
	while (!uart_cfg_done) {
		uart_process_commands();
	}
	return XST_SUCCESS;
}

int uart_wait_afe_config_done(void)
{
	return uart_wait_config_done();
}

void uart_intrhandler(void *callbackref) {
	u32 isrStatus;
	XUartPs* uart_ps_handle = (XUartPs *) callbackref;

	isrStatus = XUartPs_ReadReg(uart_ps_handle->Config.BaseAddress,
			XUARTPS_IMR_OFFSET);
	isrStatus &= XUartPs_ReadReg(uart_ps_handle->Config.BaseAddress,
			XUARTPS_ISR_OFFSET);

	if ((isrStatus & (XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT)) != 0U) {
		uart_drain_rx_fifo(uart_ps_handle);
	}

	XUartPs_WriteReg(uart_ps_handle->Config.BaseAddress, XUARTPS_ISR_OFFSET,
			isrStatus);
}

u32 setup_uart_intr_system(void) {
	u32 status;

	status = XScuGic_Connect(&xInterruptController, UART_INT_IRQ_ID,
			(Xil_ExceptionHandler) uart_intrhandler, (void *) &uartps);
	XUartPs_SetInterruptMask(&uartps, XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	XScuGic_Enable(&xInterruptController, UART_INT_IRQ_ID);
	XScuGic_InterruptMaptoCpu(&xInterruptController, 0U, /* CPU number, not a target bit mask */
	UART_INT_IRQ_ID);

	DEBUG_PRINT(" uart intr init \r\n");

	return XST_SUCCESS;
}
