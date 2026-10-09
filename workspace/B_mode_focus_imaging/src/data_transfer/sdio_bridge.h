#ifndef ZYNQ_SDIO_BRIDGE_H
#define ZYNQ_SDIO_BRIDGE_H
#include "bridge_protocol.h"
/* Canonical definition: esp32c61/protocol/bridge_protocol.h; checked by tests. */
void sdio_bridge_init(void);
void sdio_bridge_poll(void);
void sdio_bridge_set_output(int sdio);
int sdio_bridge_output_is_sdio(void);
void sdio_bridge_print_status(void);
#endif
