`timescale 1ns / 1ps

`ifndef fpga_define_v__
`define fpga_define_v__

//------------------------------------------------------------------------------
//				  ADDR SEGMENT
//------------------------------------------------------------------------------
`define			ADDR_SEG_ST				3'b000	//100
`define			ADDR_SEG_RT				3'b001	//101
`define			ADDR_SEG_REG			3'b100	//000
//------------------------------------------------------------------------------
//ST
//------------------------------------------------------------------------------
`define			ADDR_DEMOD_SIN			20'h00000		//	25.06.18	//	ST
`define			ADDR_DEMOD_COS			20'h08000		//	25.06.18	//	ST
`define			ADDR_LOG1_COEF			20'h10000		//	24.04.23	//	ST
`define			ADDR_TX_ST_REG			20'h11000		//	25.06.18 	//	ST
`define			ADDR_RX_FOC_ELEXY		20'h11200		//	25.08.18 	//	ST
`define			ADDR_RX_APOD_DATA		20'h11300		//	25.08.18 	//	ST
`define			ADDR_FILTER_COEF		20'h11400		//	25.08.18	//	ST
`define			ADDR_SCAN_TIME			20'h11500		//	25.08.18	//	ST
`define			ADDR_SCAN_SPI_START		20'h11528		//	scan timing word 10
`define			ADDR_BEAMFORM_CTRL		20'h1152C		//	scan timing word 11, bit 0: run request
`define			ADDR_RX_GAIN_SHIFT		20'h11530		//	scan timing word 12, bits 3:0: filter_iq gain shift, +6.02 dB/step, 0..8
//-----------------------------------------------------------------------------
//RT
//-----------------------------------------------------------------------------
`define			ADDR_TX_RT_REG			20'h00000		//	25.06.18	//	RT
`define			ADDR_RX_FOC_DELTA		20'h10000		//	25.06.18	//	RT
//------------------------------------------------------------------------------
//REG
//------------------------------------------------------------------------------
`define			ADDR_TX_ENABLE			20'hC0100		//	25.06.23	//	REG
`define			ADDR_SPI_START			20'hC0108		//	25.06.23	//	REG
`define			ADDR_TX_RESET			20'hC010C		//	25.06.23	//	REG
`define			ADDR_TX_CFG_RD_REQ		20'hC0110		//	25.06.23	//	REG
`define			ADDR_TX_CFG_RDDATA		20'hC0114		//	25.06.23	//	REG
`define			ADDR_SCAN_ENABLE		20'hD0000		//	25.06.13	//	REG
//-----------------------------------------------------------------------------

`endif
