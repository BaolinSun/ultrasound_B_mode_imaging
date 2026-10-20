#include "sdio_host.h"
#include "sdio_cmd_engine.h"

#include <stddef.h>
#include <string.h>

#include "sleep.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xsdps_core.h"
#include "xsdps_hw.h"
#include "xiltimer.h"

/* SDT lookup uses physical base addresses, never the generated instance index:
 * SD0 is the boot card and must NOT be touched by this driver. */
#define SDIO_HOST_BASEADDR ((UINTPTR)0xE0101000U)

#define SDIO_FN1                    1u
#define SDIO_BLOCK_SIZE             US_SDIO_BLOCK_BYTES
#define SDIO_DATA_PORT_ADDR         0x1F800u
#define SDIO_INIT_CLOCK_HZ          400000u
#ifndef SDIO_RUN_CLOCK_HZ
#define SDIO_RUN_CLOCK_HZ           25000000u
#endif
#define SDIO_CMD53_USE_PIO_WRITE    0u
#define SDIO_CMD53_FIRST_BLOCK_PIO_WARMUP 0u
#define SDIO_CMD53_DMA_FALLBACK_PIO 0u
#define SDIO_CMD53_RELAXED_RESP     1u
#define SDIO_CMD53_LOG_FIRST        0u
#define SDIO_CMD53_LOG_EVERY        0u

#define SDIO_REG_DEVICE_ID          2u
#define SDIO_REG_STATUS             3u
#define SDIO_REG_WINDOW_PACKETS     4u
#define SDIO_REG_WINDOW_GRANT_SEQ   5u
#define SDIO_REG_WINDOW_ACK_SEQ     6u

#define SDIO_DEVICE_ID_ESP32C61     0xC6u
#define SDIO_STATUS_READY           0x01u
#define SDIO_STATUS_STREAM_ENABLE   0x10u
#define SDIO_STATUS_BACKPRESSURE    0x20u

#define SDIO_CCCR_IO_ENABLE         0x02u
#define SDIO_CCCR_IO_READY          0x03u
#define SDIO_CCCR_IF                0x07u
#define SDIO_CCCR_FN0_BLKSIZE_LO    0x10u
#define SDIO_FBR1_BLKSIZE_LO        0x110u

#define SDIO_CMD5_R4                (0x0500u | RESP_R3)
#define SDIO_CMD3_R6                (0x0300u | RESP_R6)
#if SDIO_CMD53_RELAXED_RESP
#define SDIO_CMD53_R5_DATA          (0x3500u | RESP_R3 | (u32)XSDPS_DAT_PRESENT_SEL_MASK)
#else
#define SDIO_CMD53_R5_DATA          (0x3500u | RESP_R1 | (u32)XSDPS_DAT_PRESENT_SEL_MASK)
#endif
#define SDIO_OCR_3V3                0x00ff8000u
#define SDIO_OCR_READY              0x80000000u

static u32 s_cmd53_write_count;
static u32 s_cmd53_dma_blocks;
static u32 s_cmd53_pio_warmup_blocks;
static u32 s_cmd53_pio_fallback_blocks;
static u32 s_cmd53_dma_error_blocks;
static sdio_host_timing_t s_cmd53_timing;

static u32 esp32_shared_reg_addr(u32 logical_addr)
{
    if (logical_addr > 31u) {
        logical_addr += 12u;
    }
    if (logical_addr > 23u) {
        logical_addr += 4u;
    }

    return 0x6Cu + logical_addr;
}

static int sdio_cmd(sdio_host_t *host, u32 cmd, u32 arg, u32 blk_count);
static int sdio_fast_cmd(sdio_host_t *host, u32 cmd, u32 arg, u32 blk_count, u8 data_events_ok);
static int sdio_cmd52_fast_cmd(sdio_host_t *host, u32 arg);
static int sdio_cmd53_fast_cmd(sdio_host_t *host, u32 arg, u32 blk_count);

static u32 sdio_elapsed_ms(XTime start, XTime end)
{
    return (u32)(((u64)(end - start) * 1000u) / (u64)(COUNTS_PER_SECOND));
}

void sdio_host_take_timing(sdio_host_timing_t *timing)
{
    if (timing == NULL) {
        return;
    }

    *timing = s_cmd53_timing;
    (void)memset(&s_cmd53_timing, 0, sizeof(s_cmd53_timing));
}

static void sdio_config_dma_preserve_bus(sdio_host_t *host)
{
    u32 base = host->sd.Config.BaseAddress;
    u8 host_ctrl = XSdPs_ReadReg8(base, XSDPS_HOST_CTRL1_OFFSET);

    host_ctrl &= (u8)~XSDPS_HC_DMA_ADMA2_64_MASK;
    if (host->sd.HC_Version == XSDPS_HC_SPEC_V3) {
        host_ctrl |= (u8)XSDPS_HC_DMA_ADMA2_64_MASK;
    } else {
        host_ctrl |= (u8)XSDPS_HC_DMA_ADMA2_32_MASK;
    }
    XSdPs_WriteReg8(base, XSDPS_HOST_CTRL1_OFFSET, host_ctrl);
}

static u8 sdio_should_log_cmd53(u32 write_count)
{
    if (write_count <= SDIO_CMD53_LOG_FIRST) {
        return 1u;
    }
#if SDIO_CMD53_LOG_EVERY == 0
    return 0u;
#else
    return ((write_count % SDIO_CMD53_LOG_EVERY) == 0u) ? 1u : 0u;
#endif
}

static void sdio_print_stats_if_due(const char *tag, u32 write_count)
{
    if (write_count == 0u || sdio_should_log_cmd53(write_count)) {
        sdio_host_print_stats(tag);
    }
}

