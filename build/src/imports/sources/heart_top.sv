module heart_top(
	input							sys_clk_n,
	input							sys_clk_p,
	
	output							AFE5832_SPI_CLK,
	output							AFE5832_SPI_CS,
	input							AFE5832_SPI_MISO,
	output							AFE5832_SPI_MOSI,
	output							afe5832_tgc_slope_0,
	output							afe5832_up_down_0,
	input							dcm_0,
	input							dcp_0,
	input	[15:0]					dm_0,
	input	[15:0]					dp_0,
	input							fcm_0,
	input							fcp_0,
	
	inout	[14:0]					DDR_addr,
	inout	[2:0]					DDR_ba,
	inout							DDR_cas_n,
	inout							DDR_ck_n,
	inout							DDR_ck_p,
	inout							DDR_cke,
	inout							DDR_cs_n,
	inout	[3:0]					DDR_dm,
	inout	[31:0]					DDR_dq,
	inout	[3:0]					DDR_dqs_n,
	inout	[3:0]					DDR_dqs_p,
	inout							DDR_odt,
	inout							DDR_ras_n,
	inout							DDR_reset_n,
	inout							DDR_we_n,
	inout							FIXED_IO_ddr_vrn,
	inout							FIXED_IO_ddr_vrp,
	inout	[53:0]					FIXED_IO_mio,
	inout							FIXED_IO_ps_clk,
	inout							FIXED_IO_ps_porb,
	inout							FIXED_IO_ps_srstb,
	inout	[18:0]					GPIO_0_0_tri_io,

	output							tx_dsel0,				//-	tx管脚，dsel0
	output							tx_dsel1,				//-	tx管脚，dsel1
	output							tx_reset,				//	发射控制管脚
	output		[7:0]				tr_en,
	output							tx_stdby,
	output							tx_cw_en,
	output		[1:0]				tx_lvds_sync_p,			//	发射同步触发管脚
	output		[1:0]				tx_lvds_sync_n,			//	发射同步触发管脚
	output		[1:0]				tx_sclk,				//	发射配置通讯管脚
	output		[1:0]				tx_sen0,				//	发射配置通讯管脚
	output		[1:0]				tx_sen1,				//	发射配置通讯管脚
	output		[1:0]				tx_dat0,				//	发射配置通讯管脚
	output		[1:0]				tx_dat1,				//	发射配置通讯管脚
	input		[1:0]				tx_sdout,				//	发射配置通讯管脚
	
	output							test_led
);
//--------------------------------------------------------------------------------------------------
localparam	[23:0]				PARA_TX_CFG_RDDATA_ADDR = 24'h4C0114;
//--------------------------------------------------------------------------------------------------
wire							clk_100m;
wire							clk_200m;
wire							rst_n;
//--------------------------------------------------------------------------------------------------
wire	[23:0]					para_addr;
wire							para_clk;
wire	[31:0]					para_din;
reg		[31:0]					para_dout;
wire							para_en;
wire							para_rst;
wire	[3:0]					para_we;
wire	[31:0]					tx_cfg_rddata_debug;
wire	[31:0]					para_rddata_unused;
wire							para_rdvalid_unused;

wire	[511:0]					data_ch;
wire							data_clk;

wire up_header, up_last, capture_busy;
wire [31:0] frame_ctrl_rdata;
reg								proc_en;
reg			[511:0]				proc_in;
wire							up_dataen;
wire		[15:0]				up_data;
wire		[31:0]				rtc_rfdot;

