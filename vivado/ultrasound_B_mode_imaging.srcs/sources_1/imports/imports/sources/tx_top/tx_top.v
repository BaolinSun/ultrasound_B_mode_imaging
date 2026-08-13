module tx_top
(
	input								clk,
	input								reset,

	input								scan_enable,
	input								rtc_trig,
	input	[6:0]						rtc_line,
	input								spi_start,
	input	[6:0]						tx_st_reg_num,
	output								tx_st_reg_rden,
	output	[8:0]						tx_st_reg_rdaddr,
	input	[31:0]						tx_st_reg_rddata,
	
	input	[4:0]						tx_rt_reg_num,
	output								tx_rt_reg_rden,
	output	[13:0]						tx_rt_reg_rdaddr,
	input	[31:0]						tx_rt_reg_rddata,
		
	input								tx_spi_rdreq,
	input	[9:0]						tx_spi_rdaddr,
	output	reg[31:0]					tx_spi_rxdata,
	output	reg							tx_spi_rxvalid,

	input								tx_cfg_reset,			//- 1clk 脉冲自清除复位信�?
	output								tx_dsel0,				//-	tx管脚，dsel0
	output								tx_dsel1,				//-	tx管脚，dsel1
	output								tx_reset,				//	发射控制管脚
	output	[7:0]						tr_en,
	output								tx_stdby,
	output								tx_cw_en,
	output								ctrl_spi_rdy,
	
	output	[1:0]						tx_sclk,				//	发射配置通讯管脚
	output	[1:0]						tx_sen0,				//	发射配置通讯管脚
	output	[1:0]						tx_sen1,				//	发射配置通讯管脚
	output	[1:0]						tx_dat0,				//	发射配置通讯管脚
	output	[1:0]						tx_dat1,				//	发射配置通讯管脚
	input	[1:0]						tx_sdout				//	发射配置通讯管脚
);

wire									tx_spi_trig0;
wire									tx_spi_mode0;			//	0=tx	1=rx
wire		[9:0]						tx0_spi_addr0;
wire		[31:0]						tx0_spi_txdata0;
wire		[9:0]						tx1_spi_addr0;
wire		[31:0]						tx1_spi_txdata0;
wire									tx_spi_trig1;
wire									tx_spi_mode1;
wire		[9:0]						tx0_spi_addr1;
wire		[31:0]						tx0_spi_txdata1;
wire		[9:0]						tx1_spi_addr1;
wire		[31:0]						tx1_spi_txdata1;
wire									tx_spi_rdnext;
wire									tx_spi_done0;
wire									tx_spi_done1;
wire									tx0_sclk0,tx0_sclk1;
wire									tx1_sclk0,tx1_sclk1;
wire	[31:0]							tx_spi_rxdata1;
wire									tx_spi_rxvalid1;

tx_cfg_top								tx_cfg_top_i
(
	.clk								(clk					),
	.reset								(reset					),
	.scan_enable						(scan_enable			),
	.rtc_trig							(rtc_trig				),
	.rtc_line							(rtc_line				),
	.spi_start							(spi_start				),
	.tx_st_reg_rden						(tx_st_reg_rden			),
	.tx_st_reg_rdaddr					(tx_st_reg_rdaddr		),
	.tx_st_reg_rddata					(tx_st_reg_rddata		),
	.tx_st_reg_num						(tx_st_reg_num			),
	.tx_rt_reg_rden						(tx_rt_reg_rden			),
	.tx_rt_reg_rdaddr					(tx_rt_reg_rdaddr		),
	.tx_rt_reg_rddata					(tx_rt_reg_rddata		),
	.tx_rt_reg_num						(tx_rt_reg_num			),
	.tx_spi_rdreq						(tx_spi_rdreq			),
	.tx_spi_rdaddr						(tx_spi_rdaddr			),
	
	.tx_spi_trig0						(tx_spi_trig0			),
	.tx_spi_mode0						(tx_spi_mode0			),
	.tx0_spi_addr0						(tx0_spi_addr0			),
	.tx0_spi_txdata0					(tx0_spi_txdata0		),
	.tx_spi_trig1						(tx_spi_trig1			),
	.tx_spi_mode1						(tx_spi_mode1			),
	.tx0_spi_addr1						(tx0_spi_addr1			),
	.tx0_spi_txdata1					(tx0_spi_txdata1		),
	.tx1_spi_addr0						(tx1_spi_addr0			),
	.tx1_spi_txdata0					(tx1_spi_txdata0		),
	.tx1_spi_addr1						(tx1_spi_addr1			),
	.tx1_spi_txdata1					(tx1_spi_txdata1		),
	.tx_spi_done0						(tx_spi_done0			),
	.tx_spi_done1						(tx_spi_done1			),
	.tx_spi_rdnext						(tx_spi_rdnext			)
);

//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------


