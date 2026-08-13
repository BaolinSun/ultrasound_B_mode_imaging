//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
`include "fpga_define.vh"
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
module bf_top
(
	input										clk,
	input										reset,
	input										rtc_trig,				//	扫描时序状态
	input										rtc_rxen,				//	扫描时序状态
	input										rtc_bfen,				//	扫描时序状态
	input			[6:0]						rtc_line,

	output										bf_apod_data_rden,		//	静态参数
	output			[5:0]						bf_apod_data_rdaddr,	//	静态参数
	input			[7:0]						bf_apod_data_rddata,	//	静态参数
	output										bf_foc_elexy_rden,		//	静态参数
	output			[9:0]						bf_foc_elexy_rdaddr,	//	静态参数
	input			[15:0]						bf_foc_elexy_rddata,	//	静态参数

	output										bf_foc_delta_rden,		//	周期参数
	output			[5:0]						bf_foc_delta_rdaddr,	//	周期参数
	input			[19:0]						bf_foc_delta_rddata,	//	周期参数

	input										clk_ad,
	input										ad_en_in,
	input			[31:0][15:0]				ad_ch_in,
	output										rf_dataen,
	output			[20:0]						rf_data
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												rtc_calc;
reg												rtc_trig_t0;
reg												rtc_trig_t1;
reg												rtc_trig_neg;
reg												rtc_trig_dly;
reg												rtc_trig_384;
reg												rtc_trig_385;
reg					[9:0]						rtc_trig_cnt;
reg					[511:0]						rtc_bfen_dly;
reg					[510:0]						rtc_calc_dly;
wire				[510:0]						bf_rtc_bfen;
wire				[510:0]						bf_rtc_calc;
wire											bf_rtc_bfst;
reg												bf_rtc_bfld;
wire                                        bf_rtc_bfpr;

always@(posedge clk)
begin
	rtc_calc <= (rtc_bfen | rtc_bfen_dly[255]);
	rtc_trig_t0 <= rtc_trig;
	rtc_trig_t1 <= rtc_trig_t0;
	rtc_trig_neg <= (!rtc_trig_t0 & rtc_trig_t1);
	rtc_bfen_dly <= {rtc_bfen_dly, rtc_bfen};
	rtc_calc_dly <= {rtc_calc_dly, rtc_calc};
	rtc_trig_dly <= (rtc_trig_cnt[8:6] == 3'b010);
	rtc_trig_384 <= (rtc_trig_cnt == 284);
	rtc_trig_385 <= (rtc_trig_cnt == 285);
	
	if(rtc_trig_neg)			rtc_trig_cnt <= 600;
	else if(rtc_trig_cnt > 0)	rtc_trig_cnt <= rtc_trig_cnt - 1'b1;
end

assign				bf_rtc_bfen					=	rtc_bfen_dly[511:1];
assign				bf_rtc_calc					=	rtc_calc_dly[510:0];
assign				bf_rtc_bfst					=	rtc_trig_dly;
assign				bf_rtc_bfld					=	rtc_trig_384;
assign				bf_rtc_bfpr					=	rtc_trig_385;
//--------------------------------------------------------------------------------------------------
//					ad_process
//--------------------------------------------------------------------------------------------------
wire											ad_en_pre;
reg												ad_en;
reg				[31:0][15:0]					ad_ch;
reg												ad_en_t0;
reg				[31:0][15:0]					ad_ch_t0;

always@(posedge clk_ad)
begin
	if(ad_en_in) begin
		ad_ch <= ad_ch_in;
		ad_en <= 1'b1;
	end
	else begin
		ad_ch <= 32'd0;
		ad_en <= 1'b0;
	end
	ad_en_t0 <= ad_en;
	ad_ch_t0 <= ad_ch;
end

//--------------------------------------------------------------------------------------------------
//					bf_delay
//--------------------------------------------------------------------------------------------------
wire											dly_en_pre;
wire											dly_dataen;
wire [31:0][12:0]								dly_data;
reg												bf_rtc_num;

bf_delay							bf_delay_i
(
	.clk							(clk				),
	.reset							(reset				),
	.rtc_line						(rtc_line			),
	.bf_rtc_bfld					(bf_rtc_bfld		),
	.bf_rtc_bfen					(bf_rtc_bfen		),
	.bf_rtc_calc					(bf_rtc_calc		),
	.bf_rtc_bfst					(bf_rtc_bfst		),
	.bf_rtc_num						(bf_rtc_num			),
	.bf_foc_elexy_rden				(bf_foc_elexy_rden	),
	.bf_foc_elexy_rdaddr			(bf_foc_elexy_rdaddr),
	.bf_foc_elexy_rddata			(bf_foc_elexy_rddata),
	.bf_foc_delta_rden				(bf_foc_delta_rden	),
	.bf_foc_delta_rdaddr			(bf_foc_delta_rdaddr),
	.bf_foc_delta_rddata			(bf_foc_delta_rddata),
	.dly_en_pre						(dly_en_pre			),
	.dly_dataen						(dly_dataen			),
	.dly_data						(dly_data			)
);
//--------------------------------------------------------------------------------------------------
//					bf_focusing
//--------------------------------------------------------------------------------------------------
wire								foc_dataen;
wire		[31:0][15:0]			foc_data;

bf_focusing							bf_focusing_i
(
	.clk							(clk),
	.ad_en_pre						(ad_en_in),
	.ad_en_it						(ad_en_t0),
	.ad_ch_it						(ad_ch_t0),

	.bf_rtc_bfst					(rtc_trig_neg),
	.dly_en_pre						(dly_en_pre),
	.dly_dataen						(dly_dataen),
	.dly_data						(dly_data),
	.foc_dataen						(foc_dataen),
	.foc_data						(foc_data)
);
//--------------------------------------------------------------------------------------------------
//					bf_apod
//--------------------------------------------------------------------------------------------------
wire											apod_dataen;
wire				[31:0][7:0]					apod_data;

bf_apod											bf_apod_i
(
	.clk										(clk),
	.reset										(reset),
	.bf_rtc_bfst								(bf_rtc_bfst),
	.bf_rtc_bfen								(bf_rtc_bfen),
	.rtc_line									(rtc_line),
	
	.bf_apod_data_rden							(bf_apod_data_rden),
	.bf_apod_data_rdaddr						(bf_apod_data_rdaddr),
	.bf_apod_data_rddata						(bf_apod_data_rddata),

	.apod_dataen								(apod_dataen),
	.apod_data									(apod_data)
);
//--------------------------------------------------------------------------------------------------
//					bf_madd
//--------------------------------------------------------------------------------------------------
bf_madd											bf_madd_i
(
	.clk										(clk),
	.foc_dataen									(foc_dataen),
	.foc_data									(foc_data),
	.apod_data									(apod_data),
	.madd_dataen								(rf_dataen),
	.madd_data									(rf_data)
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