wire							rtc_txen;
wire							rtc_rxen;
wire		[6:0]				rtc_line;
wire		[15:0]				data_length;
wire							test_sel;
//--------------------------------------------------------------------------------------------------
clk_gen							clk_gen_i
(
	.clk_in1_p					(sys_clk_p),
	.clk_in1_n					(sys_clk_n),
	.clk_out1					(clk_100m),
	.clk_out2					(clk_200m),
	.locked						(rst_n)
);
//--------------------------------------------------------------------------------------------------
pus_ctrl_system_wrapper	pus_ctrl_system_wrapper_i
(
    .frame_header(up_header), .frame_last(up_last), .frame_busy(capture_busy),
    .frame_ctrl_rdata(frame_ctrl_rdata),
	.clk_100m				(clk_100m	),
	.pll_locked				(rst_n		),
    .clk_200m           (clk_200m),
	.AFE5832_SPI_CLK		(AFE5832_SPI_CLK	),
	.AFE5832_SPI_CS			(AFE5832_SPI_CS		),
	.AFE5832_SPI_MISO		(AFE5832_SPI_MISO	),
	.AFE5832_SPI_MOSI		(AFE5832_SPI_MOSI	),
	.dcm_0					(dcm_0				),
	.dcp_0					(dcp_0				),
	.dm_0					(dm_0				),
	.dp_0					(dp_0				),
	.fcm_0					(fcm_0				),
	.fcp_0					(fcp_0				),
	.afe5832_tgc_slope_0	(			),
	.afe5832_up_down_0		(			),
	.data_ch				(data_ch	),
	.data_clk				(data_clk	),

	.up_data_valid			(up_dataen),
	.up_data				(up_data),
	.data_length			(16'h0400),
	
	.DDR_addr				(DDR_addr			),
	.DDR_ba					(DDR_ba				),
	.DDR_cas_n				(DDR_cas_n			),
	.DDR_ck_n				(DDR_ck_n			),
	.DDR_ck_p				(DDR_ck_p			),
	.DDR_cke				(DDR_cke			),
	.DDR_cs_n				(DDR_cs_n			),
	.DDR_dm					(DDR_dm				),
	.DDR_dq					(DDR_dq				),
	.DDR_dqs_n				(DDR_dqs_n			),
	.DDR_dqs_p				(DDR_dqs_p			),
	.DDR_odt				(DDR_odt			),
	.DDR_ras_n				(DDR_ras_n			),
	.DDR_reset_n			(DDR_reset_n		),
	.DDR_we_n				(DDR_we_n			),
	.FIXED_IO_ddr_vrn		(FIXED_IO_ddr_vrn	),
	.FIXED_IO_ddr_vrp		(FIXED_IO_ddr_vrp	),
	.FIXED_IO_mio			(FIXED_IO_mio		),
	.FIXED_IO_ps_clk		(FIXED_IO_ps_clk	),
	.FIXED_IO_ps_porb		(FIXED_IO_ps_porb	),
	.FIXED_IO_ps_srstb		(FIXED_IO_ps_srstb	),
	.GPIO_0_0_tri_io		(GPIO_0_0_tri_io	),

	.para_addr				(para_addr	),
	.para_clk				(para_clk	),
	.para_din				(para_din	),
	.para_dout				(para_dout	),
	.para_en				(para_en	),
	.para_rst				(para_rst	),
	.para_we				(para_we	)
);

// The AXI BRAM controller is configured for one cycle of read latency. The
// generic para_top read path is multi-cycle and was never connected here, so
// return the retained TX7332 readback through a dedicated registered address
// decode. tx_cfg_rddata_debug is already synchronous to para_clk.
always @(posedge para_clk)
begin
	if (para_rst)
		para_dout <= 32'd0;
	else if (para_en && (para_we == 4'b0000))
	begin
		if (para_addr == PARA_TX_CFG_RDDATA_ADDR)
			para_dout <= tx_cfg_rddata_debug;
		else if (para_addr[23:8] == 16'h4c02)
            para_dout <= frame_ctrl_rdata;
		else
			para_dout <= 32'd0;
	end
end

always@(posedge data_clk)
begin
	proc_en <= rtc_rxen;
	proc_in[15:0]    <= data_ch[15:0]   ;
	proc_in[31:16]   <= data_ch[31:16]  ;
	proc_in[47:32]   <= data_ch[47:32]  ;
	proc_in[63:48]   <= data_ch[63:48]  ;
	proc_in[79:64]   <= data_ch[79:64]  ;
	proc_in[95:80]   <= data_ch[95:80]  ;
	proc_in[111:96]  <= data_ch[111:96] ;
	proc_in[127:112] <= data_ch[127:112];
	proc_in[143:128] <= data_ch[143:128];
	proc_in[159:144] <= data_ch[159:144];
	proc_in[175:160] <= data_ch[175:160];
	proc_in[191:176] <= data_ch[191:176];
	proc_in[207:192] <= data_ch[207:192];
	proc_in[223:208] <= data_ch[223:208];
	proc_in[239:224] <= data_ch[239:224];
	proc_in[255:240] <= data_ch[255:240];
	proc_in[271:256] <= data_ch[271:256];
	proc_in[287:272] <= data_ch[287:272];
	proc_in[303:288] <= data_ch[303:288];
	proc_in[319:304] <= data_ch[319:304];
	proc_in[335:320] <= data_ch[335:320];
	proc_in[351:336] <= data_ch[351:336];
	proc_in[367:352] <= data_ch[367:352];
	proc_in[383:368] <= data_ch[383:368];
	proc_in[399:384] <= data_ch[399:384];
	proc_in[415:400] <= data_ch[415:400];
	proc_in[431:416] <= data_ch[431:416];
	proc_in[447:432] <= data_ch[447:432];
	proc_in[463:448] <= data_ch[463:448];
	proc_in[479:464] <= data_ch[479:464];
	proc_in[495:480] <= data_ch[495:480];
	proc_in[511:496] <= data_ch[511:496];
end

ila_bf						ila_bf_i
(
	.clk					(data_clk),
	.probe0					({proc_en,proc_in[15:0]}),
	.probe1					({rtc_rxen,proc_top_i.rtc_bfen}),
	.probe2					({up_dataen,up_data}),
	.probe3					(proc_top_i.rx_foc_delta_rddata),
	.probe4					(rtc_line)
);
//--------------------------------------------------------------------------------------------------
proc_top					proc_top_i
(
    .up_header(up_header), .up_last(up_last), .capture_busy(capture_busy),
	.clk_para				(para_clk		),
	.clk_200				(clk_200m		),
	.clk_25					(data_clk		),
	.reset					(!rst_n			),
	.para_wren				(para_we[0]		),
	.para_rden				(1'b0			),
	.para_addr				(para_addr		),
	.para_wrdata			(para_din		),
	.para_rddata			(para_rddata_unused),
	.para_rdvalid			(para_rdvalid_unused),
	.tx_cfg_rddata_debug	(tx_cfg_rddata_debug),
	.tx_dsel0				(tx_dsel0		),
	.tx_dsel1				(tx_dsel1		),
	.tx_reset				(tx_reset		),
	.tr_en					(tr_en			),
	.tx_stdby				(tx_stdby		),
	.tx_cw_en				(tx_cw_en		),
	.tx_sclk				(tx_sclk		),
	.tx_sen0				(tx_sen0		),
	.tx_sen1				(tx_sen1		),
	.tx_dat0				(tx_dat0		),
	.tx_dat1				(tx_dat1		),
	.tx_sdout				(tx_sdout		),
	.data_en				(proc_en		),
	.data_ch				(proc_in		),
	.up_dataen				(up_dataen		),
	.up_data				(up_data		),
	.rtc_rfdot				(rtc_rfdot		),
	.rtc_txen				(rtc_txen		),
	.rtc_rxen				(rtc_rxen		),
	.rtc_tgc_slope			(afe5832_tgc_slope_0),
	.rtc_tgc_updown			(afe5832_up_down_0	),
	.rtc_line				(rtc_line		)
);
//--------------------------------------------------------------------------------------------------
OBUFDS								OBUFDS_sync0_inst
(
	.O								(tx_lvds_sync_p[0]),
	.OB								(tx_lvds_sync_n[0]),
	.I								(rtc_txen)
);
OBUFDS								OBUFDS_sync1_inst
(
	.O								(tx_lvds_sync_p[1]),
	.OB								(tx_lvds_sync_n[1]),
	.I								(rtc_txen)
);
//--------------------------------------------------------------------------------------------------
reg				[24:0]							cnt_work;

always@(posedge clk_100m)
begin
	if(!rst_n)
		cnt_work <= 0;
	else
		cnt_work <= cnt_work + 1'b1;
end

assign	test_led				=	cnt_work[24];
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

endmodule
