#ifndef ZYNQ_SDIO_BRIDGE_H
#define ZYNQ_SDIO_BRIDGE_H
#include "output_config.h"
#include "bridge_protocol.h"
/* Canonical definition: esp32c61/protocol/bridge_protocol.h; checked by tests. */
void sdio_bridge_init(void);
void sdio_bridge_poll(void);
int sdio_bridge_power_idle(void);
int sdio_bridge_idle_pending(void); /* read-only SD1 CMD/DAT/DMA snapshot */
int sdio_bridge_output_is_sdio(void);
#endif
