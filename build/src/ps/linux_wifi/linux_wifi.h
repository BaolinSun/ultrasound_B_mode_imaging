#ifndef _LINUX_WIFI_H
#define _LINUX_WIFI_H

#include "../main.h"

#define DATA_ADDR 0x1E400000
#define DATA_FLAG 0x1E400004

void wifi_mem_init();
void send_thur_wifi(u32 pre_buffer_cnt,u32 now_buffer_cnt,u32 package_size, u32 package_nums, u32 data_addr);

#endif
