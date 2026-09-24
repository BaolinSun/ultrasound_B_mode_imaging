module filter_iq
(
	input										clk,
	input										reset,
	input										rtc_rxen,
	// Programmable receive gain, 0..8, each step +6.02 dB. 0 = original behaviour.
	input			[3:0]						gain_shift,
	output	reg									filter_coef_rden,
	output	reg		[12:0]						filter_coef_rdaddr,
	input			[15:0]						filter_coef_rddata,

	input										demod_headen,
	input			[15:0]						demod_head,
	input										demod_start,
	input										demod_last,
	input										demod_dataen,
	input			[24:0]						demod_i,
	input			[24:0]						demod_q,
	
	output										filter_headen,
	output			[15:0]						filter_head,
	output										filter_start,
	output										filter_last,
	output										filter_dataen,
	output			[15:0]						filter_i,
	output			[15:0]						filter_q
);
//--------------------------------------------------------------------------------------------------
// gain_shift is written in para_st on the parameter clock (clk_para, 100 MHz) but
// consumed here on clk_25 (28.6 MHz). Taken raw it is an unsynchronised CDC feeding
// the barrel shifter and the 33-bit carry chain, which the tool times against the
// worst-case 5 ns inter-clock edge alignment instead of the 35 ns clk_25 period.
// Synchronise it first, same as scan_enable -> rtc_enable_sync_ff1 elsewhere in
// this design; pin.xdc false-paths only the first stage.
//
// The third register only adopts a value once two consecutive synchronised samples
// agree, so bit skew during a write cannot momentarily present a mixed code (for
// example 3 -> 4 appearing as 7) to the shifter.
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO", DONT_TOUCH = "TRUE" *) reg [3:0] gain_shift_sync_ff1;
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO", DONT_TOUCH = "TRUE" *) reg [3:0] gain_shift_sync_ff2;
reg [3:0] gain_shift_q;

always@(posedge clk)
begin
	if(reset) begin
		gain_shift_sync_ff1 <= 4'd0;
		gain_shift_sync_ff2 <= 4'd0;
		gain_shift_q <= 4'd0;
	end else begin
		gain_shift_sync_ff1 <= gain_shift;
		gain_shift_sync_ff2 <= gain_shift_sync_ff1;
		if(gain_shift_sync_ff2 == gain_shift_sync_ff1)
			gain_shift_q <= gain_shift_sync_ff2;
	end
end
//--------------------------------------------------------------------------------------------------
reg					[6:0]						filter_coef_rdcnt;
reg					[6:0]						filter_coef_rdcnt_t0;
reg					[6:0]						filter_coef_rdcnt_t1;