tx_spi									tx_spi_0
(				
	.clk								(clk				),
	.reset								(reset				),
	.spi_trig							(tx_spi_trig0		),
	.spi_mode							(tx_spi_mode0		),
	.spi_addr							(tx0_spi_addr0	),
	.spi_txdata							(tx0_spi_txdata0	),
	.spi_rxdata							(tx_spi_rxdata0		),
	.spi_rxvalid						(tx_spi_rxvalid0	),
	.spi_done							(tx_spi_done0		),
	.spi_rdnext							(tx_spi_rdnext		),
	.o_spi_sen							(tx_sen0[0]		),
	.o_spi_sclk							(tx0_sclk0		),
	.o_spi_sdata						(tx_dat0[0]		),
	.i_spi_sdout						(tx_sdout[0]	)
);

assign tx_sclk[0] = tx0_sclk0 & tx0_sclk1;
assign tx_sclk[1] = tx1_sclk0 & tx1_sclk1;
always@(posedge clk)
begin
	tx_spi_rxdata <= tx_spi_rxdata0 | tx_spi_rxdata1;
	tx_spi_rxvalid <= tx_spi_rxvalid1;
end

tx_spi									tx_spi_1
(				
	.clk								(clk			),
	.reset								(reset			),
	.spi_trig							(tx_spi_trig1	),
	.spi_mode							(tx_spi_mode1	),
	.spi_addr							(tx0_spi_addr1	),
	.spi_txdata							(tx0_spi_txdata1),
	.spi_rxdata							(tx_spi_rxdata1	),
	.spi_rxvalid						(tx_spi_rxvalid1),
	.spi_done							(tx_spi_done1	),
	.spi_rdnext							(				),
	.o_spi_sen							(tx_sen1[0]		),
	.o_spi_sclk							(tx0_sclk1		),
	.o_spi_sdata						(tx_dat1[0]		),
	.i_spi_sdout						(tx_sdout[0]	)
);

tx_spi									tx_spi_2
(
	.clk								(clk			),
	.reset								(reset			),
	.spi_trig							(tx_spi_trig0	),
	.spi_mode							(tx_spi_mode0	),
	.spi_addr							(tx1_spi_addr0	),
	.spi_txdata							(tx1_spi_txdata0),
	.spi_rxdata							(				),
	.spi_rxvalid						(				),
	.spi_done							(				),
	.spi_rdnext							(				),
	.o_spi_sen							(tx_sen0[1]		),
	.o_spi_sclk							(tx1_sclk0		),
	.o_spi_sdata						(tx_dat0[1]		),
	.i_spi_sdout						(tx_sdout[1]	)
);

tx_spi									tx_spi_3
(				
	.clk								(clk			),
	.reset								(reset			),
	.spi_trig							(tx_spi_trig1	),
	.spi_mode							(tx_spi_mode1	),
	.spi_addr							(tx1_spi_addr1	),
	.spi_txdata							(tx1_spi_txdata1),
	.spi_rxdata							(				),
	.spi_rxvalid						(				),
	.spi_done							(				),
	.spi_rdnext							(				),
	.o_spi_sen							(tx_sen1[1]		),
	.o_spi_sclk							(tx1_sclk1		),
	.o_spi_sdata						(tx_dat1[1]		),
	.i_spi_sdout						(tx_sdout[1]	)
);
//--------------------------------------------------------------------------------------------------
ila_tx						ila_tx_i
(
	.clk					(clk),
	.probe0					(spi_start),
	.probe1					(rtc_trig),
	.probe2					(tx_st_reg_num),
	.probe3					(tx_st_reg_rden),
	.probe4					(tx_st_reg_rdaddr),
	.probe5					(tx_st_reg_rddata),
	.probe6					({tx_spi_0.o_spi_sen,tx_spi_0.o_spi_sclk,tx_spi_0.o_spi_sdata}),
	.probe7					({tx_spi_1.o_spi_sen,tx_spi_1.o_spi_sclk,tx_spi_1.o_spi_sdata}),
	.probe8					({tx_spi_2.o_spi_sen,tx_spi_2.o_spi_sclk,tx_spi_2.o_spi_sdata}),
	.probe9					({tx_spi_3.o_spi_sen,tx_spi_3.o_spi_sclk,tx_spi_3.o_spi_sdata})
);
//--------------------------------------------------------------------------------------------------
tx_ctrl									tx_ctrl_i
(
	.clk								(clk			),
	.reset								(reset			),
	.tx_cfg_reset						(tx_cfg_reset	),
	.tx_reset							(tx_reset		),
	.tx_dsel0							(tx_dsel0		),
	.tx_dsel1							(tx_dsel1		),
	.ctrl_spi_rdy						(ctrl_spi_rdy	)
);
//--------------------------------------------------------------------------------------------------
assign tr_en = 8'b0000_0000;
assign tx_stdby = 1'b0;
assign tx_cw_en = 1'b0;
//--------------------------------------------------------------------------------------------------
endmodule
