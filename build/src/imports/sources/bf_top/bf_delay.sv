module bf_delay
(
	input										clk,
	input										reset,
	input			[6:0]						rtc_line,
	input										bf_rtc_bfld,
	input			[510:0]						bf_rtc_bfen,			//	扫描时序状态
	input			[510:0]						bf_rtc_calc,			//	扫描时序状态
	input										bf_rtc_bfst,
	input										bf_rtc_num,

	output	reg									bf_foc_elexy_rden,		//	静态参数
	output	reg		[9:0]						bf_foc_elexy_rdaddr,	//	静态参数
	input			[15:0]						bf_foc_elexy_rddata,	//	静态参数
	output	reg									bf_foc_delta_rden,		//	周期参数
	output	reg		[5:0]						bf_foc_delta_rdaddr,		//	周期参数
	input			[19:0]						bf_foc_delta_rddata,	//	周期参数

	output										dly_en_pre,
	output										dly_dataen,
	output			[31:0][12:0]				dly_data
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												bf_foc_elexy_rden_t0;
reg												flag_elexy_rdrdy;
reg					[6:0]						flag_elexy_rdnum;
reg					[31:0]						flag_elexy_x;
reg		signed		[15:0]						para_elexy_rd;
reg		signed		[15:0]						para_elexy;
reg					[5:0]						bf_foc_elech_rdcnt;

always@(posedge clk)
begin
	if(!bf_rtc_bfst)							bf_foc_elech_rdcnt <= 0;
	else if(bf_foc_elech_rdcnt == 32)			bf_foc_elech_rdcnt <= bf_foc_elech_rdcnt;
	else										bf_foc_elech_rdcnt <= bf_foc_elech_rdcnt + 1'b1;
	
	bf_foc_elexy_rden <= (bf_rtc_bfst & (bf_foc_elech_rdcnt < 32));
	bf_foc_elexy_rdaddr <= {rtc_line[0], bf_foc_elech_rdcnt[4:0]};

	bf_foc_elexy_rden_t0 <= bf_foc_elexy_rden;
	flag_elexy_rdrdy <= bf_foc_elexy_rden_t0;
	flag_elexy_rdnum <= flag_elexy_rdrdy ? (flag_elexy_rdnum + 1'b1) : 0;
	
	para_elexy_rd <= bf_foc_elexy_rddata;
	para_elexy <= para_elexy_rd;
end
//--------------------------------------------------------------------------------------------------
reg		signed		[19:0]						para_delta;

always@(posedge clk)
begin
	bf_foc_delta_rden <= (!bf_rtc_calc[5]) & bf_rtc_calc[4];
	bf_foc_delta_rdaddr <= rtc_line[6:1];
	para_delta <= bf_foc_delta_rddata;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg					[12:0]						para_fni;
reg					[12:0]						para_fnt;
reg					[12:0]						para_fnx;
wire				[25:0]						para_ffn;
reg					[25:0]						para_ffn_pipe0;
reg					[25:0]						para_ffn_pipe1;
reg					[25:0]						para_ffn_pipe2;
reg					[13:0]						coef_fni;
reg					[13:0]						coef_fnt;
reg					[27:0]						coef_ffn;

always@(posedge clk)
begin
	para_fni <= (bf_rtc_calc[48]) ? para_fni + 1'b1 : 0;
	para_fnt <= (bf_rtc_calc[79]) ? para_fnt + 1'b1 : 0;
	para_fnx <= (bf_rtc_calc[45]) ? para_fnx + 1'b1 : 0;

    coef_fni <= para_fni;
    coef_fnt <= para_fnt;
	coef_ffn <= para_ffn;
end
//--------------------------------------------------------------------------------------------------
always@(posedge clk)
begin
	para_ffn_pipe0 <= para_fnx * para_fnx;
	para_ffn_pipe1 <= para_ffn_pipe0;
	para_ffn_pipe2 <= para_ffn_pipe1;
end

assign				para_ffn					=	para_ffn_pipe2;
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg		signed		[31:0][15:0]			calc_exi;

genvar num_ch;
generate
	for(num_ch=0; num_ch < 32; num_ch=num_ch+1)
	begin
		always@(posedge clk)
		begin
			flag_elexy_x[num_ch] <= (flag_elexy_rdrdy & !flag_elexy_rdnum[5] & (flag_elexy_rdnum[4:0] == num_ch));
			if(flag_elexy_x[num_ch])	calc_exi[num_ch] <= para_elexy;
		end
	end
endgenerate
//--------------------------------------------------------------------------------------------------
reg		signed		[19:0]		coef_dtx;
reg		signed		[19:0]		calc_dtx;

reg					[15:0]		calc_fni;
reg					[31:0]		calc_ffn;
reg		signed		[15:0]		calc_ftx;


always@(posedge clk)
begin
	coef_dtx <= para_delta;
end

always@(posedge clk)
begin
	calc_dtx <= coef_dtx;
	calc_fni <= coef_fni;
	calc_ftx <= coef_fnt;
	calc_ffn <= coef_ffn;
end


wire	[31:0][12:0]		dly_calc;

genvar num_c;
generate
	for(num_c=0; num_c < 32; num_c=num_c+1)
	begin:mbp
		bf_calc_ch							bf_calc_ch_i
		(
			.clk							(clk),
			.calc_exi						(calc_exi[num_c]),
			.calc_dtx						(calc_dtx),
			.calc_ftx						(calc_ftx),
			.calc_fni						(calc_fni),
			.calc_ffn						(calc_ffn),
			.dly_calc						(dly_calc[num_c])
		);
	end
endgenerate
//--------------------------------------------------------------------------------------------------
assign			dly_en_pre						=	bf_rtc_bfen[83];
assign			dly_dataen						=	bf_rtc_bfen[84];
assign			dly_data						=	dly_calc;
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
