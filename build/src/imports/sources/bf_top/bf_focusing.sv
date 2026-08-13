module bf_focusing
(
	input										clk,
	input										ad_en_pre,
	input										ad_en_it,
	input			[31:0][15:0]				ad_ch_it,

	input										bf_rtc_bfst,
	input										dly_en_pre,
	input										dly_dataen,
	input			[31:0][12:0]				dly_data,
	output	reg									foc_dataen,
	output	reg		[31:0][15:0]				foc_data
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg					[12:0]						foc_wrcnt;

always@(posedge clk)
begin
	if(bf_rtc_bfst)		foc_wrcnt <= 0;
	else if(ad_en_pre)	foc_wrcnt <= foc_wrcnt + 1'b1;
end
//--------------------------------------------------------------------------------------------------
//					cache dpram write and read
//--------------------------------------------------------------------------------------------------
reg					[31:0]					foc_wren;
reg					[31:0][12:0]			foc_wraddr;
wire				[31:0][15:0]			foc_wrdata;
reg					[31:0]					foc_rden;
reg					[31:0][12:0]			foc_rdaddr;
reg					[31:0][15:0]			foc_rddata;

genvar num_ch;
generate
	for(num_ch=0; num_ch < 32; num_ch=num_ch+1)
	begin:ram
		always@(posedge clk)
		begin
			foc_wren[num_ch] <= ad_en_pre;
			foc_wraddr[num_ch] <= foc_wrcnt;
		end

		assign		foc_wrdata[num_ch]		=	ad_ch_it[num_ch];
		assign		foc_rdaddr[num_ch]		=	dly_data[num_ch];

		always@(posedge clk)
		begin
			foc_rden[num_ch] <= dly_en_pre;

			foc_data[num_ch] <= foc_rddata[num_ch];
		end

		(* ram_style = "block" *) reg [15:0] foc_ram [0:8191];

		always@(posedge clk)
		begin
			if(foc_wren[num_ch])
				foc_ram[foc_wraddr[num_ch]] <= foc_wrdata[num_ch];
			if(foc_rden[num_ch])
				foc_rddata[num_ch] <= foc_ram[foc_rdaddr[num_ch]];
		end
	end
endgenerate
//--------------------------------------------------------------------------------------------------
reg												dly_dataen_t0;

always@(posedge clk)
begin
	dly_dataen_t0 <= dly_dataen;
	foc_dataen <= dly_dataen_t0;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
