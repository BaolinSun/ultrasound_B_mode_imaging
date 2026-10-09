#include "uart.h"
#include "xscugic.h"
#include "xinterrupt_wrap.h"
#include "xstatus.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "../AFE5832/afe5832.h"
#include "../bram/bram.h"
#include "../HV/hv.h"
#include "../data_transfer/sdio_bridge.h"

#define UART_BASEADDR       XPAR_XUARTPS_0_BASEADDR
#define UART_INT_IRQ_ID \
	((u32)(XGet_IntrId(XPAR_XUARTPS_0_INTERRUPTS) + \
		XGet_IntrOffset(XPAR_XUARTPS_0_INTERRUPTS)))

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
static command_response_fn uart_response_fn = NULL;

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
		Beamform_Stop();
		uart_cfg_active = 1;
		uart_cfg_done = 0;
		AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL);
		uart_ack(uart_cfg_seq, "CFG_BEGIN");
		return;
	}

	if (strcmp(op, "END") == 0) {
		if (!uart_cfg_active) {
			uart_nack(uart_cfg_seq, "NO_CFG");
			return;
		}
		/* Optional legacy argument is ignored; END commits the configuration transaction. */
		AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL);
		uart_cfg_active = 0;
		uart_cfg_done = 1;
		Beamform_Start();
		uart_ack(uart_cfg_seq, "CFG_END");
		return;
	}

	if (strcmp(op, "ABORT") == 0) {
		AFE5832_SetRegMap(AFE5832_REG_MAP_NORMAL);
		uart_cfg_active = 0;
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
		Beamform_Resume();
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
	char *ctx = line;
	char *cmd = uart_next_token(&ctx);

	if (cmd == NULL) {
		return;
	}
	if (strcmp(cmd, "OUTPUT") == 0) {
		char *mode = uart_next_token(&ctx);
		if (mode && strcmp(mode,"STATUS")==0) { sdio_bridge_print_status(); uart_ack(0,"OUTPUT_STATUS"); }
		else if (mode && (strcmp(mode,"USB")==0 || strcmp(mode,"SDIO")==0)) {
			sdio_bridge_set_output(strcmp(mode,"SDIO")==0);
			uart_ack(0,strcmp(mode,"SDIO")==0?"OUTPUT_SDIO":"OUTPUT_USB");
		} else uart_nack(0,"OUTPUT_ARGS");
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
	if (line == NULL) {
		uart_nack(uart_cfg_seq, "USB_LINE");
		uart_response_fn = previous_response_fn;
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
}

u32 uart_init(void) {
	u32 res;
	XUartPs_Config *cfg;
	cfg = XUartPs_LookupConfig(UART_BASEADDR);
	if (cfg == NULL) {
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

	return XST_SUCCESS;
}

void uart_process_commands(void)
{
	char line[UART_LINE_MAX];

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
	XScuGic_InterruptMaptoCpu(&xInterruptController, XSCUGIC_SPI_CPU1_MASK,
	UART_INT_IRQ_ID);

	DEBUG_PRINT(" uart intr init \r\n");

	return XST_SUCCESS;
}