void sdio_host_print_stats(const char *tag)
{
    xil_printf("[ZYNQ][SDIO] CMD53 stats %s total=%u dma=%u pio_warmup=%u pio_fallback=%u dma_error=%u\r\n",
               (tag != NULL) ? tag : "",
               s_cmd53_write_count,
               s_cmd53_dma_blocks,
               s_cmd53_pio_warmup_blocks,
               s_cmd53_pio_fallback_blocks,
               s_cmd53_dma_error_blocks);
}

static void sdio_clear_intr_status(sdio_host_t *host)
{
    u32 base = host->sd.Config.BaseAddress;

    XSdPs_WriteReg16(base, XSDPS_NORM_INTR_STS_OFFSET, XSDPS_NORM_INTR_ALL_MASK);
    XSdPs_WriteReg16(base, XSDPS_ERR_INTR_STS_OFFSET, XSDPS_ERROR_INTR_ALL_MASK);
}

static void sdio_dump_regs(sdio_host_t *host, const char *tag, u32 cmd, u32 arg, u32 blk_count)
{
    u32 base = host->sd.Config.BaseAddress;
    u32 resp0 = XSdPs_ReadReg(base, XSDPS_RESP0_OFFSET);
    u32 psr = XSdPs_ReadReg(base, XSDPS_PRES_STATE_OFFSET);

    xil_printf("[ZYNQ][SDIO] %s cmd=0x%08x arg=0x%08x blk=%u resp0=0x%08x\r\n",
               tag,
               cmd,
               arg,
               blk_count,
               resp0);
    xil_printf("[ZYNQ][SDIO] %s norm=0x%04x err=0x%04x psr=0x%08x xfer=0x%04x blk_size=0x%04x blk_cnt=0x%04x\r\n",
               tag,
               XSdPs_ReadReg16(base, XSDPS_NORM_INTR_STS_OFFSET),
               XSdPs_ReadReg16(base, XSDPS_ERR_INTR_STS_OFFSET),
               psr,
               XSdPs_ReadReg16(base, XSDPS_XFER_MODE_OFFSET),
               XSdPs_ReadReg16(base, XSDPS_BLK_SIZE_OFFSET),
               XSdPs_ReadReg16(base, XSDPS_BLK_CNT_OFFSET));
    xil_printf("[ZYNQ][SDIO] %s host_ctrl1=0x%02x adma_err=0x%02x adma_sar=0x%08x sdma_addr=0x%08x\r\n",
               tag,
               XSdPs_ReadReg8(base, XSDPS_HOST_CTRL1_OFFSET),
               XSdPs_ReadReg8(base, XSDPS_ADMA_ERR_STS_OFFSET),
               XSdPs_ReadReg(base, XSDPS_ADMA_SAR_OFFSET),
               XSdPs_ReadReg(base, XSDPS_SDMA_SYS_ADDR_OFFSET));
    xil_printf("[ZYNQ][SDIO] %s cmd_inhibit=%u dat_inhibit=%u dat_active=%u wr_active=%u bwr_en=%u dat[3:0]=0x%x cmd_lvl=%u\r\n",
               tag,
               psr & 1u,
               (psr >> 1) & 1u,
               (psr >> 2) & 1u,
               (psr >> 8) & 1u,
               (psr >> 10) & 1u,
               (psr >> 20) & 0x0fu,
               (psr >> 24) & 1u);
}

static void sdio_reset_cmd_dat_lines(sdio_host_t *host, const char *reason)
{
    u32 base = host->sd.Config.BaseAddress;
    u32 retry;

    xil_printf("[ZYNQ][SDIO] reset CMD/DAT after %s psr=0x%08x\r\n",
               reason,
               XSdPs_ReadReg(base, XSDPS_PRES_STATE_OFFSET));
    XSdPs_WriteReg8(base,
                    XSDPS_SW_RST_OFFSET,
                    XSDPS_SWRST_CMD_LINE_MASK | XSDPS_SWRST_DAT_LINE_MASK);
    for (retry = 0u; retry < 1000u; ++retry) {
        if ((XSdPs_ReadReg8(base, XSDPS_SW_RST_OFFSET) &
             (XSDPS_SWRST_CMD_LINE_MASK | XSDPS_SWRST_DAT_LINE_MASK)) == 0u) {
            break;
        }
        usleep(10u);
    }
    sdio_clear_intr_status(host);
    xil_printf("[ZYNQ][SDIO] reset CMD/DAT done retry=%u psr=0x%08x\r\n",
               retry,
               XSdPs_ReadReg(base, XSDPS_PRES_STATE_OFFSET));
}

static int sdio_wait_bus_idle(sdio_host_t *host, u8 require_dat_idle, u32 timeout_us, const char *tag)
{
    u32 base = host->sd.Config.BaseAddress;
    u32 mask = XSDPS_PSR_INHIBIT_CMD_MASK;

    if (require_dat_idle != 0u) {
        mask |= XSDPS_PSR_INHIBIT_DAT_MASK;
    }

    while (timeout_us != 0u) {
        if ((XSdPs_ReadReg(base, XSDPS_PRES_STATE_OFFSET) & mask) == 0u) {
            return SDIO_HOST_OK;
        }
        usleep(1u);
        timeout_us--;
    }

    sdio_dump_regs(host, tag, SDIO_CMD53_R5_DATA, 0u, 0u);
    sdio_reset_cmd_dat_lines(host, tag);
    return SDIO_HOST_ERR_TIMEOUT;
}

