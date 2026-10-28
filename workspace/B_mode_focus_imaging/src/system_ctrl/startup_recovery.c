#include "startup_recovery.h"
#ifdef STARTUP_RECOVERY_HOST_TEST
#include "startup_recovery_test_services.h"
#else
#include "temp.h"
#include "temp_hw.h"
#include "../AFE5832/afe_power.h"
#include "../AFE5832/afe5832.h"
#include "../TX7332/tx_power.h"
#include "../GPIO/gpio.h"
#include "../HV/hv.h"
#include "../bram/bram.h"
#include "../uart/uart.h"
#include "../uart/uart_log.h"
#include "../usb/usb_transport.h"
#include "../data_transfer/frame_store.h"
#include "../data_transfer/sdio_bridge.h"
#include "afe_power_regs.h"
#include "xil_io.h"
#include "xil_printf.h"
#endif

static int armed, backoff, clock_wait_logged;
static uint32_t tried_at, attempts, afe_operations, tx_operations, clock_wait_at;

int startup_recovery_blocks(void) { return armed; }

void startup_recovery_init(int initial_frame_rc, int inherited_usb_idle) {
    /* Arm once, from the actual boot result. Runtime faults never re-arm it. */
    armed=initial_frame_rc!=0 && inherited_usb_idle;
    backoff=0;attempts=0;clock_wait_logged=0;
    afe_operations=afe_power_operation_sequence();
    tx_operations=tx_power_operation_sequence();
    if(armed) xil_printf("@BOOT_RECOVERY WAIT_TEMP threshold_mC=%d retry_ms=%u; scan/HV/upload blocked\r\n",
        STARTUP_RECOVERY_TEMP_MC,STARTUP_RECOVERY_RETRY_MS);
    else if(initial_frame_rc) xil_printf("@BOOT_RECOVERY DISABLED inherited USB DMA not proven idle; manual intervention required\r\n");
}

void startup_recovery_poll(void) {
    if(!armed) return;
    /* Explicit power operations supersede automatic boot recovery. Never
     * reinterpret a user's SLEEP/WAKE failure as the original cold-start fault. */
    if(afe_operations!=afe_power_operation_sequence() || tx_operations!=tx_power_operation_sequence()) {
        armed=0;
        xil_printf("@BOOT_RECOVERY CANCELLED explicit AFE/TX operation\r\n");
        return;
    }
    uint32_t now=temp_hw_now_ms();int32_t mc;
    if(backoff && (uint32_t)(now-tried_at)<STARTUP_RECOVERY_RETRY_MS) return;
    if(temperature_service_zynq_mc(&mc)!=0 || mc<STARTUP_RECOVERY_TEMP_MC) return;
    /* No side effects while control/configuration or a lease is in flight.
     * AP_READY is a prerequisite, not a substitute for the fresh epoch check
     * performed below by afe_power_init(). No PLL/SPI/clock configuration. */
    if(uart_config_active() || uart_idle_pending() || zynq_uart_idle_pending() ||
       Beamform_GetRunRequest() || Beamform_GetSpiStartRequest() || hv_is_enabled() ||
       !usb_transport_power_idle() || !sdio_bridge_power_idle() || !tx_power_allowed() ||
       XGpioPs_ReadPin(&gpiops,AFE5832_PDN_GBL_PIN_EMIO)) return;
    uint32_t version=Xil_In32(AP_BASE+AP_VERSION),control=Xil_In32(AP_BASE+AP_CONTROL);
    uint32_t flags=Xil_In32(AP_BASE+AP_STATUS);
    if(version!=AP_MAGIC || control!=0 || (flags&AP_READY)!=AP_READY) {
        if(!clock_wait_logged || (uint32_t)(now-clock_wait_at)>=STARTUP_RECOVERY_RETRY_MS) {
            clock_wait_logged=1;clock_wait_at=now;
            xil_printf("@BOOT_RECOVERY WAIT_CLOCK zynq_mC=%d version=%08x control=%08x flags=%08x\r\n",
                (int)mc,(unsigned)version,(unsigned)control,(unsigned)flags);
        }
        return;
    }
    tried_at=now;backoff=1;attempts++;
    xil_printf("@BOOT_RECOVERY TRY attempt=%u zynq_mC=%d\r\n",attempts,(int)mc);
    /* No USB controller reset: its live OUT/configuration service stays intact.
     * SDIO must relinquish its old source-unavailable handshake before INIT
     * resets SD1. A held/uncertain DMA lease is never revoked. */
    if(sdio_bridge_source_recovery_begin()!=0) {
        xil_printf("@BOOT_RECOVERY RETRY SDIO_NOT_IDLE\r\n");return;
    }
    int rc=frame_store_init();
    const volatile frame_store_init_diag_t *d=frame_store_init_diagnostic();
    xil_printf("@BOOT_RECOVERY DDR rc=%d stage=%u version=%08x seq=%u ack=%u result=%u flags=%08x\r\n",
        rc,(unsigned)d->stage,(unsigned)d->version,(unsigned)d->sequence,
        (unsigned)d->ack,(unsigned)d->result,(unsigned)d->flags);
    if(rc) return;
    /* Refresh only the failed boot-time AFE software binding and receive epoch.
     * This does NOT call AFE5832_init(), para_down(), or reset the whole PS. */
    afe_power_init();
    if(!afe_power_allowed()) {
        xil_printf("@BOOT_RECOVERY RETRY AFE_NOT_READY error=%s\r\n",afe_power_error());return;
    }
    if(frame_store_pause()!=0) {
        xil_printf("@BOOT_RECOVERY RETRY DDR_PAUSE\r\n");return;
    }
    /* Rebuild SDIO's source advertisement only on the fully confirmed path;
     * its existing ABORT/session handshake requires a fresh user upload start. */
    sdio_bridge_init();
    armed=0;
    xil_printf("@BOOT_RECOVERY READY attempts=%u; scan=0 hv=0 upload=stopped; query PS status then start manually\r\n",attempts);
}
