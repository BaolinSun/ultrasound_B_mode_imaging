module bf_madd
(	
	input										clk,
	input										foc_dataen,
	input			[31:0][15:0]				foc_data,
	input			[31:0][7:0]					apod_data,
	output										madd_dataen,
	output	reg		[20:0]						madd_data
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire	signed		[15:0][24:0]	data_a;
wire	signed		[15:0][25:0]	data_b;

genvar num_ch;
generate
	for(num_ch=0; num_ch < 16; num_ch=num_ch+1)
	begin:dsp_ch
		dsp_madd_a								dsp_madd_a
		(	
			.CLK								(clk),
			.A									(foc_data[num_ch*2+0]),
			.B									({1'b0,apod_data[num_ch*2+0]}),
			.P									(data_a[num_ch])
		);	
	
		dsp_madd_b								dsp_madd_b
		(	
			.CLK								(clk),
			.A									(foc_data[num_ch*2+1]),
			.B									({1'b0,apod_data[num_ch*2+1]}),
			.C									(data_a[num_ch]),
			.P									(data_b[num_ch])
		);	
	end
endgenerate
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg		signed		[7:0][30:0]			add_a;
reg		signed		[3:0][30:0]			add_b;
reg		signed		[1:0][30:0]			add_c;
reg		signed			[30:0]			add_d;

always@(posedge clk)
begin
	add_a[00]	<=	{{5{data_b[00][25]}},data_b[00]}	+	{{5{data_b[01][25]}},data_b[01]};
	add_a[01]	<=	{{5{data_b[02][25]}},data_b[02]}	+	{{5{data_b[03][25]}},data_b[03]};
	add_a[02]	<=	{{5{data_b[04][25]}},data_b[04]}	+	{{5{data_b[05][25]}},data_b[05]};
	add_a[03]	<=	{{5{data_b[06][25]}},data_b[06]}	+	{{5{data_b[07][25]}},data_b[07]};
	add_a[04]	<=	{{5{data_b[08][25]}},data_b[08]}	+	{{5{data_b[09][25]}},data_b[09]};
	add_a[05]	<=	{{5{data_b[10][25]}},data_b[10]}	+	{{5{data_b[11][25]}},data_b[11]};
	add_a[06]	<=	{{5{data_b[12][25]}},data_b[12]}	+	{{5{data_b[13][25]}},data_b[13]};
	add_a[07]	<=	{{5{data_b[14][25]}},data_b[14]}	+	{{5{data_b[15][25]}},data_b[15]};

	add_b[0]	<=	add_a[00]	+	add_a[01];
	add_b[1]	<=	add_a[02]	+	add_a[03];
	add_b[2]	<=	add_a[04]	+	add_a[05];
	add_b[3]	<=	add_a[06]	+	add_a[07];

	add_c[0]	<=	add_b[0]	+	add_b[1];
	add_c[1]	<=	add_b[2]	+	add_b[3];

	add_d	<=	add_c[0]	+	add_c[1];
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg					[11:0]						foc_dataen_dly;
reg	signed			[20:0]						add_s;

always@(posedge clk)
begin
	foc_dataen_dly <= {foc_dataen_dly, foc_dataen};

	if((add_d[7:0] == 8'b1000_0000) & add_d[30])
		add_s <= add_d[30:8];
	else
		add_s <= add_d[30:8] + add_d[7];

	madd_data	<=	add_s;
end
//--------------------------------------------------------------------------------------------------
assign			madd_dataen						=	foc_dataen_dly[10];
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