static int sdio_wait_intr(sdio_host_t *host, u16 mask, const char *tag)
{
    u32 base = host->sd.Config.BaseAddress;
    XTime started, now;
    XTime_GetTime(&started);
    u32 timeout = 2000000u;

    while (timeout != 0u) {
        u16 norm = XSdPs_ReadReg16(base, XSDPS_NORM_INTR_STS_OFFSET);

        if ((norm & XSDPS_INTR_ERR_MASK) != 0u) {
            sdio_dump_regs(host, tag, SDIO_CMD53_R5_DATA, 0u, 0u);
            return SDIO_HOST_ERR_TRANSFER;
        }
        if ((mask & XSDPS_INTR_BWR_MASK) != 0u) {
            u32 psr = XSdPs_ReadReg(base, XSDPS_PRES_STATE_OFFSET);

            if ((psr & XSDPS_PSR_BUFF_WR_EN_MASK) != 0u) {
                return SDIO_HOST_OK;
            }
        }
        if ((mask & XSDPS_INTR_TC_MASK) != 0u) {
            u32 psr = XSdPs_ReadReg(base, XSDPS_PRES_STATE_OFFSET);
            u32 busy = XSDPS_PSR_INHIBIT_CMD_MASK |
                       XSDPS_PSR_INHIBIT_DAT_MASK |
                       XSDPS_PSR_DAT_ACTIVE_MASK |
                       XSDPS_PSR_WR_ACTIVE_MASK |
                       XSDPS_PSR_BUFF_WR_EN_MASK;

            if ((psr & busy) == 0u) {
                return SDIO_HOST_OK;
            }
        }
        if ((norm & mask) != 0u) {
            return SDIO_HOST_OK;
        }
        XTime_GetTime(&now);
        if (now-started > COUNTS_PER_SECOND/10U) break;
        timeout--;
    }

    sdio_dump_regs(host, tag, SDIO_CMD53_R5_DATA, 0u, 0u);
    return SDIO_HOST_ERR_TIMEOUT;
}

static int sdio_pio_write_block(sdio_host_t *host, u32 arg, const u8 *block, u8 trace)
{
    u32 base = host->sd.Config.BaseAddress;
    int ret;

    ret = sdio_wait_bus_idle(host, 1u, 10000u, "CMD53_PIO_PRE_IDLE");
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    XSdPs_WriteReg16(base, XSDPS_BLK_SIZE_OFFSET, US_SDIO_BLOCK_BYTES);
    XSdPs_WriteReg16(base, XSDPS_BLK_CNT_OFFSET, 1u);
    host->sd.TransferMode = XSDPS_TM_BLK_CNT_EN_MASK;

    sdio_clear_intr_status(host);
    ret = sdio_cmd53_fast_cmd(host, arg, 1u);
    if (ret != SDIO_HOST_OK) {
        sdio_dump_regs(host, "CMD53_PIO_CMD_FAIL", SDIO_CMD53_R5_DATA, arg, 1u);
        sdio_reset_cmd_dat_lines(host, "CMD53 PIO command failure");
        return ret;
    }

    ret = sdio_wait_intr(host, XSDPS_INTR_BWR_MASK, "CMD53_PIO_WAIT_BWR");
    if (ret != SDIO_HOST_OK) {
        sdio_reset_cmd_dat_lines(host, "CMD53 PIO buffer write wait failure");
        return ret;
    }

    for (u32 i = 0u; i < US_SDIO_BLOCK_BYTES; i += 4u) {
        u32 word = ((u32)block[i]) |
                   ((u32)block[i + 1u] << 8) |
                   ((u32)block[i + 2u] << 16) |
                   ((u32)block[i + 3u] << 24);
        XSdPs_WriteReg(base, XSDPS_BUF_DAT_PORT_OFFSET, word);
    }
    XSdPs_WriteReg16(base, XSDPS_NORM_INTR_STS_OFFSET, XSDPS_INTR_BWR_MASK);

    ret = sdio_wait_intr(host, XSDPS_INTR_TC_MASK, "CMD53_PIO_WAIT_TC");
    if (ret != SDIO_HOST_OK) {
        sdio_reset_cmd_dat_lines(host, "CMD53 PIO transfer complete failure");
        return ret;
    }
    XSdPs_WriteReg16(base, XSDPS_NORM_INTR_STS_OFFSET, XSDPS_INTR_TC_MASK);

    if (trace != 0u) {
        xil_printf("[ZYNQ][SDIO] CMD53 PIO write complete resp0=0x%08x psr=0x%08x\r\n",
                   XSdPs_ReadReg(base, XSDPS_RESP0_OFFSET),
                   XSdPs_ReadReg(base, XSDPS_PRES_STATE_OFFSET));
    }
    return SDIO_HOST_OK;
}

static int sdio_cmd(sdio_host_t *host, u32 cmd, u32 arg, u32 blk_count)
{
    if (((cmd & (u32)XSDPS_DAT_PRESENT_SEL_MASK) == 0u) && (blk_count == 0u)) {
        host->sd.TransferMode = 0u;
    }

    return sdio_fast_cmd(host,cmd,arg,blk_count,0u);
}

static uint32_t cmd_read32(void *ctx,unsigned p) { return XSdPs_ReadReg(((sdio_host_t*)ctx)->sd.Config.BaseAddress,p); }
static uint16_t cmd_read16(void *ctx,unsigned p) { return XSdPs_ReadReg16(((sdio_host_t*)ctx)->sd.Config.BaseAddress,p); }
static void cmd_write32(void *ctx,unsigned p,uint32_t v) { XSdPs_WriteReg(((sdio_host_t*)ctx)->sd.Config.BaseAddress,p,v); }
static void cmd_write16(void *ctx,unsigned p,uint16_t v) { XSdPs_WriteReg16(((sdio_host_t*)ctx)->sd.Config.BaseAddress,p,v); }
static void cmd_write8(void *ctx,unsigned p,uint8_t v) { XSdPs_WriteReg8(((sdio_host_t*)ctx)->sd.Config.BaseAddress,p,v); }
static uint64_t cmd_time(void *ctx) {
    XTime t; (void)ctx; XTime_GetTime(&t);
    return sdio_ticks_us((u64)t,(u64)(COUNTS_PER_SECOND));
}

