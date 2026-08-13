#include "iic.h"
#include "../main.h"
XIicPs iicps_0;
XIicPs iicps_1;

u32 IIC_Init(void) {
	u32 status = 0;
	XIicPs_Config *cfg;
	cfg = XIicPs_LookupConfig(XPAR_XIICPS_0_BASEADDR);
	if (cfg == NULL) {
		return XST_FAILURE;
	}
	status = XIicPs_CfgInitialize(&iicps_0, cfg, cfg->BaseAddress);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	status = XIicPs_SelfTest(&iicps_0);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	status = XIicPs_SetSClk(&iicps_0, IIC_CLK);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	cfg = XIicPs_LookupConfig(XPAR_XIICPS_1_BASEADDR);
	if (cfg == NULL) {
		return XST_FAILURE;
	}
	status = XIicPs_CfgInitialize(&iicps_1, cfg, cfg->BaseAddress);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	status = XIicPs_SelfTest(&iicps_1);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	status = XIicPs_SetSClk(&iicps_1, IIC_CLK);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	return XST_SUCCESS;
}
u32 IIC_polled_write(u32 iic_device_num, u32 addr, u32 data_length, u8 *data) {
	u32 status = 0;
	if (iic_device_num == 0) {
		status = XIicPs_MasterSendPolled(&iicps_0, data, data_length, addr);
		while (XIicPs_BusIsBusy(&iicps_0))
			;
		return status;
	} else if (iic_device_num == 1) {
		status = XIicPs_MasterSendPolled(&iicps_1, data, data_length, addr);
		while (XIicPs_BusIsBusy(&iicps_1))
			;
		return status;
	} else {

		DEBUG_PRINT("Error : check device num\r\n");
		return XST_FAILURE;
	}
}
u32 IIC_polled_read(u32 iic_device_num, u8 device_addr, u8 reg_addr,
		u32 data_length, u8 *data) {
	u32 status = 0;
	u8 temp_data[2] = { 0 };
	temp_data[0] = reg_addr;
	if (iic_device_num == 0) {
//		data[1] = reg_addr;
		// 先写发送的地址
		status = XIicPs_MasterSendPolled(&iicps_0, temp_data, 1, device_addr);
		while (XIicPs_BusIsBusy(&iicps_0))
			;
		// 写完读地址后转接收
		status = XIicPs_MasterRecvPolled(&iicps_0, data, data_length, device_addr);
		while (XIicPs_BusIsBusy(&iicps_0))
			;
		return status;
	} else if (iic_device_num == 1) {
		// 先写发送的地址
		status = XIicPs_MasterSendPolled(&iicps_1, temp_data, 1, device_addr);
		while (XIicPs_BusIsBusy(&iicps_1))
			;
		status = XIicPs_MasterRecvPolled(&iicps_1, data,data_length, device_addr);
		while (XIicPs_BusIsBusy(&iicps_1))
			;
		return status;
	} else {

		DEBUG_PRINT("Error : check device num\r\n");
		return XST_FAILURE;
	}
}
