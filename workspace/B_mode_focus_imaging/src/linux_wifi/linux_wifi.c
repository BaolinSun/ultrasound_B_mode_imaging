#include "linux_wifi.h"
#include "xil_cache.h"
#include "xil_mmu.h"
volatile u32 *data_addr_addr = (u32 *) DATA_ADDR;
volatile u32 *data_flag = (u32 *) DATA_FLAG;
volatile u32 *send_flag = (u32 *) (DATA_FLAG + 4);
volatile u32 *send_buffer = (u32 *) (DATA_FLAG + 8);
void wifi_mem_init() {
	Xil_SetTlbAttributes(DATA_ADDR, 0x14de2);
	Xil_SetTlbAttributes(DATA_FLAG, 0x14de2);
	Xil_SetTlbAttributes(DATA_FLAG + 4, 0x14de2);
	Xil_SetTlbAttributes(DATA_FLAG + 8, 0x14de2);
	*data_addr_addr = 0;
	*data_flag = 0;
	*send_flag = 0;
	*send_buffer = 0;
}
/*
 * pre_buffer_cnt : 目前正在wifi发送的缓冲区
 * now_buffer_cnt : 准备发送的缓冲区
 * package_size : 每个缓冲区内的小缓冲区的大小
 * package_nums : 每个缓冲区内的小缓冲区的数量
 * data_addr : 缓冲区起始地址
 * */
void send_thur_wifi(u32 pre_buffer_cnt, u32 now_buffer_cnt, u32 package_size,
		u32 package_nums, u32 data_addr) {
	static int init = 0;
	// linux 端写完后将buffer_标准位+1写入
	if (init != 0) {
		u32 send_finish = pre_buffer_cnt + 1;
		while ((*send_flag) != send_finish) {
			Xil_DCacheInvalidateRange((INTPTR) data_addr_addr, 32);
		}
		// 清零标志位
		*send_flag = 0;
	} else {
	}
	init = 1;
	u32 data_temp = 0;
	Xil_DCacheFlushRange((INTPTR) data_addr_addr, 32);
	// 写当前缓冲区编号
	*send_buffer = now_buffer_cnt;
	// 写缓冲区首地址
	*data_addr_addr = data_addr;
	data_temp = (package_size & 0x0000FFFF) << 16;
	data_temp |= (package_nums & 0x0000FFFF);

	*data_flag = data_temp;
	Xil_DCacheFlushRange((INTPTR) data_addr_addr, 32);
//	while (*data_flag != 0) {
//		Xil_DCacheFlushRange(data_addr_addr, 16);
//	};
}