static int sdio_fast_cmd(sdio_host_t *host, u32 cmd, u32 arg, u32 blk_count, u8 data_events_ok)
{
    const sdio_cmd_io_t io={host,cmd_read32,cmd_read16,cmd_write32,cmd_write16,cmd_write8,cmd_time};
    int result=sdio_cmd_execute(&io,XSdPs_FrameCmd(&host->sd,cmd),arg,(u16)blk_count,
                               host->sd.TransferMode,data_events_ok,100000U);
    if(result!=SDIO_HOST_OK) {
        sdio_dump_regs(host,result==SDIO_HOST_ERR_TIMEOUT?"COMMAND_TIMEOUT":"COMMAND_ERROR",cmd,arg,blk_count);
        sdio_reset_cmd_dat_lines(host,"bounded command failure");
    }
    return result;
}

static int sdio_cmd52_fast_cmd(sdio_host_t *host, u32 arg)
{
    XTime start;
    XTime end;
    int ret;

    host->sd.TransferMode = 0u;
    XTime_GetTime(&start);
    ret = sdio_fast_cmd(host, CMD52, arg, 0u, 0u);
    XTime_GetTime(&end);
    s_cmd53_timing.cmd52_ms += sdio_elapsed_ms(start, end);

    return ret;
}

static int sdio_cmd53_fast_cmd(sdio_host_t *host, u32 arg, u32 blk_count)
{
    return sdio_fast_cmd(host, SDIO_CMD53_R5_DATA, arg, blk_count, 1u);
}

static u32 sdio_resp0(sdio_host_t *host)
{
    return XSdPs_ReadReg(host->sd.Config.BaseAddress, XSDPS_RESP0_OFFSET);
}

static int sdio_cmd52(sdio_host_t *host, u8 write, u8 fn, u32 addr, u8 wr_val, u8 *rd_val)
{
    u32 arg = ((u32)(write & 1u) << 31) |
              ((u32)(fn & 7u) << 28) |
              ((addr & 0x1FFFFu) << 9) |
              (u32)wr_val;
    int ret = sdio_cmd52_fast_cmd(host, arg);

    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    if (rd_val != NULL) {
        *rd_val = (u8)(sdio_resp0(host) & 0xffu);
    }

    return SDIO_HOST_OK;
}

int sdio_write_reg(sdio_host_t *host, u32 logical_addr, u8 value)
{
    return sdio_cmd52(host, 1u, host->function_num, esp32_shared_reg_addr(logical_addr), value, NULL);
}

int sdio_read_reg(sdio_host_t *host, u32 logical_addr, u8 *value)
{
    return sdio_cmd52(host, 0u, host->function_num, esp32_shared_reg_addr(logical_addr), 0u, value);
}

static int sdio_set_block_size(sdio_host_t *host, u8 fn, u16 block_size)
{
    u32 base = (fn == 0u) ? SDIO_CCCR_FN0_BLKSIZE_LO : SDIO_FBR1_BLKSIZE_LO;
    int ret;

    ret = sdio_cmd52(host, 1u, 0u, base, (u8)(block_size & 0xffu), NULL);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    return sdio_cmd52(host, 1u, 0u, base + 1u, (u8)((block_size >> 8) & 0xffu), NULL);
}

static int sdio_enable_fn1(sdio_host_t *host)
{
    u8 ready = 0;
    int ret = sdio_cmd52(host, 1u, 0u, SDIO_CCCR_IO_ENABLE, 0x02u, NULL);

    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    for (u32 i = 0u; i < 1000u; ++i) {
        ret = sdio_cmd52(host, 0u, 0u, SDIO_CCCR_IO_READY, 0u, &ready);
        if(ret!=SDIO_HOST_OK) return ret;
        if (ret == SDIO_HOST_OK && (ready & 0x02u) != 0u) {
            return SDIO_HOST_OK;
        }
        usleep(1000u);
    }

    return SDIO_HOST_ERR_TIMEOUT;
}

static int sdio_force_4bit_bus(sdio_host_t *host)
{
    u8 host_ctrl;
    int ret = sdio_cmd52(host, 1u, 0u, SDIO_CCCR_IF, 0x02u, NULL);

    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    host_ctrl = XSdPs_ReadReg8(host->sd.Config.BaseAddress, XSDPS_HOST_CTRL1_OFFSET);
    host_ctrl &= (u8)~XSDPS_HC_EXT_BUS_WIDTH;
    host_ctrl |= (u8)XSDPS_HC_BUS_WIDTH_4;
    XSdPs_WriteReg8(host->sd.Config.BaseAddress, XSDPS_HOST_CTRL1_OFFSET, host_ctrl);
    host->sd.BusWidth = XSDPS_4_BIT_WIDTH;
    return SDIO_HOST_OK;
}

