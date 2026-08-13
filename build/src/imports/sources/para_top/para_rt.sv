//--------------------------------------------------------------------------------------------------
`include "fpga_define.vh"
//--------------------------------------------------------------------------------------------------
module para_rt
(
	input										clk_para,
	input										clk_200,
	input										clk_25,
	input										reset,
	input										para_rt_wren,
	input										para_rt_rden,
	input		[19:0]							para_rt_addr,
	input		[31:0]							para_rt_wrdata,
	output	reg	[31:0]							para_rt_rddata,
	output	reg									para_rt_rdvalid,

	input										tx_rt_reg_rden,
	input		[13:0]							tx_rt_reg_rdaddr,
	output		[31:0]							tx_rt_reg_rddata,
	output	reg	[4:0]							tx_rt_reg_num,
	
	input										rx_foc_delta_rden,
	input		[5:0]							rx_foc_delta_rdaddr,
	output		[31:0]							rx_foc_delta_rddata
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												para_fanout_wren	;
reg												para_fanout_rden	;
reg				[19:0]							para_fanout_addr	;
reg				[31:0]							para_fanout_wrdata	;
reg				[31:0]							para_fanout_rddata	;
reg												para_fanout_rdvalid	;

always@(posedge clk_para)
begin
	para_fanout_wren	<=	para_rt_wren;
	para_fanout_rden	<=	para_rt_rden;
	para_fanout_addr	<=	para_rt_addr;
	para_fanout_wrdata	<=	para_rt_wrdata;

	para_rt_rddata		<=	para_fanout_rddata;
	para_rt_rdvalid		<=	para_fanout_rdvalid;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												ram_tx_rt_reg_en	,	ram_tx_rt_reg_wren	;
reg												ram_rx_foc_delta_en	,	ram_rx_foc_delta_wren;
//--------------------------------------------------------------------------------------------------
reg												ram_tx_rt_reg_rden	,	ram_tx_rt_reg_rdrdy	;
reg												ram_rx_foc_delta_rden	,	ram_rx_foc_delta_rdrdy	;
//--------------------------------------------------------------------------------------------------
reg				[19:0]							ram_tx_rt_reg_addr	;
reg				[9:0]							ram_rx_foc_delta_addr	;
//--------------------------------------------------------------------------------------------------
reg				[31:0]							ram_tx_rt_reg_wrdata;
reg				[31:0]							ram_rx_foc_delta_wrdata	;
//--------------------------------------------------------------------------------------------------
wire			[31:0]							ram_tx_rt_reg_rddata;
wire			[31:0]							ram_rx_foc_delta_rddata	;
//--------------------------------------------------------------------------------------------------
reg												ram_notuse_addr_rden	,	ram_notuse_addr_rdrdy	;
//--------------------------------------------------------------------------------------------------
always@(posedge clk_para)
begin
	ram_tx_rt_reg_en		<=	(para_fanout_addr[19:16] == (`ADDR_TX_RT_REG  >> 16));
	ram_rx_foc_delta_en		<=	(para_fanout_addr[19:08] == (`ADDR_RX_FOC_DELTA >>  8));
		
	ram_tx_rt_reg_wren		<=	(para_fanout_wren & (para_fanout_addr[19:16] == (`ADDR_TX_RT_REG  >> 16)));
	ram_rx_foc_delta_wren	<=	(para_fanout_wren & (para_fanout_addr[19:08] == (`ADDR_RX_FOC_DELTA >>  8)));
	
	ram_tx_rt_reg_rden		<=	(para_fanout_rden & (para_fanout_addr[19:16] == (`ADDR_TX_RT_REG  >> 16)));
	ram_rx_foc_delta_rden	<=	(para_fanout_rden & (para_fanout_addr[19:08] == (`ADDR_RX_FOC_DELTA >>  8)));
	
	ram_tx_rt_reg_rdrdy		<=	ram_tx_rt_reg_rden;
	ram_rx_foc_delta_rdrdy	<=	ram_rx_foc_delta_rden	;
	ram_notuse_addr_rdrdy	<=	ram_notuse_addr_rden;

	ram_tx_rt_reg_addr		<=	{20'b0, para_fanout_addr[15:2]};
	ram_rx_foc_delta_addr	<=	{3'b0, para_fanout_addr[ 7:2]};
	
	ram_tx_rt_reg_wrdata	<=	para_fanout_wrdata;
	ram_rx_foc_delta_wrdata	<=	para_fanout_wrdata;
	if(ram_tx_rt_reg_wren & (ram_tx_rt_reg_addr[13:0] == 14'h03FF))	tx_rt_reg_num	<=	para_fanout_wrdata;

		 if(ram_tx_rt_reg_rdrdy		)	para_fanout_rddata	<=	ram_tx_rt_reg_rddata;
	else if(ram_rx_foc_delta_rdrdy	)	para_fanout_rddata	<=	ram_rx_foc_delta_rddata;
	else if(ram_notuse_addr_rdrdy	)	para_fanout_rddata	<=	32'h_eeee_eeee;

		 if(ram_tx_rt_reg_rdrdy		)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_rx_foc_delta_rdrdy	)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_notuse_addr_rdrdy	)	para_fanout_rdvalid	<=	1'b1;
	else								para_fanout_rdvalid	<=	1'b0;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
tdpram_32bx16384								ram_rt_reg
(
	.clka										(clk_para),
	.ena										(ram_tx_rt_reg_en),
	.wea										(ram_tx_rt_reg_wren),
	.addra										(ram_tx_rt_reg_addr),
	.dina										(ram_tx_rt_reg_wrdata),
	.douta										(ram_tx_rt_reg_rddata),
	.clkb										(clk_200),
	.web										(0),
	.dinb										(0),
	.enb										(tx_rt_reg_rden),
	.addrb										(tx_rt_reg_rdaddr),
	.doutb										(tx_rt_reg_rddata)
);

tdpram_32bx1024									ram_rx_foc_delta
(
	.clka										(clk_para),
	.ena										(ram_rx_foc_delta_en),
	.wea										(ram_rx_foc_delta_wren),
	.addra										(ram_rx_foc_delta_addr),
	.dina										(ram_rx_foc_delta_wrdata),
	.douta										(ram_rx_foc_delta_rddata),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(rx_foc_delta_rden),
	.addrb										({4'd0,rx_foc_delta_rdaddr}),
	.doutb										(rx_foc_delta_rddata)
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
