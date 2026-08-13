#ifndef __AD9512_H
#define __AD9512_H


#include "xspips.h"
#include "xparameters.h"
#define SPI_BASEADDR XPAR_XSPIPS_0_BASEADDR

#define AD9512_FUNC_PIN 26


u32 hw_spi_init(void);
u32 ad9512_init(void);
u32 ad9512_spi_write(u32 addr,u8 data);
u32 ad9512_spi_read(u32 addr,u8 *data);

// 50 100 200
void tx7332_set_freq(u32 chip_id,u32 freq);
void afe5832_set_freq(u32 chip_id,u32 freq);
#endif