static int sdio_enumerate_card(sdio_host_t *host)
{
    int ret;
    u32 resp0 = 0u;

    host->sd.CardType = XSDPS_CARD_SDIO;
    host->sd.BlkSize = SDIO_BLOCK_SIZE;

    if (XSdPs_ResetConfig(&host->sd) != XST_SUCCESS) {
        return SDIO_HOST_ERR_CONFIG;
    }
    /* ResetConfig clears status-enable registers. Restore polling events;
     * signal IRQs remain disabled (no interrupt-handler dependency). */
    XSdPs_ConfigInterrupt(&host->sd);
    sdio_config_dma_preserve_bus(host);
    xil_printf("[ZYNQ][SDIO] poll events enabled norm=0x%04x err=0x%04x\r\n",
        (unsigned)XSdPs_ReadReg16(host->sd.Config.BaseAddress,XSDPS_NORM_INTR_STS_EN_OFFSET),
        (unsigned)XSdPs_ReadReg16(host->sd.Config.BaseAddress,XSDPS_ERR_INTR_STS_EN_OFFSET));
    if (XSdPs_Change_ClkFreq(&host->sd, SDIO_INIT_CLOCK_HZ) != XST_SUCCESS) {
        return SDIO_HOST_ERR_CONFIG;
    }

    /* CMD0 alone does not reset an already-selected SDIO function. Follow
     * IDF sdmmc_io_reset: CCCR IO_ABORT bit3 before GO_IDLE_STATE. The reset
     * itself may time out/lose CRC; subsequent CMD5 must still succeed. */
    xil_printf("[ZYNQ][SDIO] CMD52 I/O reset before enumeration\r\n");
    (void)sdio_cmd52(host,1u,0u,0x06u,0x08u,NULL);
    usleep(1000u);
    xil_printf("[ZYNQ][SDIO] CMD0 reset card\r\n");
    ret=sdio_cmd(host, CMD0, 0u, 0u);
    if(ret!=SDIO_HOST_OK) return ret;
    xil_printf("[ZYNQ][SDIO] CMD0 complete; CMD5 OCR probe\r\n");
    usleep(10000u);
    XTime ocr_start,ocr_now;
    XTime_GetTime(&ocr_start);
    for (u32 retry = 0u; retry < 500u; ++retry) {
        sdio_clear_intr_status(host);
        ret = sdio_cmd(host, SDIO_CMD5_R4, SDIO_OCR_3V3, 0u);
        resp0 = sdio_resp0(host);
        if(ret!=SDIO_HOST_OK) return ret;
        if (ret == SDIO_HOST_OK && (resp0 & SDIO_OCR_READY) != 0u) {
            break;
        }
        usleep(1000u);
        XTime_GetTime(&ocr_now);
        if(sdio_elapsed_ms(ocr_start,ocr_now)>=1000U) break;
    }

    if ((resp0 & SDIO_OCR_READY) == 0u) {
        xil_printf("[ZYNQ][SDIO] CMD5 timeout resp0=0x%08x\r\n", resp0);
        return SDIO_HOST_ERR_TIMEOUT;
    }

    xil_printf("[ZYNQ][SDIO] CMD5 ready; CMD3 assign RCA\r\n");
    ret = sdio_cmd(host, SDIO_CMD3_R6, 0u, 0u);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }
    host->rca = sdio_resp0(host) & 0xffff0000u;
    if (host->rca == 0u) {
        return SDIO_HOST_ERR_CMD;
    }

    ret = sdio_cmd(host, CMD7, host->rca, 0u);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    xil_printf("[ZYNQ][SDIO] CMD7 selected; enable function1\r\n");
    ret = sdio_enable_fn1(host);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    ret = sdio_force_4bit_bus(host);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    ret = sdio_set_block_size(host, 0u, SDIO_BLOCK_SIZE);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    ret = sdio_set_block_size(host, SDIO_FN1, SDIO_BLOCK_SIZE);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    if (SDIO_RUN_CLOCK_HZ > 25000000u) {
        u8 speed;
        if (sdio_cmd52(host,0u,0u,0x13u,0u,&speed)!=SDIO_HOST_OK || !(speed & 1u) ||
            sdio_cmd52(host,1u,0u,0x13u,speed|2u,NULL)!=SDIO_HOST_OK)
            return SDIO_HOST_ERR_CONFIG;
    }
    if (XSdPs_Change_ClkFreq(&host->sd, SDIO_RUN_CLOCK_HZ) != XST_SUCCESS) {
        return SDIO_HOST_ERR_CONFIG;
    }

    return SDIO_HOST_OK;
}

int sdio_host_init(sdio_host_t *host)
{
    XSdPs_Config *cfg;
    int ret;
    u8 device_id = 0u;

    if (host == NULL) {
        return SDIO_HOST_ERR_CONFIG;
    }

    (void)memset(host, 0, sizeof(*host));
    host->function_num = SDIO_FN1;
    host->block_size = SDIO_BLOCK_SIZE;
    host->data_port_addr = SDIO_DATA_PORT_ADDR;

    #ifdef SDT
    cfg = XSdPs_LookupConfig(SDIO_HOST_BASEADDR);
#else
    cfg = NULL;
    for (u32 i=0; i<XPAR_XSDPS_NUM_INSTANCES; ++i) {
        XSdPs_Config *candidate=XSdPs_LookupConfig((u16)i);
        if (candidate && candidate->BaseAddress==SDIO_HOST_BASEADDR) cfg=candidate;
    }
#endif
    if (cfg == NULL || cfg->BaseAddress!=SDIO_HOST_BASEADDR) {
        return SDIO_HOST_ERR_LOOKUP;
    }

    ret = XSdPs_CfgInitialize(&host->sd, cfg, cfg->BaseAddress);
    if (ret != XST_SUCCESS) {
        return SDIO_HOST_ERR_CONFIG;
    }

    ret = sdio_enumerate_card(host);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    ret = sdio_read_reg(host, SDIO_REG_DEVICE_ID, &device_id);
    if (ret != SDIO_HOST_OK) {
        return ret;
    }
    if (device_id != SDIO_DEVICE_ID_ESP32C61) {
        u8 fn0_device_id = 0u;
        u8 fn1_device_id = 0u;
        u32 device_addr = esp32_shared_reg_addr(SDIO_REG_DEVICE_ID);

        (void)sdio_cmd52(host, 0u, 0u, device_addr, 0u, &fn0_device_id);
        (void)sdio_cmd52(host, 0u, host->function_num, device_addr, 0u, &fn1_device_id);
        xil_printf("[ZYNQ][SDIO] bad ESP device id: got=0x%02x expected=0x%02x addr=0x%05x fn=%u fn0=0x%02x fn1=0x%02x\r\n",
                   device_id,
                   SDIO_DEVICE_ID_ESP32C61,
                   device_addr,
                   host->function_num,
                   fn0_device_id,
                   fn1_device_id);
        return SDIO_HOST_ERR_DEVICE_ID;
    }

    xil_printf("[ZYNQ][SDIO] init OK, RCA=0x%08x block=%u data_port=0x%05x clk=%u window_max=%u batch_max=%u pio=%u first_pio_warmup=%u dma_fallback_pio=%u relaxed_resp=%u\r\n",
               host->rca,
               host->block_size,
               host->data_port_addr,
               (u32)SDIO_RUN_CLOCK_HZ,
               (u32)SDIO_HOST_MAX_WINDOW_PACKETS,
               (u32)SDIO_HOST_MAX_BATCH_BLOCKS,
               (u32)SDIO_CMD53_USE_PIO_WRITE,
               (u32)SDIO_CMD53_FIRST_BLOCK_PIO_WARMUP,
               (u32)SDIO_CMD53_DMA_FALLBACK_PIO,
               (u32)SDIO_CMD53_RELAXED_RESP);
    return SDIO_HOST_OK;
}

