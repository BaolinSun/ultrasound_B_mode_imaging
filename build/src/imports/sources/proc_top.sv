module proc_top
(
    output reg up_header,
    output reg up_last,
    output wire capture_busy,
	input								clk_para,
	input								clk_200,
	input								clk_25,
	input								reset,
	input								para_wren,
	input								para_rden,
	input		[23:0]					para_addr,
	input		[31:0]					para_wrdata,
	output	reg	[31:0]					para_rddata,
	output	reg							para_rdvalid,
	output		[31:0]					tx_cfg_rddata_debug,

	output								tx_dsel0,				//-	tx管脚，dsel0
	output								tx_dsel1,				//-	tx管脚，dsel1
	output								tx_reset,				//	发射控制管脚
	output		[7:0]					tr_en,
	output								tx_stdby,
	output								tx_cw_en,
	
	output		[1:0]					tx_sclk,				//	发射配置通讯管脚
	output		[1:0]					tx_sen0,				//	发射配置通讯管脚
	output		[1:0]					tx_sen1,				//	发射配置通讯管脚
	output		[1:0]					tx_dat0,				//	发射配置通讯管脚
	output		[1:0]					tx_dat1,				//	发射配置通讯管脚
	input		[1:0]					tx_sdout,				//	发射配置通讯管脚

	input								data_en,
	input		[511:0]					data_ch,
	output								log_start,
	output								log_last,
	output	reg							up_dataen,
	output	reg	[15:0]					up_data,
	output		[31:0]					rtc_rfdot,
		
	output								rtc_txen,
	output								rtc_rxen,
	output								rtc_tgc_slope,
	output								rtc_tgc_updown,
	output		[6:0]					rtc_line
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire									demod_sin_rden;
wire	[12:0]							demod_sin_rdaddr;
wire	[31:0]							demod_sin_rddata;
wire									demod_cos_rden;
wire	[12:0]							demod_cos_rdaddr;
wire	[31:0]							demod_cos_rddata;
wire									log1_coef_rden;
wire	[9:0]							log1_coef_rdaddr;
wire	[31:0]							log1_coef_rddata;
wire									log2_coef_rden;
wire	[5:0]							log2_coef_rdaddr;
wire	[31:0]							log2_coef_rddata;
wire									tx_st_reg_rden;
wire	[8:0]							tx_st_reg_rdaddr;
wire	[31:0]							tx_st_reg_rddata;
wire	[6:0]							tx_st_reg_num;
wire									tx_st_wave_wrdone;
wire									tx_rt_reg_rden;
wire	[13:0]							tx_rt_reg_rdaddr;
wire	[31:0]							tx_rt_reg_rddata;
wire	[4:0]							tx_rt_reg_num;
wire									rx_apod_data_rden;
wire	[5:0]							rx_apod_data_rdaddr;
wire	[7:0]							rx_apod_data_rddata;
wire									rx_foc_elexy_rden;
wire	[5:0]							rx_foc_elexy_rdaddr;
wire	[15:0]							rx_foc_elexy_rddata;
wire									filter_coef_rden;
wire	[5:0]							filter_coef_rdaddr;
wire	[15:0]							filter_coef_rddata;
wire									tx_spi_rdreq;
wire	[31:0]							tx_spi_rdaddr;
wire									rx_foc_delta_rden;
wire	[5:0]							rx_foc_delta_rdaddr;
reg		[31:0]							rx_foc_delta_rddata;
wire	[31:0]							rtc_period;
wire	[31:0]							rt_length;
wire	[31:0]							tx_start;
wire	[31:0]							tx_length;
wire	[31:0]							rx_start;
wire	[31:0]							rx_length;
wire	[31:0]							bf_start;
wire	[31:0]							bf_length;
wire	[3:0]							rtc_down;
wire	[3:0]							gain_shift;
wire									spi_start;
reg										i_spi_sdout;
wire	[31:0]							tx_spi_rxdata;
wire									tx_spi_rxvalid;
wire	[31:0]							tx_spi_rxdata0;
wire									tx_spi_rxvalid0;
wire	[31:0]							tx_spi_rxdata1;
wire									tx_spi_rxvalid1;
wire									tx_cfg_reset;
wire									ctrl_spi_rdy;

wire									rtc_trig;
wire									rtc_bfen;
wire									tx_cfg_req;
wire									tx_cfg_ready;
wire									tx_cfg_done;
wire		[7:0]						rtc_shot;
wire									beamform_active;
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO", DONT_TOUCH = "TRUE" *) reg beamform_active_sync_ff1;
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO", DONT_TOUCH = "TRUE" *) reg beamform_active_sync_ff2;
reg										ad_en_in;
reg		[31:0][15:0]					ad_ch_in;
wire									rx_physical_valid;
wire	[511:0]						rx_physical_data;
wire									scan_enable;
wire									rtc_enable;
wire									log_headen;
wire		[15:0]						log_head;
wire									log_dataen;
wire		[15:0]						log_data;
//--------------------------------------------------------------------------------------------------
// Synchronize the actual scan state before it enters the 25 MHz processing domain.
always@(posedge clk_25)
begin
	if(reset) begin
		beamform_active_sync_ff1 <= 1'b0;
		beamform_active_sync_ff2 <= 1'b0;
	end else begin
		beamform_active_sync_ff1 <= beamform_active;
		beamform_active_sync_ff2 <= beamform_active_sync_ff1;
	end
end
//--------------------------------------------------------------------------------------------------
// CDC boundary note: data_en/data_ch are already synchronous to clk_25 here.
// This fixed board-wiring permutation is combinational and adds no clock domain.
rx_channel_reorder					rx_channel_reorder_i
(
	.raw_valid						(data_en			),
	.raw_data						(data_ch			),
	.physical_valid				(rx_physical_valid	),
	.physical_data					(rx_physical_data	)
);

integer ad_channel_index;
always_ff @(posedge clk_25)
begin
	ad_en_in <= rx_physical_valid;
	for(ad_channel_index = 0; ad_channel_index < 32; ad_channel_index = ad_channel_index + 1) begin
		ad_ch_in[ad_channel_index] <= rx_physical_valid? rx_physical_data[ad_channel_index*16 +: 16] : 16'd0;
	end
end
//--------------------------------------------------------------------------------------------------
para_top								para_top_i
(
	.clk_para							(clk_para			),
	.clk_200							(clk_200			),
	.clk_25								(clk_25				),
	.reset								(reset				),
	.rtc_trig							(rtc_trig			),
	.para_wren							(para_wren			),
	.para_rden							(para_rden			),
	.para_addr							(para_addr			),
	.para_wrdata						(para_wrdata		),
	.para_rddata						(para_rddata		),
	.para_rdvalid						(para_rdvalid		),
	.demod_sin_rden						(demod_sin_rden	),
	.demod_sin_rdaddr					(demod_sin_rdaddr),
	.demod_sin_rddata					(demod_sin_rddata),
	.demod_cos_rden						(demod_cos_rden	),
	.demod_cos_rdaddr					(demod_cos_rdaddr),
	.demod_cos_rddata					(demod_cos_rddata),
	.log1_coef_rden						(log1_coef_rden),
	.log1_coef_rdaddr					(log1_coef_rdaddr),
	.log1_coef_rddata					(log1_coef_rddata),
	.log2_coef_rden						(log2_coef_rden),
	.log2_coef_rdaddr					(log2_coef_rdaddr),
	.log2_coef_rddata					(log2_coef_rddata),
	.tx_st_reg_rden						(tx_st_reg_rden		),
	.tx_st_reg_rdaddr					(tx_st_reg_rdaddr	),
	.tx_st_reg_rddata					(tx_st_reg_rddata	),
	.tx_st_reg_num						(tx_st_reg_num		),
	.rx_apod_data_rden					(rx_apod_data_rden	),
	.rx_apod_data_rdaddr				(rx_apod_data_rdaddr),
	.rx_apod_data_rddata				(rx_apod_data_rddata),
	.rx_foc_elexy_rden					(rx_foc_elexy_rden	),
	.rx_foc_elexy_rdaddr				(rx_foc_elexy_rdaddr),
	.rx_foc_elexy_rddata				(rx_foc_elexy_rddata),
	.filter_coef_rden					(filter_coef_rden	),
	.filter_coef_rdaddr					(filter_coef_rdaddr),
	.filter_coef_rddata					(filter_coef_rddata),
	.tx_rt_reg_rden						(tx_rt_reg_rden		),
	.tx_rt_reg_rdaddr					(tx_rt_reg_rdaddr	),
	.tx_rt_reg_rddata					(tx_rt_reg_rddata	),
	.tx_rt_reg_num						(tx_rt_reg_num	),
	.rx_foc_delta_rden					(rx_foc_delta_rden	),
	.rx_foc_delta_rdaddr				(rx_foc_delta_rdaddr),
	.rx_foc_delta_rddata				(rx_foc_delta_rddata),
	.scan_enable						(scan_enable	),
	.rtc_rfdot							(rtc_rfdot),
	.rtc_period							(rtc_period),
	.rt_length							(rt_length),
	.tx_start							(tx_start),
	.tx_length							(tx_length),
	.rx_start							(rx_start),
	.rx_length							(rx_length),
	.bf_start							(bf_start),
	.bf_length							(bf_length),
	.rtc_down							(rtc_down),
	.gain_shift							(gain_shift),
	.rtc_enable							(rtc_enable),
	.spi_start							(spi_start	),
	.tx_cfg_reset						(tx_cfg_reset	),
	.tx_spi_rdreq						(tx_spi_rdreq		),
	.tx_spi_rdaddr						(tx_spi_rdaddr		),
	.tx_spi_rxvalid						(tx_spi_rxvalid		),
	.tx_spi_rxdata						(tx_spi_rxdata		),
	.tx_cfg_rddata_debug				(tx_cfg_rddata_debug)
);

//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
tx_top									tx_top_i
(
	.clk								(clk_200				),
	.reset								(reset					),
	.scan_enable						(beamform_active		),
	.tx_cfg_req						(tx_cfg_req				),
	.rtc_line							(rtc_line				),
	.rtc_polarity						(rtc_shot[0]			),
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
	
	.tx_spi_rdreq						(tx_spi_rdreq			),
	.tx_spi_rdaddr						(tx_spi_rdaddr			),
	.tx_spi_rxdata						(tx_spi_rxdata			),
	.tx_spi_rxvalid						(tx_spi_rxvalid			),
	.tx_cfg_rddata_debug				(tx_cfg_rddata_debug	),
	
	.tx_cfg_reset						(tx_cfg_reset),
	.tx_dsel0							(tx_dsel0			),
	.tx_dsel1							(tx_dsel1			),
	.tx_reset							(tx_reset			),
	.tr_en								(tr_en				),
	.tx_stdby							(tx_stdby			),
	.tx_cw_en							(tx_cw_en			),
	.ctrl_spi_rdy						(ctrl_spi_rdy		),

	.tx_sclk							(tx_sclk		),
	.tx_sen0							(tx_sen0		),
	.tx_sen1							(tx_sen1		),
	.tx_dat0							(tx_dat0		),
	.tx_dat1							(tx_dat1		),
	.tx_sdout							(tx_sdout		)
);
//--------------------------------------------------------------------------------------------------
scan_sync								scan_sync_i
(
	.clk								(clk_200),
	.reset								(reset),

	.rtc_period							(rtc_period),
	.rt_length							(rt_length),
	.tx_start							(tx_start),
	.tx_length							(tx_length),
	.rx_start							(rx_start),
	.rx_length							(rx_length),
	.bf_start							(bf_start),
	.bf_length							(bf_length),
	.tx_cfg_ready						(tx_cfg_ready),
	.tx_cfg_done						(tx_cfg_done),
	.tx_cfg_req						(tx_cfg_req),
	.rtc_enable							(scan_enable),
	.rtc_trig							(rtc_trig),
	.rtc_txen							(rtc_txen),
	.rtc_rxen							(rtc_rxen),
	.rtc_bfen							(rtc_bfen),
	.rtc_tgc_slope						(rtc_tgc_slope),
	.rtc_tgc_updown						(rtc_tgc_updown),
	.rtc_line							(rtc_line),
	.rtc_shot							(rtc_shot),
	.beamform_active					(beamform_active)
);

ila_timing								ila_timing_i
(
	.clk								(clk_200),
	.probe0								(rtc_trig),
	.probe1								({rtc_rxen,rtc_bfen}),
	.probe2								(rx_length),
	.probe3								(bf_length),
	.probe4								(rtc_line)
);
//--------------------------------------------------------------------------------------------------
wire									rf_dataen;
wire			[20:0]					rf_data;
bf_top									bf_top_i
(
	.clk								(clk_25				),
	.reset								(reset				),
	.rtc_trig							(rtc_trig			),
	.rtc_rxen							(rtc_rxen			),
	.rtc_bfen							(rtc_bfen			),
	.rtc_line							(rtc_line			),
	.bf_apod_data_rden					(rx_apod_data_rden	),
	.bf_apod_data_rdaddr				(rx_apod_data_rdaddr),
	.bf_apod_data_rddata				(rx_apod_data_rddata),
	.bf_foc_elexy_rden					(rx_foc_elexy_rden	),
	.bf_foc_elexy_rdaddr				(rx_foc_elexy_rdaddr),
	.bf_foc_elexy_rddata				(rx_foc_elexy_rddata),
	.bf_foc_delta_rden					(rx_foc_delta_rden	),
	.bf_foc_delta_rdaddr				(rx_foc_delta_rdaddr),
	.bf_foc_delta_rddata				(rx_foc_delta_rddata),
	.clk_ad								(clk_25				),
	.ad_en_in							(ad_en_in			),
	.ad_ch_in							(ad_ch_in			),
	.rf_dataen							(rf_dataen			),
	.rf_data							(rf_data			)
);
//--------------------------------------------------------------------------------------------------
wire drain_shot_done, drain_line_start, capture_drain_fault;
// Hold the PI FIFO enabled until accepted RF/log work drains. capture_busy
// is registered in clk_25: no combinational feedback and no new CDC.
wire processing_enable = beamform_active_sync_ff2 | capture_busy;
bproc_top								bproc_top_i
(
    .drain_shot_done(drain_shot_done), .drain_line_start(drain_line_start),
	.clk_25								(clk_25				),
	.reset								(reset				),
	.rtc_trig							(rtc_trig			),
	.rtc_rxen							(rtc_rxen			),
	.rtc_shot							(rtc_shot			),
	.demod_sin_rden						(demod_sin_rden		),
	.demod_sin_rdaddr					(demod_sin_rdaddr	),
	.demod_sin_rddata					(demod_sin_rddata	),
	.demod_cos_rden						(demod_cos_rden		),
	.demod_cos_rdaddr					(demod_cos_rdaddr	),
	.demod_cos_rddata					(demod_cos_rddata	),
	.log1_coef_rden						(log1_coef_rden		),
	.log1_coef_rdaddr					(log1_coef_rdaddr	),
	.log1_coef_rddata					(log1_coef_rddata	),
	.log2_coef_rden						(log2_coef_rden		),
	.log2_coef_rdaddr					(log2_coef_rdaddr	),
	.log2_coef_rddata					(log2_coef_rddata	),
	.filter_coef_rden					(filter_coef_rden	),
	.filter_coef_rdaddr					(filter_coef_rdaddr	),
	.filter_coef_rddata					(filter_coef_rddata	),
	.scan_enable						(processing_enable),
	.rtc_enable							(processing_enable),
	.rtc_rfdot							(rtc_rfdot			),
	.rtc_period							(rtc_period			),
	.rt_length							(rt_length			),
	.tx_start							(tx_start			),
	.tx_length							(tx_length			),
	.rx_start							(rx_start			),
	.rx_length							(rx_length			),
	.bf_start							(bf_start			),
	.bf_length							(bf_length			),
	.rtc_down							(rtc_down			),
	.gain_shift							(gain_shift			),
	.rf_dataen							(rf_dataen			),
	.rf_data							(rf_data			),
	.log_headen							(log_headen			),
	.log_head							(log_head			),
	.log_start							(log_start			),
	.log_last							(log_last			),
	.log_dataen							(log_dataen			),
	.log_data							(log_data			)
);

// Held window CDC: only bf_sync1 receives an asynchronous input. New work is
// counted on edges, never re-armed just because the last BF window stays high.
(* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg bf_sync1, bf_sync2;
always_ff @(posedge clk_25) begin
    if(reset) begin bf_sync1<=0;bf_sync2<=0;end
    else begin bf_sync1<=rtc_bfen;bf_sync2<=bf_sync1;end
end
capture_drain_tracker capture_drain_i (
    .clk_25(clk_25), .reset(reset),
    .scan_active(beamform_active_sync_ff2), .bf_window(bf_sync2),
    .shot_done(drain_shot_done), .line_start(drain_line_start),
    .line_done(log_last), .output_valid(up_dataen | up_last),
    .busy(capture_busy), .fault(capture_drain_fault)
);
always@(posedge clk_25)
begin
    up_header <= !reset && log_headen;
    up_last <= !reset && log_last;
	if(log_headen)			up_dataen <= 1'b1;
	else if(log_dataen)		up_dataen <= 1'b1;
	else					up_dataen <= 1'b0;

	if(log_headen)			up_data <= log_head;
	else if(log_dataen)		up_data <= log_data;
	else					up_data <= 16'd0;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

endmodule
