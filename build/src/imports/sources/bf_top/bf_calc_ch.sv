module bf_calc_ch

(
	input										clk,
	input			[15:0]						calc_exi,				//	15+1s<<2
	input			[19:0]						calc_dtx,				//	19+1s<<17
	input			[15:0]						calc_ftx,				//	18+1s<<2
	input			[15:0]						calc_fni,				//	16+0s<<0
	input			[31:0]						calc_ffn,				//	32+0s<<0
	output			[12:0]						dly_calc				//	20+0s<<3
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
(* DONT_TOUCH = "TRUE" *)	reg	signed	[15:0]	data_xni;				//	15+1s<<2
(* DONT_TOUCH = "TRUE" *)	reg	signed	[19:0]	data_dtx;				//	19+1s<<17
(* DONT_TOUCH = "TRUE" *)	reg			[15:0]	data_ftx;				//	16+1s<<0
(* DONT_TOUCH = "TRUE" *)	reg			[15:0]	data_fni;				//	16+1s<<0
(* DONT_TOUCH = "TRUE" *)	reg			[31:0]	data_ffn;				//	36+1s<<4
(* DONT_TOUCH = "TRUE" *)	reg	signed	[32:0]	data_xff_t0;			//	38+1s<<4
(* DONT_TOUCH = "TRUE" *)	reg	signed	[32:0]	data_xff_t1;			//	38+1s<<4
(* DONT_TOUCH = "TRUE" *)	reg	signed	[32:0]	data_xff_t2;			//	38+1s<<4
(* DONT_TOUCH = "TRUE" *)	reg	signed	[32:0]	data_xff_t3;			//	38+1s<<4

always@(posedge clk)
begin
	data_xni <= calc_exi << 1;
	data_dtx	<=	calc_dtx;
	data_ftx	<=	calc_ftx;									//	16+0s<<0	+1s
	data_fni	<=	calc_fni<<1;								//	16+0s<<0	+1s
	data_ffn	<=	calc_ffn;								//	32+0s<<0	+1s	<<4
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire	signed		[32:0]						data_xff;				//	37+1s<<4
wire	signed		[35:0]						data_xsin;				//	36+1s<<19
wire	signed		[51:0]						data_fsc;				//	53+1s<<19

dsp_16x16_32									dsp_yff					//	pipeline=4	DSP=1
(
	.CLK										(clk),
	.A											(data_xni),				//	15+1s<<2
	.B											(data_xni),				//	15+1s<<2
	.C											(data_ffn),				//	36+1s<<4
	.P											(data_xff)				//	37+1s<<4
);

dsp_20x16										dsp_xsin				//	pipeline=5	DSP=1
(
	.CLK										(clk),
	.A											(data_dtx),				//	19+1s<<17
	.B											(data_xni),				//	15+1s<<2
	.P											(data_xsin)				//	36+1s<<19
);

mult_36x16										mult_fsc				//	pipeline=4	DSP=2
(
	.CLK										(clk),
	.A											(data_xsin),			//	36+1s<<19
	.B											(data_fni),				//	16+1s<<0
	.P											(data_fsc)				//	53+1s<<19
);
//------------------------------------------------------------------------------------------
reg		signed		[47:0]						data_pre1;				//	42+6s<<10 ( max 65535*65535<<10)  42+0s<<10
reg		signed		[47:0]						data_pre2;				//	42+6s<<10 ( max 65535*65535<<10)  42+0s<<10
reg		signed		[43:0]						data_tmp1;
reg		signed		[43:0]						data_tmp2;


always@(posedge clk)
begin
	data_xff_t0  <= data_xff;
	data_xff_t1  <= data_xff_t0;
	data_xff_t2  <= data_xff_t1;
	data_xff_t3  <= data_xff_t2;

	data_tmp1 <= data_fsc[51:8] + data_fsc[7];
	data_tmp2 <= {5'b0,data_xff,10'b0};

	// data_pre <= {data_fsc[51:9]} + {data_xff_t3, 9'd0, data_fsc[8]};	//	(53+1s<<19	>>9)  (38+1s<<4 <<6)
	data_pre1 <= data_tmp2 - data_tmp1;	//	(53+1s<<19	>>9)  (38+1s<<4 <<6)
	data_pre2 <= data_pre1>>1;	//	(53+1s<<19	>>9)  (38+1s<<4 <<6)
end
//--------------------------------------------------------------------------------------------------
wire				[23:0]						data_sqrt;				//	21+3s<<5

sqrt_44											sqrt_xy					//	pipeline=23	RoundPosInf
(
	.aclk										(clk),
	.s_axis_cartesian_tvalid					(1'b1),
	.s_axis_cartesian_tdata						(data_pre2),				//	42+6s<<10
	.m_axis_dout_tdata							(data_sqrt),			//	21+3s<<5
	.m_axis_dout_tvalid							()
);
//--------------------------------------------------------------------------------------------------
reg		signed		[20:0]						data_dly1;				//	20+1s<<3
reg		signed		[20:0]						data_dly2;				//	20+1s<<3
reg		signed		[20:0]						data_dly3;				//	20+1s<<3

always@(posedge clk)
begin
	data_dly1 <= data_sqrt[23:4] + data_sqrt[3];			//	(21+3s<<5	>>2)	(18+1s<<2	<<1)
	data_dly2 <= data_dly1 + data_ftx;			//	(21+3s<<5	>>2)	(18+1s<<2	<<1)
	data_dly3 <= data_dly2[20:1] + data_dly2[0];			//	(21+3s<<5	>>2)	(18+1s<<2	<<1)
end
//--------------------------------------------------------------------------------------------------
assign				dly_calc					=	data_dly3[20] ? 13'd0 :
													(data_dly3[19:0] > 20'd8191) ? 13'd8191 :
													data_dly3[12:0];			//	keep 13-bit sample address for 6144 depth
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