always@(posedge clk)
begin
	if(!rtc_rxen)								filter_coef_rdcnt <= 0;
	else if(filter_coef_rdcnt == 64)			filter_coef_rdcnt <= filter_coef_rdcnt;
	else										filter_coef_rdcnt <= filter_coef_rdcnt + 1'b1;

	filter_coef_rdcnt_t0 <= filter_coef_rdcnt;
	filter_coef_rdcnt_t1 <= filter_coef_rdcnt_t0;

	filter_coef_rden <= (rtc_rxen & (filter_coef_rdcnt < 7'd64));
	filter_coef_rdaddr <= filter_coef_rdcnt[5:0];
end

reg	signed		[63:0][11:0]			filter_coef;

always@(posedge clk)
begin
	filter_coef[filter_coef_rdcnt_t1] <= filter_coef_rddata[11:0];
end
//--------------------------------------------------------------------------------------------------
reg  signed		[63:0][24:0]			data_in_i;
reg  signed		[31:0][25:0]			data_add_i;
reg  signed		[31:0][37:0]			add_a_i;
always@(posedge clk)
begin
	data_in_i[0]  <= demod_i;
end

genvar j;
generate
	for(j=0;j<63;j=j+1)
	begin
		always@(posedge clk)
		begin
			data_in_i[j+1]  <= data_in_i[j];
		end
	end
endgenerate

genvar	i;
generate
	for(i=0; i<32 ; i=i+1)
	begin:add_64
		always@(posedge clk)
		begin
			data_add_i[i] <= {data_in_i[i][24],data_in_i[i]} + {data_in_i[63-i][24],data_in_i[63-i]};
		end
	end
endgenerate

genvar	i0;
generate
	for(i0=0; i0<32 ; i0=i0+1)
	begin:mult_inst
		mult_26x12b_d3 mult_a	(
			.CLK					(clk),
			.A						(data_add_i[i0]),
			.B						(filter_coef[i0]),
			.P						(add_a_i[i0])
			);
	end
endgenerate
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg  signed		[31:0][38:0]			add_a0_i;
reg  signed		[15:0][38:0]			add_b_i;
reg  signed		[15:0][39:0]			add_b0_i;
reg  signed		[7:0][39:0]				add_c_i;
reg  signed		[7:0][40:0]				add_c0_i;
reg  signed		[3:0][40:0]				add_d_i;
reg  signed		[3:0][41:0]				add_d0_i;
reg  signed		[1:0][41:0]				add_e_i;
reg  signed		[1:0][42:0]				add_e0_i;
reg  signed			 [42:0]				sum_i;

genvar	g;
generate
	for(g=0; g <32; g=g+1)
		assign add_a0_i[g]={add_a_i[g][37],add_a_i[g]};
endgenerate

genvar	w;
generate
	for(w=0; w <16; w=w+1)
		assign add_b0_i[w]={add_b_i[w][38],add_b_i[w]};
endgenerate

genvar	k;
generate
	for(k=0; k <8; k=k+1)
		assign add_c0_i[k]={add_c_i[k][39],add_c_i[k]};
endgenerate

genvar	m;
generate
	for(m=0; m <4; m=m+1)
		assign add_d0_i[m]={add_d_i[m][40],add_d_i[m]};
endgenerate

genvar	n;
generate
	for(n=0; n <2; n=n+1)
		assign add_e0_i[n]={add_e_i[n][41],add_e_i[n]};
endgenerate

always @(posedge clk)
begin
	add_b_i[0]  <= add_a0_i[1]	+ add_a0_i[0];
	add_b_i[1]  <= add_a0_i[3]	+ add_a0_i[2];
	add_b_i[2]  <= add_a0_i[5]	+ add_a0_i[4];
	add_b_i[3]  <= add_a0_i[7]	+ add_a0_i[6];
	add_b_i[4]  <= add_a0_i[9]	+ add_a0_i[8];
	add_b_i[5]  <= add_a0_i[11] + add_a0_i[10];
	add_b_i[6]  <= add_a0_i[13] + add_a0_i[12];
	add_b_i[7]  <= add_a0_i[15] + add_a0_i[14];
	add_b_i[8]  <= add_a0_i[17] + add_a0_i[16];
	add_b_i[9]  <= add_a0_i[19] + add_a0_i[18];
	add_b_i[10] <= add_a0_i[21] + add_a0_i[20];
	add_b_i[11] <= add_a0_i[23] + add_a0_i[22];
	add_b_i[12] <= add_a0_i[25] + add_a0_i[24];
	add_b_i[13] <= add_a0_i[27] + add_a0_i[26];
	add_b_i[14] <= add_a0_i[29] + add_a0_i[28];
	add_b_i[15] <= add_a0_i[31] + add_a0_i[30];

	add_c_i[0]  <= add_b0_i[1] + add_b0_i[0];
	add_c_i[1]  <= add_b0_i[2] + add_b0_i[3];
	add_c_i[2]  <= add_b0_i[4] + add_b0_i[5];
	add_c_i[3]  <= add_b0_i[6] + add_b0_i[7];
	add_c_i[4]  <= add_b0_i[8] + add_b0_i[9];
	add_c_i[5]  <= add_b0_i[10]+ add_b0_i[11];
	add_c_i[6]  <= add_b0_i[12]+ add_b0_i[13];
	add_c_i[7]  <= add_b0_i[14]+ add_b0_i[15];

	add_d_i[0]  <= add_c0_i[1] + add_c0_i[0];
	add_d_i[1]  <= add_c0_i[2] + add_c0_i[3];
	add_d_i[2]  <= add_c0_i[4] + add_c0_i[5];
	add_d_i[3]  <= add_c0_i[6] + add_c0_i[7];
	
	add_e_i[0]  <= add_d0_i[1] + add_d0_i[0];
	add_e_i[1]  <= add_d0_i[2] + add_d0_i[3];
	
	sum_i 		<= add_e0_i[0] + add_e0_i[1];
end
//--------------------------------------------------------------------------------------------------
// Programmable receive gain, I path.
//
// sum_i is the 43-bit filter accumulator. The original code took a fixed
// sum_i >>> 19 with round-half-up (and a tie-break that skips the increment on
// exact negative halves), then sliced [15:0] with NO saturation.
//
// Two things changed here:
//  1. sum_i_g pre-shifts the accumulator left by (8 + gain_shift) and the fixed
//     right shift becomes 27, so the net shift is 19 - gain_shift. Each gain_shift
//     step is +6.02 dB. gain_shift = 0 is BIT-EXACT with the original >>19: the
//     pre-shift by 8 puts sum_i[42:0] at sum_i_g[50:8], so sum_i_g[58:27]
//     == sum_i[42:19], sum_i_g[26] == sum_i[18], and the tie condition
//     sum_i_g[26:0] == 27'h4000000 reduces to sum_i[18:0] == 19'h40000.
//     59 bits = 43 + 16 so the maximum shift cannot push MSBs out.
//  2. sum_round_i is widened to 33 bits and the [15:0] slice is saturated.
//     The old I path declared sum_round_i as [23:0] while the expression assigned
//     to it was 25 bits, so the I path silently dropped a bit that Q kept.
//
// The two-register pipeline depth is preserved, so the dly_* alignment is unchanged.
wire signed			 [58:0]				sum_i_e = {{16{sum_i[42]}}, sum_i};
wire signed			 [58:0]				sum_i_g = sum_i_e <<< (5'd8 + gain_shift_q);
reg  signed			 [32:0]				sum_round_i;
reg  signed			 [15:0]				data_out_i;

always @(posedge clk)
begin
	if((sum_i_g[58] == 1'b1) & (sum_i_g[26:0] == 27'h4000000))begin
		sum_round_i <= {sum_i_g[58], sum_i_g[58:27]};
	end
	else begin
		sum_round_i <= {sum_i_g[58], sum_i_g[58:27]} + sum_i_g[26];
	end
end

always @(posedge clk)
begin 
	if(sum_round_i > 33'sd32767)			data_out_i <= 16'sh7FFF;
	else if(sum_round_i < -33'sd32768)	data_out_i <= 16'sh8000;
	else									data_out_i <= sum_round_i[15:0];
end
//--------------------------------------------------------------------------------------------------
reg  signed		[63:0][24:0]			data_in_q;
reg  signed		[31:0][25:0]			data_add_q;
reg  signed		[31:0][37:0]			add_a_q;

always@(posedge clk)
begin
	data_in_q[0]  <= demod_q;
end

genvar jj;
generate
	for(jj=0;jj<63;jj=jj+1)
	begin
		always@(posedge clk)
		begin
			data_in_q[jj+1]  <= data_in_q[jj];
		end
	end
endgenerate

genvar	ii;
generate
	for(ii=0; ii<32 ; ii=ii+1)
	begin:add_641
		always@(posedge clk)
		begin
			data_add_q[ii] <= {data_in_q[ii][24],data_in_q[ii]} + {data_in_q[63-ii][24],data_in_q[63-ii]};
		end
	end
endgenerate

genvar	ii0;
generate
	for(ii0=0; ii0<32 ; ii0=ii0+1)
	begin:mult_inst1
		mult_26x12b_d3 mult_a	(
			.CLK					(clk		),
			.A						(data_add_q[ii0]),
			.B						(filter_coef[ii0]),
			.P						(add_a_q[ii0]	)
			);
	end
endgenerate
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg  signed		[31:0][38:0]			add_a0_q;
reg  signed		[15:0][38:0]			add_b_q;
reg  signed		[15:0][39:0]			add_b0_q;
reg  signed		[7:0][39:0]				add_c_q;
reg  signed		[7:0][40:0]				add_c0_q;
reg  signed		[3:0][40:0]				add_d_q;
reg  signed		[3:0][41:0]				add_d0_q;
reg  signed		[1:0][41:0]				add_e_q;
reg  signed		[1:0][42:0]				add_e0_q;
reg  signed			 [42:0]				sum_q;

genvar	gg;
generate
	for(gg=0; gg <32; gg=gg+1)
		assign add_a0_q[gg]={add_a_q[gg][37],add_a_q[gg]};
endgenerate

genvar	ww;
generate
	for(ww=0; ww <16; ww=ww+1)
		assign add_b0_q[ww]={add_b_q[ww][38],add_b_q[ww]};
endgenerate

genvar	kk;
generate
	for(kk=0; kk <8; kk=kk+1)
		assign add_c0_q[kk]={add_c_q[kk][39],add_c_q[kk]};
endgenerate

genvar	mm;
generate
	for(mm=0; mm <4; mm=mm+1)
		assign add_d0_q[mm]={add_d_q[mm][40],add_d_q[mm]};
endgenerate

genvar	nn;
generate
	for(nn=0; nn <2; nn=nn+1)
		assign add_e0_q[nn]={add_e_q[nn][41],add_e_q[nn]};
endgenerate

always @(posedge clk)
begin
	add_b_q[0]  <= add_a0_q[1]	+ add_a0_q[0];
	add_b_q[1]  <= add_a0_q[3]	+ add_a0_q[2];
	add_b_q[2]  <= add_a0_q[5]	+ add_a0_q[4];
	add_b_q[3]  <= add_a0_q[7]	+ add_a0_q[6];
	add_b_q[4]  <= add_a0_q[9]	+ add_a0_q[8];
	add_b_q[5]  <= add_a0_q[11] + add_a0_q[10];
	add_b_q[6]  <= add_a0_q[13] + add_a0_q[12];
	add_b_q[7]  <= add_a0_q[15] + add_a0_q[14];
	add_b_q[8]  <= add_a0_q[17] + add_a0_q[16];
	add_b_q[9]  <= add_a0_q[19] + add_a0_q[18];
	add_b_q[10] <= add_a0_q[21] + add_a0_q[20];
	add_b_q[11] <= add_a0_q[23] + add_a0_q[22];
	add_b_q[12] <= add_a0_q[25] + add_a0_q[24];
	add_b_q[13] <= add_a0_q[27] + add_a0_q[26];
	add_b_q[14] <= add_a0_q[29] + add_a0_q[28];
	add_b_q[15] <= add_a0_q[31] + add_a0_q[30];

	add_c_q[0]  <= add_b0_q[1] + add_b0_q[0];
	add_c_q[1]  <= add_b0_q[2] + add_b0_q[3];
	add_c_q[2]  <= add_b0_q[4] + add_b0_q[5];
	add_c_q[3]  <= add_b0_q[6] + add_b0_q[7];
	add_c_q[4]  <= add_b0_q[8] + add_b0_q[9];
	add_c_q[5]  <= add_b0_q[10]+ add_b0_q[11];
	add_c_q[6]  <= add_b0_q[12]+ add_b0_q[13];
	add_c_q[7]  <= add_b0_q[14]+ add_b0_q[15];

	add_d_q[0]  <= add_c0_q[1] + add_c0_q[0];
	add_d_q[1]  <= add_c0_q[2] + add_c0_q[3];
	add_d_q[2]  <= add_c0_q[4] + add_c0_q[5];
	add_d_q[3]  <= add_c0_q[6] + add_c0_q[7];
	
	add_e_q[0]  <= add_d0_q[1] + add_d0_q[0];
	add_e_q[1]  <= add_d0_q[2] + add_d0_q[3];
	
	sum_q 		<= add_e0_q[0] + add_e0_q[1];
end
//--------------------------------------------------------------------------------------------------
// Programmable receive gain, Q path.
//
// sum_q is the 43-bit filter accumulator. The original code took a fixed
// sum_q >>> 19 with round-half-up (and a tie-break that skips the increment on
// exact negative halves), then sliced [15:0] with NO saturation.
//
// Two things changed here:
//  1. sum_q_g pre-shifts the accumulator left by (8 + gain_shift) and the fixed
//     right shift becomes 27, so the net shift is 19 - gain_shift. Each gain_shift
//     step is +6.02 dB. gain_shift = 0 is BIT-EXACT with the original >>19: the
//     pre-shift by 8 puts sum_q[42:0] at sum_q_g[50:8], so sum_q_g[58:27]
//     == sum_q[42:19], sum_q_g[26] == sum_q[18], and the tie condition
//     sum_q_g[26:0] == 27'h4000000 reduces to sum_q[18:0] == 19'h40000.
//     59 bits = 43 + 16 so the maximum shift cannot push MSBs out.
//  2. sum_round_q is widened to 33 bits and the [15:0] slice is saturated.
//     The old I path declared sum_round_i as [23:0] while the expression assigned
//     to it was 25 bits, so the I path silently dropped a bit that Q kept.
//
// The two-register pipeline depth is preserved, so the dly_* alignment is unchanged.
wire signed			 [58:0]				sum_q_e = {{16{sum_q[42]}}, sum_q};
wire signed			 [58:0]				sum_q_g = sum_q_e <<< (5'd8 + gain_shift_q);
reg  signed			 [32:0]				sum_round_q;
reg  signed			 [15:0]				data_out_q;

always @(posedge clk)
begin
	if((sum_q_g[58] == 1'b1) & (sum_q_g[26:0] == 27'h4000000))begin
		sum_round_q <= {sum_q_g[58], sum_q_g[58:27]};
	end
	else begin
		sum_round_q <= {sum_q_g[58], sum_q_g[58:27]} + sum_q_g[26];
	end
end

always @(posedge clk)
begin 
	if(sum_round_q > 33'sd32767)			data_out_q <= 16'sh7FFF;
	else if(sum_round_q < -33'sd32768)	data_out_q <= 16'sh8000;
	else									data_out_q <= sum_round_q[15:0];
end
//--------------------------------------------------------------------------------------------------
reg					[76:0]						dly_headen;
reg					[76:0][15:0]				dly_head;
reg					[76:0]						dly_start;
reg					[76:0]						dly_last;
reg					[76:0]						dly_dataen;

always@(posedge clk)
begin
	dly_headen	<=	{dly_headen	,	demod_headen	};
	dly_head	<=	{dly_head	,	demod_head	};
	dly_start	<=	{dly_start	,	demod_start	};
	dly_last	<=	{dly_last	,	demod_last	};
	dly_dataen	<=	{dly_dataen	,	demod_dataen	};
end

//-------------------------------------------------------------------------------------------------- */
assign				filter_headen			=	dly_headen[75];
assign				filter_head				=	dly_head[75];
assign				filter_start			=	dly_start[75];
assign				filter_last				=	dly_last[75];
assign				filter_dataen			=	dly_dataen[75];
assign				filter_i				=	data_out_i;
assign				filter_q				=	data_out_q;
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
