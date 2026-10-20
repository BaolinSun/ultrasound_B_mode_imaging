#ifndef UB_FRAME_STORE_H
#define UB_FRAME_STORE_H
#include "xil_types.h"
/* PL ABI: BRAM offset 0x4c0200, all accesses in the parameter/AXI clock. */
#define FS_BASE 0x404C0200U
#define FS_MAGIC 0x55424631U
#define FS_DIAG_MAGIC_VALUE 0x55424431U
enum { FS_VERSION=0, FS_CMD=4, FS_SEQ=8, FS_ACK=12, FS_RESULT=16,
 FS_FLAGS=20, FS_SLOT=24, FS_FRAME=28, FS_BYTES=32, FS_DECIMATION=36,
 FS_LEASE=40, FS_WATERMARK=44, FS_RELEASE_TOKEN=48, FS_DROPS=52,
 FS_ERRORS=56, FS_OCCUPANCY=60, FS_LATEST=64, FS_PUBLISHED=68 };
enum { FS_DIAG_VERSION=72, FS_DIAG_AXI=76, FS_DIAG_SEQUENCE=80,
 FS_DIAG_COMMAND=84, FS_DIAG_PENDING=88, FS_DIAG_INPUT=92,
 FS_DIAG_SAMPLES=96, FS_DIAG_AGE=100, FS_DIAG_PARSER=104 };
enum { FS_INIT=1, FS_ACQUIRE=2, FS_RELEASE=3, FS_PAUSE=4, FS_RESUME=5 };
enum { FS_OK=0, FS_EMPTY=1, FS_BUSY=2, FS_INVALID=3 };
typedef struct { const u8 *data; u32 frame, bytes, decimation, lease, slot; } frame_lease_t;
/* Retained startup diagnostics: stages identify the exact failed prerequisite.
 * Read-only observation; does not bypass DMA quiescence or PL ownership. */
enum { FS_INIT_VERSION=1, FS_INIT_DMA=2, FS_INIT_COMMAND_VERSION=3,
 FS_INIT_ACK=4, FS_INIT_RESULT=5, FS_INIT_READY=6 };
typedef struct {
    u32 stage, version, sd_clock_before, sd_clock_after;
    u32 sd_present_before, sd_present_after, sd_reset_after, dma_elapsed_ms;
    u32 sequence, ack, result, flags;
    u32 sd_cleanup_phase, sd_clock_ready;
} frame_store_init_diag_t;
enum { FS_SD_CLOCK=1, FS_SD_RESET=2, FS_SD_IDLE=3, FS_SD_DONE=4 };
const volatile frame_store_init_diag_t *frame_store_init_diagnostic(void);
int frame_store_init(void);
/* Local init plus current PL ABI/initialized flag; never performs recovery. */
int frame_store_is_ready(void);
/* 0 acquired, 1 no new complete frame, -1 ABI/ownership error. */
int frame_store_acquire(frame_lease_t *frame, u32 watermark);
int frame_store_release(frame_lease_t *frame);
int frame_store_pause(void); /* only succeeds after hardware pipeline/AXI drain */
enum { FS_DRAIN_OK=0, FS_DRAIN_NOT_READY=1, FS_DRAIN_ABI=2,
       FS_DRAIN_TIMEOUT=3, FS_DRAIN_RESULT=4 };
typedef struct {
    u32 reason,version,sequence,ack,result,flags;
    u32 axi,input,parser,samples,age;
} frame_store_pause_diag_t;
const volatile frame_store_pause_diag_t *frame_store_pause_diagnostic(void);
int frame_store_resume(void);
#endif