u8 sdio_host_read_status(sdio_host_t *host)
{
    u8 status = 0u;

    if (host == NULL || sdio_read_reg(host, SDIO_REG_STATUS, &status) != SDIO_HOST_OK) {
        return 0u;
    }

    return status;
}

int sdio_host_wait_window(sdio_host_t *host, u32 timeout_ms, u8 *grant_seq, u8 *grant_packets)
{
    XTime start;
    XTime now;
    const u64 timeout_ticks = ((u64)timeout_ms * (u64)COUNTS_PER_SECOND) / 1000u;

    if (host == NULL || grant_seq == NULL || grant_packets == NULL) {
        return SDIO_HOST_ERR_CONFIG;
    }

    XTime_GetTime(&start);
    while (1) {
        u8 status = 0u;
        u8 seq = 0u;
        int ret = sdio_read_reg(host, SDIO_REG_STATUS, &status);

        if (ret == SDIO_HOST_OK &&
            ((status & (SDIO_STATUS_READY | SDIO_STATUS_STREAM_ENABLE)) ==
             (SDIO_STATUS_READY | SDIO_STATUS_STREAM_ENABLE)) &&
            ((status & SDIO_STATUS_BACKPRESSURE) == 0u)) {
            ret = sdio_read_reg(host, SDIO_REG_WINDOW_GRANT_SEQ, &seq);
            if (ret == SDIO_HOST_OK && seq != 0u && seq != host->last_grant_seq) {
                u8 packets = 0u;

                ret = sdio_read_reg(host, SDIO_REG_WINDOW_PACKETS, &packets);
                if (ret != SDIO_HOST_OK) {
                    return ret;
                }
                if (packets == 0u) {
                    packets = 1u;
                } else if (packets > SDIO_HOST_MAX_WINDOW_PACKETS) {
                    packets = SDIO_HOST_MAX_WINDOW_PACKETS;
                }
                *grant_seq = seq;
                *grant_packets = packets;
                return SDIO_HOST_OK;
            }
        }

        XTime_GetTime(&now);
        if ((now - start) > timeout_ticks) {
            return SDIO_HOST_ERR_TIMEOUT;
        }

        usleep(50u);
    }
}

