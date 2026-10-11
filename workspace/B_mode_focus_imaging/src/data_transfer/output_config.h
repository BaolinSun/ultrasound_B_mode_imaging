#ifndef DATA_OUTPUT_CONFIG_H
#define DATA_OUTPUT_CONFIG_H

/* Compile-time only: edit this macro and rebuild BOOT.BIN.
 * Canonical source: build/src/ps/data_transfer/output_config.h.
 * No UART or USB command can change the data output mode. */
#define DATA_OUTPUT_USB  0
#define DATA_OUTPUT_SDIO 1

#ifndef DATA_OUTPUT_MODE
#define DATA_OUTPUT_MODE DATA_OUTPUT_SDIO
#endif

#if DATA_OUTPUT_MODE != DATA_OUTPUT_USB && DATA_OUTPUT_MODE != DATA_OUTPUT_SDIO
#error "DATA_OUTPUT_MODE must be DATA_OUTPUT_USB or DATA_OUTPUT_SDIO"
#endif

#endif
