module tx_top
(
	input								clk,
	input								reset,

	input								scan_enable,
	input								tx_cfg_req,
	input	[6:0]						rtc_line,
	input								rtc_polarity,
	output								tx_cfg_ready,
	output								tx_cfg_done,
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
	input	[31:0]						tx_cfg_rddata_debug,

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
wire	[31:0]							tx_spi_rxdata0;
wire									tx_spi_rxvalid0;
wire	[31:0]							tx_spi_rxdata1;
wire									tx_spi_rxvalid1;
reg		[31:0]							tx_spi_die1_rddata;
reg		[31:0]							tx_spi_die2_rddata;
reg									tx_spi_die1_rdvalid;
reg									tx_spi_die2_rdvalid;
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO", DONT_TOUCH = "TRUE" *) reg [1:0] tx_spi_rdreq_sync;
reg tx_spi_rdreq_seen;
reg tx_spi_rdreq_pending;
reg [9:0] tx_spi_rdaddr_latched;
wire tx_spi_rdaccept;

// Toggle synchronizer for clk_para -> clk_200.  The bundled address is held
// by para_reg until the completion toggle returns, so it is stable before the
// second synchronized request stage changes.
always @(posedge clk)
begin
	if(reset) begin
		tx_spi_rdreq_sync <= 2'b00;
		tx_spi_rdreq_seen <= 1'b0;
		tx_spi_rdreq_pending <= 1'b0;
		tx_spi_rdaddr_latched <= 10'd0;
	end else begin
		tx_spi_rdreq_sync <= {tx_spi_rdreq_sync[0], tx_spi_rdreq};
		if(tx_spi_rdreq_sync[1] != tx_spi_rdreq_seen) begin
			tx_spi_rdreq_seen <= tx_spi_rdreq_sync[1];
			tx_spi_rdreq_pending <= 1'b1;
			tx_spi_rdaddr_latched <= tx_spi_rdaddr;
		end
		if(tx_spi_rdaccept)
			tx_spi_rdreq_pending <= 1'b0;
	end
end

tx_cfg_top								tx_cfg_top_i
(
	.clk								(clk					),
	.reset								(reset					),
	.scan_enable						(scan_enable			),
	.tx_cfg_req						(tx_cfg_req				),
	.rtc_line							(rtc_line				),
	.rtc_polarity						(rtc_polarity			),
	.tx_cfg_ready						(tx_cfg_ready			),
	.tx_cfg_done						(tx_cfg_done			),
	.spi_start							(spi_start				),
	.tx_st_reg_rden						(tx_st_reg_rden			),
	.tx_st_reg_rdaddr					(tx_st_reg_rdaddr		),
	.tx_st_reg_rddata					(tx_st_reg_rddata		),
	.tx_st_reg_num						(tx_st_reg_num			),
	.tx_rt_reg_rden						(tx_rt_reg_rden			),
	.tx_rt_reg_rdaddr					(tx_rt_reg_rdaddr		),
	.tx_rt_reg_rddata					(tx_rt_reg_rddata		),
	.tx_rt_reg_num						(tx_rt_reg_num			),
	.tx_spi_rdreq						(tx_spi_rdreq_pending	),
	.tx_spi_rdaddr						(tx_spi_rdaddr_latched	),
	.tx_spi_rdaccept					(tx_spi_rdaccept		),
	
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

// TX7332 readback returns die 1 and die 2 on separate SPI transactions.
// Latch both valid results and apply the datasheet-required OR only after
// both transactions have completed.
always @(posedge clk)
begin
	if (reset)
	begin
		tx_spi_die1_rddata  <= 32'd0;
		tx_spi_die2_rddata  <= 32'd0;
		tx_spi_die1_rdvalid <= 1'b0;
		tx_spi_die2_rdvalid <= 1'b0;
		tx_spi_rxdata       <= 32'd0;
		tx_spi_rxvalid      <= 1'b0;
	end
	else
	begin
		if(tx_spi_rdaccept) begin
			tx_spi_die1_rdvalid <= 1'b0;
			tx_spi_die2_rdvalid <= 1'b0;
		end

		if (tx_spi_rxvalid0)
		begin
			tx_spi_die1_rddata  <= tx_spi_rxdata0;
			tx_spi_die1_rdvalid <= 1'b1;
		end

		if (tx_spi_rxvalid1)
		begin
			tx_spi_die2_rddata  <= tx_spi_rxdata1;
			tx_spi_die2_rdvalid <= 1'b1;
		end

		if (tx_spi_die1_rdvalid && tx_spi_die2_rdvalid)
		begin
			tx_spi_rxdata       <= tx_spi_die1_rddata | tx_spi_die2_rddata;
			// Completion is also a toggle.  The result stays stable until the
			// next completed read and is captured after two clk_para stages.
			tx_spi_rxvalid      <= ~tx_spi_rxvalid;
			tx_spi_die1_rdvalid <= 1'b0;
			tx_spi_die2_rdvalid <= 1'b0;
		end
	end
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
	// Read request and requested TX7332 register address.
	.probe0					(tx_spi_rdaccept),
	.probe1					(tx_spi_rdaddr_latched),
	// {physical SDOUT, die1 rxvalid, die2 rxvalid}.
	.probe2					({tx_sdout[0], tx_spi_rxvalid0, tx_spi_rxvalid1}),
	// Raw 32-bit data returned by the two die read transactions.
	.probe3					(tx_spi_rxdata0),
	.probe4					(tx_spi_rxdata1),
	// OR-combined result presented to para_reg.
	.probe5					(tx_spi_rxdata),
	// SPI transactions for die 1 and die 2 of the observed TX7332.
	.probe6					({tx_spi_0.o_spi_sen, tx_spi_0.o_spi_sclk, tx_spi_0.o_spi_sdata}),
	.probe7					({tx_spi_1.o_spi_sen, tx_spi_1.o_spi_sclk, tx_spi_1.o_spi_sdata}),
	// {combined rxvalid, latched die1 valid, latched die2 valid}.
	.probe8					({tx_spi_rxvalid, tx_spi_die1_rdvalid, tx_spi_die2_rdvalid}),
	// Value retained in para_reg and returned to PS software.
	.probe9					(tx_cfg_rddata_debug)
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
