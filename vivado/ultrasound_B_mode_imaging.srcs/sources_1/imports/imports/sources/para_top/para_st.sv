//--------------------------------------------------------------------------------------------------
`include "fpga_define.vh"
//--------------------------------------------------------------------------------------------------
module para_st
(
	input										clk_para,
	input										clk_200,
	input										clk_25,
	input										reset,
	input										para_st_wren,
	input										para_st_rden,
	input		[19:0]							para_st_addr,
	input		[31:0]							para_st_wrdata,
	output	reg	[31:0]							para_st_rddata,
	output	reg									para_st_rdvalid,

	input										demod_sin_rden,
	input		[12:0]							demod_sin_rdaddr,
	output		[31:0]							demod_sin_rddata,
	input										demod_cos_rden,
	input		[12:0]							demod_cos_rdaddr,
	output		[31:0]							demod_cos_rddata,

	input										log1_coef_rden,
	input		[9:0]							log1_coef_rdaddr,
	output		[31:0]							log1_coef_rddata,
	input										log2_coef_rden,
	input		[5:0]							log2_coef_rdaddr,
	output		[31:0]							log2_coef_rddata,

	input										tx_st_reg_rden,
	input		[8:0]							tx_st_reg_rdaddr,
	output		[31:0]							tx_st_reg_rddata,
	output	reg	[6:0]							tx_st_reg_num,

	input										rx_apod_data_rden,
	input		[5:0]							rx_apod_data_rdaddr,
	output		[7:0]							rx_apod_data_rddata,
	input										rx_foc_elexy_rden,
	input		[5:0]							rx_foc_elexy_rdaddr,
	output		[15:0]							rx_foc_elexy_rddata,

	input										filter_coef_rden,
	input		[5:0]							filter_coef_rdaddr,
	output		[15:0]							filter_coef_rddata,

	output	reg	[31:0]							rtc_rfdot,
	output	reg	[31:0]							rtc_period,
	output	reg	[31:0]							rt_length,
	output	reg	[31:0]							tx_start,
	output	reg	[31:0]							tx_length,
	output	reg	[31:0]							rx_start,
	output	reg	[31:0]							rx_length,
	output	reg	[31:0]							bf_start,
	output	reg	[31:0]							bf_length,
	output	reg	[3:0]							rtc_down,
	output	reg									spi_start,
	output	reg									scan_enable
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
	para_fanout_wren	<=	para_st_wren;
	para_fanout_rden	<=	para_st_rden;
	para_fanout_addr	<=	para_st_addr;
	para_fanout_wrdata	<=	para_st_wrdata;

	para_st_rddata		<=	para_fanout_rddata;
	para_st_rdvalid		<=	para_fanout_rdvalid;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												ram_demod_sin_en		,	ram_demod_sin_wren	;
reg												ram_demod_cos_en		,	ram_demod_cos_wren	;
reg												ram_log1_coef_en		,	ram_log1_coef_wren	;
reg												ram_tx_st_reg_en		,	ram_tx_st_reg_wren		;
reg												ram_rx_apod_data_en		,	ram_rx_apod_data_wren	;
reg												ram_rx_foc_elexy_en		,	ram_rx_foc_elexy_wren	;
reg												ram_filter_coef_en		,	ram_filter_coef_wren	;
reg												ram_scan_time_en		,	ram_scan_time_wren		;
//--------------------------------------------------------------------------------------------------
reg												ram_demod_sin_rden		,	ram_demod_sin_rdrdy	;
reg												ram_demod_cos_rden		,	ram_demod_cos_rdrdy	;
reg												ram_log1_coef_rden		,	ram_log1_coef_rdrdy	;
reg												ram_tx_st_reg_rden		,	ram_tx_st_reg_rdrdy	;
reg												ram_rx_foc_elexy_rden	,	ram_rx_foc_elexy_rdrdy	;
reg												ram_rx_apod_data_rden	,	ram_rx_apod_data_rdrdy	;
reg												ram_filter_coef_rden	,	ram_filter_coef_rdrdy	;
reg												ram_scan_time_rden		,	ram_scan_time_rdrdy	;
//--------------------------------------------------------------------------------------------------
reg				[19:0]							ram_demod_sin_addr	;
reg				[19:0]							ram_demod_cos_addr	;
reg				[19:0]							ram_log1_coef_addr	;
reg				[19:0]							ram_tx_st_reg_addr	;
reg				[19:0]							ram_rx_apod_data_addr	;
reg				[19:0]							ram_rx_foc_elexy_addr	;
reg				[19:0]							ram_filter_coef_addr	;
reg				[19:0]							ram_scan_time_addr	;
//--------------------------------------------------------------------------------------------------
reg				[31:0]							ram_demod_sin_wrdata;
reg				[31:0]							ram_demod_cos_wrdata;
reg				[31:0]							ram_log1_coef_wrdata;
reg				[31:0]							ram_tx_st_reg_wrdata;
reg				[31:0]							ram_rx_apod_data_wrdata;
reg				[31:0]							ram_rx_foc_elexy_wrdata;
reg				[31:0]							ram_filter_coef_wrdata;
reg				[31:0]							ram_scan_time_wrdata;
//--------------------------------------------------------------------------------------------------
wire			[31:0]							ram_demod_sin_rddata;
wire			[31:0]							ram_demod_cos_rddata;
wire			[31:0]							ram_log1_coef_rddata;
wire			[31:0]							ram_tx_st_reg_rddata;
wire			[31:0]							ram_rx_foc_elexy_rddata;
wire			[31:0]							ram_rx_apod_data_rddata;
wire			[31:0]							ram_filter_coef_rddata;
wire			[31:0]							ram_scan_time_rddata;
//--------------------------------------------------------------------------------------------------
reg												ram_notuse_addr_rden	,	ram_notuse_addr_rdrdy	;
//--------------------------------------------------------------------------------------------------
always@(posedge clk_para)
begin
	ram_demod_sin_en		<=	(para_fanout_addr[19:15] == (`ADDR_DEMOD_SIN  >> 15));
	ram_demod_cos_en		<=	(para_fanout_addr[19:15] == (`ADDR_DEMOD_COS  >> 15));
	ram_log1_coef_en		<=	(para_fanout_addr[19:12] == (`ADDR_LOG1_COEF  >> 12));
	ram_tx_st_reg_en		<=	(para_fanout_addr[19:9] == (`ADDR_TX_ST_REG  >> 9));
	ram_rx_apod_data_en		<=	(para_fanout_addr[19:8] == (`ADDR_RX_APOD_DATA  >> 8));
	ram_rx_foc_elexy_en		<=	(para_fanout_addr[19:8] == (`ADDR_RX_FOC_ELEXY  >> 8));
	ram_filter_coef_en		<=	(para_fanout_addr[19:8] == (`ADDR_FILTER_COEF  >> 8));
	ram_scan_time_en		<=	(para_fanout_addr[19:6] == (`ADDR_SCAN_TIME  >> 6));
	
	ram_demod_sin_wren		<=	(para_fanout_wren & (para_fanout_addr[19:15] == (`ADDR_DEMOD_SIN  >> 15)));
	ram_demod_cos_wren		<=	(para_fanout_wren & (para_fanout_addr[19:15] == (`ADDR_DEMOD_COS  >> 15)));
	ram_log1_coef_wren		<=	(para_fanout_wren & (para_fanout_addr[19:12] == (`ADDR_LOG1_COEF  >> 12)));
	ram_tx_st_reg_wren		<=	(para_fanout_wren & (para_fanout_addr[19:9] == (`ADDR_TX_ST_REG  >> 9)));
	ram_rx_apod_data_wren	<=	(para_fanout_wren & (para_fanout_addr[19:8] == (`ADDR_RX_APOD_DATA  >> 8)));
	ram_rx_foc_elexy_wren	<=	(para_fanout_wren & (para_fanout_addr[19:8] == (`ADDR_RX_FOC_ELEXY  >> 8)));
	ram_filter_coef_wren	<=	(para_fanout_wren & (para_fanout_addr[19:8] == (`ADDR_FILTER_COEF  >> 8)));
	ram_scan_time_wren		<=	(para_fanout_wren & (para_fanout_addr[19:6] == (`ADDR_SCAN_TIME  >> 6)));
	
	ram_demod_sin_rden		<=	(para_fanout_rden & (para_fanout_addr[19:15] == (`ADDR_DEMOD_SIN  >> 15)));
	ram_demod_cos_rden		<=	(para_fanout_rden & (para_fanout_addr[19:15] == (`ADDR_DEMOD_COS  >> 15)));
	ram_log1_coef_rden		<=	(para_fanout_rden & (para_fanout_addr[19:12] == (`ADDR_LOG1_COEF  >> 12)));
	ram_tx_st_reg_rden		<=	(para_fanout_rden & (para_fanout_addr[19:9] == (`ADDR_TX_ST_REG  >> 9)));
	ram_rx_apod_data_rden	<=	(para_fanout_rden & (para_fanout_addr[19:8] == (`ADDR_RX_APOD_DATA  >> 8)));
	ram_rx_foc_elexy_rden	<=	(para_fanout_rden & (para_fanout_addr[19:8] == (`ADDR_RX_FOC_ELEXY  >> 8)));
	ram_filter_coef_rden	<=	(para_fanout_rden & (para_fanout_addr[19:8] == (`ADDR_FILTER_COEF  >> 8)));
	ram_scan_time_rden		<=	(para_fanout_rden & (para_fanout_addr[19:6] == (`ADDR_SCAN_TIME  >> 6)));
	
	ram_demod_sin_rdrdy		<=	ram_demod_sin_rden;
	ram_demod_cos_rdrdy		<=	ram_demod_cos_rden;
	ram_log1_coef_rdrdy		<=	ram_log1_coef_rden;
	ram_tx_st_reg_rdrdy		<=	ram_tx_st_reg_rden;
	ram_rx_apod_data_rdrdy	<=	ram_rx_apod_data_rden;
	ram_rx_foc_elexy_rdrdy	<=	ram_rx_foc_elexy_rden;
	ram_filter_coef_rdrdy	<=	ram_filter_coef_rden;
	ram_scan_time_rdrdy		<=	ram_scan_time_rden;
	ram_notuse_addr_rdrdy	<=	ram_notuse_addr_rden;
	
	ram_demod_sin_addr		<=	{20'b0, para_fanout_addr[14:2]};		//	8192
	ram_demod_cos_addr		<=	{20'b0, para_fanout_addr[14:2]};		//	8192
	ram_log1_coef_addr		<=	{20'b0, para_fanout_addr[11:2]};		//	1024
	ram_tx_st_reg_addr		<=	{20'b0, para_fanout_addr[8:2]};		//	128	
	ram_rx_apod_data_addr	<=	{20'b0, para_fanout_addr[7:2]};		//	64	
	ram_rx_foc_elexy_addr	<=	{20'b0, para_fanout_addr[7:2]};		//	64	
	ram_filter_coef_addr	<=	{20'b0, para_fanout_addr[7:2]};		//	64	
	ram_scan_time_addr		<=	{20'b0, para_fanout_addr[5:2]};		//	16	
	
	ram_demod_sin_wrdata	<=	para_fanout_wrdata;
	ram_demod_cos_wrdata	<=	para_fanout_wrdata;
	ram_log1_coef_wrdata	<=	para_fanout_wrdata;
	ram_tx_st_reg_wrdata	<=	para_fanout_wrdata;
	ram_rx_apod_data_wrdata	<=	para_fanout_wrdata;
	ram_rx_foc_elexy_wrdata	<=	para_fanout_wrdata;
	ram_filter_coef_wrdata	<=	para_fanout_wrdata;
	ram_scan_time_wrdata	<=	para_fanout_wrdata;
	if(ram_tx_st_reg_wren & (ram_tx_st_reg_addr[6:0] == 7'h7F))		tx_st_reg_num	<=	ram_tx_st_reg_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd0))	rtc_rfdot		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd1))	rtc_period		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd2))	rt_length		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd3))	tx_start		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd4))	tx_length		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd5))	rx_start		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd6))	rx_length		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd7))	bf_start		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd8))	bf_length		<=	ram_scan_time_wrdata;
	if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd9))	rtc_down		<=	ram_scan_time_wrdata[3:0];
	if(reset) begin
		spi_start <= 1'b0;
		scan_enable <= 1'b0;
	end else begin
		if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd10))	spi_start		<=	ram_scan_time_wrdata[0];
		// Word 11 is the PS beamforming run request. scan_sync applies stop at a frame boundary.
		if(ram_scan_time_wren & (ram_scan_time_addr[19:0] == 20'd11))	scan_enable		<=	ram_scan_time_wrdata[0];
	end

		 if(ram_demod_sin_rdrdy		)	para_fanout_rddata	<=	ram_demod_sin_rddata;
	else if(ram_demod_cos_rdrdy		)	para_fanout_rddata	<=	ram_demod_cos_rddata;
	else if(ram_log1_coef_rdrdy		)	para_fanout_rddata	<=	ram_log1_coef_rddata;
	else if(ram_tx_st_reg_rdrdy		)	para_fanout_rddata	<=	ram_tx_st_reg_rddata;
	else if(ram_rx_apod_data_rdrdy	)	para_fanout_rddata	<=	ram_rx_apod_data_rddata;
	else if(ram_rx_foc_elexy_rdrdy	)	para_fanout_rddata	<=	ram_rx_foc_elexy_rddata;
	else if(ram_filter_coef_rdrdy	)	para_fanout_rddata	<=	ram_filter_coef_rddata;
	else if(ram_notuse_addr_rdrdy	)	para_fanout_rddata	<=	32'h_eeee_eeee;

		 if(ram_demod_sin_rdrdy		)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_demod_cos_rdrdy		)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_log1_coef_rdrdy		)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_tx_st_reg_rdrdy		)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_rx_apod_data_rdrdy	)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_rx_foc_elexy_rdrdy	)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_filter_coef_rdrdy	)	para_fanout_rdvalid	<=	1'b1;
	else if(ram_notuse_addr_rdrdy	)	para_fanout_rdvalid	<=	1'b1;
	else								para_fanout_rdvalid	<=	1'b0;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire			[31:0]							tx_st_reg_addr	;
wire			[31:0]							rx_apod_data_addr	;
wire			[31:0]							rx_foc_elexy_addr	;
wire			[31:0]							filter_coef_addr	;
wire			[31:0]							log2_coef_addr	;
assign			tx_st_reg_addr					=	{32'b0, tx_st_reg_rdaddr};
assign			rx_apod_data_addr				=	{32'b0, rx_apod_data_rdaddr};
assign			rx_foc_elexy_addr				=	{32'b0, rx_foc_elexy_rdaddr};
assign			filter_coef_addr				=	{32'b0, filter_coef_rdaddr};
assign			log2_coef_addr					=	{32'b0, log2_coef_rdaddr};
//--------------------------------------------------------------------------------------------------
tdpram_32bx8192									ram_demod_sin
(
	.clka										(clk_para),
	.ena										(ram_demod_sin_en),
	.wea										(ram_demod_sin_wren),
	.addra										(ram_demod_sin_addr),
	.dina										(ram_demod_sin_wrdata),
	.douta										(ram_demod_sin_rddata),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(demod_sin_rden),
	.addrb										(demod_sin_rdaddr),
	.doutb										(demod_sin_rddata)
);
//--------------------------------------------------------------------------------------------------
tdpram_32bx8192									ram_demod_cos
(
	.clka										(clk_para),
	.ena										(ram_demod_cos_en),
	.wea										(ram_demod_cos_wren),
	.addra										(ram_demod_cos_addr),
	.dina										(ram_demod_cos_wrdata),
	.douta										(ram_demod_cos_rddata),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(demod_cos_rden),
	.addrb										(demod_cos_rdaddr),
	.doutb										(demod_cos_rddata)
);
//--------------------------------------------------------------------------------------------------
tdpram_32bx1024									ram_log1_coef
(
	.clka										(clk_para),
	.ena										(ram_log1_coef_en),
	.wea										(ram_log1_coef_wren),
	.addra										(ram_log1_coef_addr),
	.dina										(ram_log1_coef_wrdata),
	.douta										(ram_log1_coef_rddata),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(log1_coef_rden),
	.addrb										(log1_coef_rdaddr),
	.doutb										(log1_coef_rddata)
);

tdpram_32bx1024									ram_log2_coef
(
	.clka										(clk_para),
	.ena										(ram_log1_coef_en),
	.wea										(ram_log1_coef_wren),
	.addra										(ram_log1_coef_addr),
	.dina										(ram_log1_coef_wrdata),
	.douta										(),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(log2_coef_rden),
	.addrb										(log2_coef_addr),
	.doutb										(log2_coef_rddata)
);
//--------------------------------------------------------------------------------------------------
tdpram_32bx1024									ram_st_reg
(
	.clka										(clk_para),
	.ena										(ram_tx_st_reg_en),
	.wea										(ram_tx_st_reg_wren),
	.addra										(ram_tx_st_reg_addr),
	.dina										(ram_tx_st_reg_wrdata),
	.douta										(ram_tx_st_reg_rddata),
	.clkb										(clk_200),
	.web										(0),
	.dinb										(0),
	.enb										(tx_st_reg_rden),
	.addrb										(tx_st_reg_addr),
	.doutb										(tx_st_reg_rddata)
);
//--------------------------------------------------------------------------------------------------
tdpram_16bx1024									ram_rx_apod_data
(
	.clka										(clk_para),
	.ena										(ram_rx_apod_data_en),
	.wea										(ram_rx_apod_data_wren),
	.addra										(ram_rx_apod_data_addr),
	.dina										(ram_rx_apod_data_wrdata[15:0]),
	.douta										(ram_rx_apod_data_rddata),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(rx_apod_data_rden),
	.addrb										(rx_apod_data_addr),
	.doutb										(rx_apod_data_rddata)
);
//--------------------------------------------------------------------------------------------------
tdpram_16bx1024									ram_rx_foc_elexy
(
	.clka										(clk_para),
	.ena										(ram_rx_foc_elexy_en),
	.wea										(ram_rx_foc_elexy_wren),
	.addra										(ram_rx_foc_elexy_addr),
	.dina										(ram_rx_foc_elexy_wrdata[15:0]),
	.douta										(ram_rx_foc_elexy_rddata),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(rx_foc_elexy_rden),
	.addrb										(rx_foc_elexy_addr),
	.doutb										(rx_foc_elexy_rddata)
);
//--------------------------------------------------------------------------------------------------
tdpram_16bx1024									ram_filter_coef
(
	.clka										(clk_para),
	.ena										(ram_filter_coef_en),
	.wea										(ram_filter_coef_wren),
	.addra										(ram_filter_coef_addr),
	.dina										(ram_filter_coef_wrdata[15:0]),
	.douta										(ram_filter_coef_rddata),
	.clkb										(clk_25),
	.web										(0),
	.dinb										(0),
	.enb										(filter_coef_rden),
	.addrb										(filter_coef_addr),
	.doutb										(filter_coef_rddata)
);
//--------------------------------------------------------------------------------------------------
endmodule
