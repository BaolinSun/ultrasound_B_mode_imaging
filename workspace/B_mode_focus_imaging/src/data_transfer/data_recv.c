#include "data_recv.h"

void data_recv_init(void)
{
	/* No PL receive interrupt or control register block in this hardware. */
}

void data_recv(u32 ddr_addr, u32 fifo_depth, u32 fifo_nums, u32 angle_nums)
{
	(void)ddr_addr;
	(void)fifo_depth;
	(void)fifo_nums;
	(void)angle_nums;
}