int sdio_host_send_block(sdio_host_t *host, const u8 block[US_SDIO_BLOCK_BYTES])
{
    const u32 cmd53_addr = host->data_port_addr - US_SDIO_BLOCK_BYTES;
    u32 arg;
    u32 write_count;
    u8 trace;
    u8 first_block_pio_warmup = 0u;

    if (host == NULL || block == NULL) {
        return SDIO_HOST_ERR_CONFIG;
    }

    write_count = ++s_cmd53_write_count;
    trace = sdio_should_log_cmd53(write_count);
    if (SDIO_CMD53_USE_PIO_WRITE == 0u &&
        SDIO_CMD53_FIRST_BLOCK_PIO_WARMUP != 0u &&
        write_count == 1u) {
        first_block_pio_warmup = 1u;
    }

    arg = (1u << 31) |
          ((u32)(host->function_num & 7u) << 28) |
          (1u << 27) |
          (1u << 26) |
          ((cmd53_addr & 0x1FFFFu) << 9) |
          1u;

    if (trace != 0u) {
        xil_printf("[ZYNQ][SDIO] CMD53 write#%u addr=0x%05x count=1 block=%u mode=%s arg=0x%08x\r\n",
                   write_count,
                   cmd53_addr,
                   (u32)US_SDIO_BLOCK_BYTES,
                   (SDIO_CMD53_USE_PIO_WRITE != 0u) ? "PIO" :
                   ((first_block_pio_warmup != 0u) ? "PIO-WARMUP" : "DMA"),
                   arg);
    }

#if SDIO_CMD53_USE_PIO_WRITE
    {
        int ret = sdio_pio_write_block(host, arg, block, trace);
        if (ret == SDIO_HOST_OK) {
            s_cmd53_pio_fallback_blocks++;
            sdio_print_stats_if_due("pio", write_count);
        }
        return ret;
    }
#else
    int ret;

    if (first_block_pio_warmup != 0u) {
        XTime pio_start;
        XTime pio_end;

        if (trace != 0u) {
            xil_printf("[ZYNQ][SDIO] CMD53 first block PIO warm-up before DMA stream\r\n");
        }
        XTime_GetTime(&pio_start);
        ret = sdio_pio_write_block(host, arg, block, trace);
        XTime_GetTime(&pio_end);
        s_cmd53_timing.pio_ms += sdio_elapsed_ms(pio_start, pio_end);
        if (ret == SDIO_HOST_OK) {
            s_cmd53_pio_warmup_blocks++;
            sdio_print_stats_if_due("warmup", write_count);
        }
        return ret;
    }

    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        ret = sdio_wait_bus_idle(host, 1u, 10000u, "CMD53_DMA_PRE_IDLE");
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_pre_idle_ms += sdio_elapsed_ms(stage_start, stage_end);
    }
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        Xil_DCacheFlushRange((UINTPTR)block, US_SDIO_BLOCK_BYTES);
        sdio_config_dma_preserve_bus(host);

        /* SetupTransfer may issue memory-card CMD16. SDIO block length was
         * already configured through CCCR/FBR CMD52, so set only host HW. */
        XSdPs_WriteReg16(host->sd.Config.BaseAddress,XSDPS_BLK_SIZE_OFFSET,US_SDIO_BLOCK_BYTES);

        XSdPs_SetupWriteDma(&host->sd, 1u, US_SDIO_BLOCK_BYTES, (u8 *)block);
        host->sd.TransferMode &= (u32)~XSDPS_TM_DAT_DIR_SEL_MASK;
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_setup_ms += sdio_elapsed_ms(stage_start, stage_end);
    }

    ret = sdio_wait_bus_idle(host, 1u, 10000u, "CMD53_DMA_CMD_IDLE");
    if (ret != SDIO_HOST_OK) {
        s_cmd53_dma_error_blocks++;
#if SDIO_CMD53_DMA_FALLBACK_PIO
        {
            XTime pio_start;
            XTime pio_end;

            xil_printf("[ZYNQ][SDIO] CMD53 DMA command idle wait failed, fallback to PIO for this block\r\n");
            XTime_GetTime(&pio_start);
            ret = sdio_pio_write_block(host, arg, block, trace);
            XTime_GetTime(&pio_end);
            s_cmd53_timing.pio_ms += sdio_elapsed_ms(pio_start, pio_end);
        }
        if (ret == SDIO_HOST_OK) {
            s_cmd53_pio_fallback_blocks++;
            sdio_print_stats_if_due("fallback", write_count);
        }
        return ret;
#else
        return ret;
#endif
    }

    if (trace != 0u) {
        xil_printf("[ZYNQ][SDIO] CMD53 DMA xfer_mode=0x%04x\r\n",
                   host->sd.TransferMode & 0xffffu);
        sdio_dump_regs(host, "CMD53_DMA_BEFORE_CMD", SDIO_CMD53_R5_DATA, arg, 1u);
    }
    sdio_clear_intr_status(host);
    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        ret = sdio_cmd53_fast_cmd(host, arg, 1u);
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_cmd_ms += sdio_elapsed_ms(stage_start, stage_end);
    }
    if (ret != SDIO_HOST_OK) {
        sdio_dump_regs(host, "CMD53_CMD_FAIL", SDIO_CMD53_R5_DATA, arg, 1u);
        sdio_reset_cmd_dat_lines(host, "CMD53 command failure");
        s_cmd53_dma_error_blocks++;
#if SDIO_CMD53_DMA_FALLBACK_PIO
        {
            XTime pio_start;
            XTime pio_end;

            xil_printf("[ZYNQ][SDIO] CMD53 DMA failed ret=%d, fallback to PIO for this block\r\n", ret);
            XTime_GetTime(&pio_start);
            ret = sdio_pio_write_block(host, arg, block, trace);
            XTime_GetTime(&pio_end);
            s_cmd53_timing.pio_ms += sdio_elapsed_ms(pio_start, pio_end);
        }
        if (ret == SDIO_HOST_OK) {
            s_cmd53_pio_fallback_blocks++;
            sdio_print_stats_if_due("fallback", write_count);
        }
        return ret;
#else
        return ret;
#endif
    }

    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        ret = sdio_wait_intr(host, XSDPS_INTR_TC_MASK, "CMD53_DMA_WAIT_TC");
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_tc_ms += sdio_elapsed_ms(stage_start, stage_end);
    }
    if (ret != SDIO_HOST_OK) {
        sdio_dump_regs(host, "CMD53_XFER_FAIL", SDIO_CMD53_R5_DATA, arg, 1u);
        sdio_reset_cmd_dat_lines(host, "CMD53 transfer failure");
        s_cmd53_dma_error_blocks++;
        return SDIO_HOST_ERR_TRANSFER;
    }

    s_cmd53_dma_blocks++;
    sdio_print_stats_if_due("dma", write_count);
    return SDIO_HOST_OK;
#endif
}

int sdio_host_quiesce(sdio_host_t *host)
{
    if(host==NULL) return SDIO_HOST_ERR_CONFIG;
    u32 psr=XSdPs_ReadReg(host->sd.Config.BaseAddress,XSDPS_PRES_STATE_OFFSET);
    if((psr & 7U)==0U) return SDIO_HOST_OK;
    sdio_reset_cmd_dat_lines(host,"release DDR lease");
    return sdio_wait_bus_idle(host,1u,10000u,"LEASE_DMA_IDLE");
}

