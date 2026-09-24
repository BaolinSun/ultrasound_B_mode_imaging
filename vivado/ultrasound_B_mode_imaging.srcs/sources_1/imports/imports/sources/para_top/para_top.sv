//--------------------------------------------------------------------------------------------------
`include "fpga_define.vh"
//--------------------------------------------------------------------------------------------------
module para_top
(
	input										clk_para,
	input										clk_200,
	input										clk_25,
	input										reset,
	input										rtc_trig,
	input										para_wren,
	input										para_rden,
	input		[23:0]							para_addr,
	input		[31:0]							para_wrdata,
	output	reg	[31:0]							para_rddata,
	output	reg									para_rdvalid,

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
	output		[6:0]							tx_st_reg_num,
	
	input										rx_apod_data_rden,
	input		[5:0]							rx_apod_data_rdaddr,
	output		[7:0]							rx_apod_data_rddata,
	input										rx_foc_elexy_rden,
	input		[5:0]							rx_foc_elexy_rdaddr,
	output		[15:0]							rx_foc_elexy_rddata,

	input										filter_coef_rden,
	input		[5:0]							filter_coef_rdaddr,
	output		[15:0]							filter_coef_rddata,

	input										tx_rt_reg_rden,
	input		[13:0]							tx_rt_reg_rdaddr,
	output		[31:0]							tx_rt_reg_rddata,
	output		[4:0]							tx_rt_reg_num,
	
	input										rx_foc_delta_rden,
	input		[5:0]							rx_foc_delta_rdaddr,
	output		[31:0]							rx_foc_delta_rddata,
	
	output										scan_enable,
	output										rtc_enable,
	output		[31:0]							rtc_rfdot,
	output		[31:0]							rtc_period,
	output		[31:0]							rt_length,
	output		[31:0]							tx_start,
	output		[31:0]							tx_length,
	output		[31:0]							rx_start,
	output		[31:0]							rx_length,
	output		[31:0]							bf_start,
	output		[31:0]							bf_length,
	output		[3:0]							rtc_down,
	output		[3:0]							gain_shift,
	
	output										spi_start,
	output										tx_cfg_reset,
	output										tx_spi_rdreq,
	output		[31:0]							tx_spi_rdaddr,
	input										tx_spi_rxvalid,
	input		[31:0]							tx_spi_rxdata,
	output		[31:0]							tx_cfg_rddata_debug

);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												para_st_wren	;
reg												para_st_rden	;
reg				[19:0]							para_st_addr	;
reg				[31:0]							para_st_wrdata	;
wire			[31:0]							para_st_rddata	;
wire											para_st_rdvalid	;
reg												para_rt_wren	;
reg												para_rt_rden	;
reg				[19:0]							para_rt_addr	;
reg				[31:0]							para_rt_wrdata	;
wire			[31:0]							para_rt_rddata	;
wire											para_rt_rdvalid	;
reg												para_reg_wren	;
reg												para_reg_rden	;
reg				[19:0]							para_reg_addr	;
reg				[31:0]							para_reg_wrdata	;
wire			[31:0]							para_reg_rddata	;
wire											para_reg_rdvalid;
reg				[19:0]							para_rd_addr	;

always@(posedge clk_para)
begin
	para_st_wren	<=	(para_wren & (para_addr[22:20] == `ADDR_SEG_ST));
	para_rt_wren	<=	(para_wren & (para_addr[22:20] == `ADDR_SEG_RT));
	para_reg_wren	<=	(para_wren & (para_addr[22:20] == `ADDR_SEG_REG));

	para_st_rden	<=	(para_rden & (para_addr[22:20] == `ADDR_SEG_ST));
	para_rt_rden	<=	(para_rden & (para_addr[22:20] == `ADDR_SEG_RT));
	para_reg_rden	<=	(para_rden & (para_addr[22:20] == `ADDR_SEG_REG));

	para_rd_addr	<=	para_rden ? para_addr[19:0] : para_rd_addr;
	para_st_addr	<=	para_addr[19:0];
	para_rt_addr	<=	para_addr[19:0];
	para_reg_addr	<=	para_addr[19:0];

	para_st_wrdata	<=	para_wrdata;
	para_rt_wrdata	<=	para_wrdata;
	para_reg_wrdata	<=	para_wrdata;

	case(para_addr[22:20])
		`ADDR_SEG_RT:	para_rddata		<=	para_rt_rddata;
		`ADDR_SEG_REG:	para_rddata		<=	para_reg_rddata;
		default:		para_rddata		<=	para_st_rddata;
	endcase

	case(para_addr[22:20])
		`ADDR_SEG_RT:	para_rdvalid	<=	para_rt_rdvalid;
		`ADDR_SEG_REG:	para_rdvalid	<=	para_reg_rdvalid;
		default:		para_rdvalid	<=	para_st_rdvalid;
	endcase
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
para_st											para_st_i
(
	.clk_para									(clk_para),
	.clk_200									(clk_200),
	.clk_25										(clk_25),
	.reset										(reset),
	.para_st_wren								(para_st_wren),
	.para_st_rden								(para_st_rden),
	.para_st_addr								(para_st_addr),
	.para_st_wrdata								(para_st_wrdata),
	.para_st_rddata								(para_st_rddata),
	.para_st_rdvalid							(para_st_rdvalid),

	.demod_sin_rden								(demod_sin_rden	),
	.demod_sin_rdaddr							(demod_sin_rdaddr),
	.demod_sin_rddata							(demod_sin_rddata),
	.demod_cos_rden								(demod_cos_rden	),
	.demod_cos_rdaddr							(demod_cos_rdaddr),
	.demod_cos_rddata							(demod_cos_rddata),
	
	.log1_coef_rden								(log1_coef_rden),
	.log1_coef_rdaddr							(log1_coef_rdaddr),
	.log1_coef_rddata							(log1_coef_rddata),
	.log2_coef_rden								(log2_coef_rden),
	.log2_coef_rdaddr							(log2_coef_rdaddr),
	.log2_coef_rddata							(log2_coef_rddata),

	.tx_st_reg_rden								(tx_st_reg_rden),
	.tx_st_reg_rdaddr							(tx_st_reg_rdaddr),
	.tx_st_reg_rddata							(tx_st_reg_rddata),
	.tx_st_reg_num								(tx_st_reg_num),
	
	.rx_apod_data_rden							(rx_apod_data_rden),
	.rx_apod_data_rdaddr						(rx_apod_data_rdaddr),
	.rx_apod_data_rddata						(rx_apod_data_rddata),
	.rx_foc_elexy_rden							(rx_foc_elexy_rden),
	.rx_foc_elexy_rdaddr						(rx_foc_elexy_rdaddr),
	.rx_foc_elexy_rddata						(rx_foc_elexy_rddata),

	.filter_coef_rden							(filter_coef_rden),
	.filter_coef_rdaddr							(filter_coef_rdaddr),
	.filter_coef_rddata							(filter_coef_rddata),

	.rtc_rfdot									(rtc_rfdot),
	.rtc_period									(rtc_period),
	.rt_length									(rt_length),
	.tx_start									(tx_start),
	.tx_length									(tx_length),
	.rx_start									(rx_start),
	.rx_length									(rx_length),
	.bf_start									(bf_start),
	.bf_length									(bf_length),
	.rtc_down									(rtc_down),
	.gain_shift									(gain_shift),
	.spi_start									(spi_start),
	.scan_enable								(scan_enable)
);

para_rt											para_rt_i
(
	.clk_para									(clk_para),
	.clk_200									(clk_200),
	.clk_25										(clk_25),
	.reset										(reset),
	.para_rt_wren								(para_rt_wren),
	.para_rt_rden								(para_rt_rden),
	.para_rt_addr								(para_rt_addr),
	.para_rt_wrdata								(para_rt_wrdata),
	.para_rt_rddata								(para_rt_rddata),
	.para_rt_rdvalid							(para_rt_rdvalid),

	.tx_rt_reg_rden								(tx_rt_reg_rden),
	.tx_rt_reg_rdaddr							(tx_rt_reg_rdaddr),
	.tx_rt_reg_rddata							(tx_rt_reg_rddata),
	.tx_rt_reg_num								(tx_rt_reg_num),
	
	.rx_foc_delta_rden							(rx_foc_delta_rden),
	.rx_foc_delta_rdaddr						(rx_foc_delta_rdaddr),
	.rx_foc_delta_rddata						(rx_foc_delta_rddata)
);


para_reg										para_reg_i
(			
	.clk										(clk_para),
	.reset										(reset),
	.para_reg_wren								(para_reg_wren),
	.para_reg_rden								(para_reg_rden),
	.para_reg_addr								(para_reg_addr),
	.para_reg_wrdata							(para_reg_wrdata),
	.para_reg_rddata							(para_reg_rddata),
	.para_reg_rdvalid							(para_reg_rdvalid),

	.tx_spi_rdreq								(tx_spi_rdreq),
	.tx_spi_rdaddr								(tx_spi_rdaddr),
	.tx_spi_rxvalid								(tx_spi_rxvalid),
	.tx_spi_rxdata								(tx_spi_rxdata),
	.tx_cfg_rddata_debug						(tx_cfg_rddata_debug),
	.rtc_enable									(rtc_enable),
	.tx_hw_reset								(tx_cfg_reset)

);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
