`timescale 1ns / 1ps

`ifndef fpga_define_v__
`define fpga_define_v__

//------------------------------------------------------------------------------
//				  ADDR SEGMENT
//------------------------------------------------------------------------------
`define			ADDR_SEG_ST				3'b_000_	//100
`define			ADDR_SEG_RT				3'b_001_	//101
`define			ADDR_SEG_REG			3'b_100_	//000
//------------------------------------------------------------------------------
//ST
//------------------------------------------------------------------------------
`define			ADDR_DEMOD_SIN			20'h_00000		//	25.06.18	//	ST
`define			ADDR_DEMOD_COS			20'h_08000		//	25.06.18	//	ST
`define			ADDR_LOG1_COEF			20'h_10000		//	24.04.23	//	ST
`define			ADDR_TX_ST_REG			20'h_11000		//	25.06.18 	//	ST
`define			ADDR_RX_FOC_ELEXY		20'h_11200		//	25.08.18 	//	ST
`define			ADDR_RX_APOD_DATA		20'h_11300		//	25.08.18 	//	ST
`define			ADDR_FILTER_COEF		20'h_11400		//	25.08.18	//	ST
`define			ADDR_SCAN_TIME			20'h_11500		//	25.08.18	//	ST
`define			ADDR_SCAN_SPI_START		20'h_11528		//	scan timing word 10
`define			ADDR_BEAMFORM_CTRL		20'h_1152C		//	scan timing word 11, bit 0: run request
//-----------------------------------------------------------------------------
//RT
//-----------------------------------------------------------------------------
`define			ADDR_TX_RT_REG			20'h_00000		//	25.06.18	//	RT
`define			ADDR_RX_FOC_DELTA		20'h_10000		//	25.06.18	//	RT
//------------------------------------------------------------------------------
//REG
//------------------------------------------------------------------------------
`define			ADDR_TX_ENABLE			20'h_C0100		//	25.06.23	//	REG
`define			ADDR_SPI_START			20'h_C0108		//	25.06.23	//	REG
`define			ADDR_TX_RESET			20'h_C010C		//	25.06.23	//	REG
`define			ADDR_TX_CFG_RD_REQ		20'h_C0110		//	25.06.23	//	REG
`define			ADDR_TX_CFG_RDDATA		20'h_C0114		//	25.06.23	//	REG
`define			ADDR_SCAN_ENABLE		20'h_D0000		//	25.06.13	//	REG
//-----------------------------------------------------------------------------

`endif
