#ifndef UB_STARTUP_RECOVERY_H
#define UB_STARTUP_RECOVERY_H
/* CPU0 foreground only. Never reinitialize a frame store after normal use. */
#define STARTUP_RECOVERY_TEMP_MC 60000
#define STARTUP_RECOVERY_RETRY_MS 30000U
void startup_recovery_init(int initial_frame_rc, int inherited_usb_idle);
void startup_recovery_poll(void);
int startup_recovery_blocks(void);
#endif
