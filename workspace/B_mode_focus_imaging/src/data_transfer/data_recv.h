#ifndef __DATA_RECV_H
#define __DATA_RECV_H


#include "../main.h"
//#include "ps_2_pl_ctrl.h"

void data_recv_init(void);
void data_recv(u32 ddr_addr,u32 fifo_depth,u32 fifo_nums,u32 angle_nums);

#endif