int sdio_host_send_blocks(sdio_host_t *host, const u8 *blocks, u32 block_count)
{
    u32 total_bytes;
    u32 cmd53_addr;
    u32 arg;
    u32 first_dma_block;
    int ret;

    if (host == NULL || blocks == NULL || block_count == 0u ||
        block_count > SDIO_HOST_MAX_BATCH_BLOCKS) {
        return SDIO_HOST_ERR_CONFIG;
    }

    if (block_count == 1u || SDIO_CMD53_USE_PIO_WRITE != 0u) {
        for (u32 i = 0u; i < block_count; ++i) {
            ret = sdio_host_send_block(host, &blocks[i * US_SDIO_BLOCK_BYTES]);
            if (ret != SDIO_HOST_OK) {
                return ret;
            }
        }
        return SDIO_HOST_OK;
    }

    if (SDIO_CMD53_FIRST_BLOCK_PIO_WARMUP != 0u && s_cmd53_write_count == 0u) {
        ret = sdio_host_send_block(host, blocks);
        if (ret != SDIO_HOST_OK) {
            return ret;
        }
        blocks += US_SDIO_BLOCK_BYTES;
        block_count--;
        if (block_count == 0u) {
            return SDIO_HOST_OK;
        }
    }

    total_bytes = block_count * US_SDIO_BLOCK_BYTES;
    cmd53_addr = host->data_port_addr - total_bytes;
    arg = (1u << 31) |
          ((u32)(host->function_num & 7u) << 28) |
          (1u << 27) |
          (1u << 26) |
          ((cmd53_addr & 0x1FFFFu) << 9) |
          (block_count & 0x1ffu);

    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        ret = sdio_wait_bus_idle(host, 1u, 10000u, "CMD53_BATCH_DMA_PRE_IDLE");
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_pre_idle_ms += sdio_elapsed_ms(stage_start, stage_end);
    }
    if (ret != SDIO_HOST_OK) {
        return ret;
    }

    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        Xil_DCacheFlushRange((UINTPTR)blocks, total_bytes);
        sdio_config_dma_preserve_bus(host);

        /* Never send SD-memory CMD16 to an SDIO-only slave. */
        XSdPs_WriteReg16(host->sd.Config.BaseAddress,XSDPS_BLK_SIZE_OFFSET,US_SDIO_BLOCK_BYTES);

        XSdPs_SetupWriteDma(&host->sd, (u16)block_count, US_SDIO_BLOCK_BYTES, blocks);
        host->sd.TransferMode &= (u32)~XSDPS_TM_DAT_DIR_SEL_MASK;
        host->sd.TransferMode &= (u32)~XSDPS_TM_AUTO_CMD12_EN_MASK;
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_setup_ms += sdio_elapsed_ms(stage_start, stage_end);
    }

    ret = sdio_wait_bus_idle(host, 1u, 10000u, "CMD53_BATCH_DMA_CMD_IDLE");
    if (ret != SDIO_HOST_OK) {
        s_cmd53_dma_error_blocks += block_count;
#if SDIO_CMD53_DMA_FALLBACK_PIO
        for (u32 i = 0u; i < block_count; ++i) {
            ret = sdio_host_send_block(host, &blocks[i * US_SDIO_BLOCK_BYTES]);
            if (ret != SDIO_HOST_OK) {
                return ret;
            }
        }
        return SDIO_HOST_OK;
#else
        return ret;
#endif
    }

    sdio_clear_intr_status(host);
    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        ret = sdio_cmd53_fast_cmd(host, arg, block_count);
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_cmd_ms += sdio_elapsed_ms(stage_start, stage_end);
    }
    if (ret != SDIO_HOST_OK) {
        sdio_dump_regs(host, "CMD53_BATCH_CMD_FAIL", SDIO_CMD53_R5_DATA, arg, block_count);
        sdio_reset_cmd_dat_lines(host, "CMD53 batch command failure");
        s_cmd53_dma_error_blocks += block_count;
#if SDIO_CMD53_DMA_FALLBACK_PIO
        for (u32 i = 0u; i < block_count; ++i) {
            ret = sdio_host_send_block(host, &blocks[i * US_SDIO_BLOCK_BYTES]);
            if (ret != SDIO_HOST_OK) {
                return ret;
            }
        }
        return SDIO_HOST_OK;
#else
        return ret;
#endif
    }

    {
        XTime stage_start;
        XTime stage_end;

        XTime_GetTime(&stage_start);
        ret = sdio_wait_intr(host, XSDPS_INTR_TC_MASK, "CMD53_BATCH_DMA_WAIT_TC");
        XTime_GetTime(&stage_end);
        s_cmd53_timing.dma_tc_ms += sdio_elapsed_ms(stage_start, stage_end);
    }
    if (ret != SDIO_HOST_OK) {
        sdio_dump_regs(host, "CMD53_BATCH_XFER_FAIL", SDIO_CMD53_R5_DATA, arg, block_count);
        sdio_reset_cmd_dat_lines(host, "CMD53 batch transfer failure");
        s_cmd53_dma_error_blocks += block_count;
        return SDIO_HOST_ERR_TRANSFER;
    }

    XSdPs_WriteReg16(host->sd.Config.BaseAddress, XSDPS_NORM_INTR_STS_OFFSET, XSDPS_INTR_TC_MASK);
    first_dma_block = s_cmd53_write_count + 1u;
    s_cmd53_write_count += block_count;
    s_cmd53_dma_blocks += block_count;
    sdio_print_stats_if_due("dma-batch", first_dma_block);
    return SDIO_HOST_OK;
}

int sdio_host_write_window_ack(sdio_host_t *host, u8 grant_seq)
{
    int ret;

    if (host == NULL) {
        return SDIO_HOST_ERR_CONFIG;
    }

    ret = sdio_write_reg(host, SDIO_REG_WINDOW_ACK_SEQ, grant_seq);
    if (ret == SDIO_HOST_OK) {
        host->last_grant_seq = grant_seq;
    }

    return ret;
}
