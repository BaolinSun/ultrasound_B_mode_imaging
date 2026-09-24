module demod_iq
(
	input                                       clk,
	output reg                                  demod_sin_rden,
	output reg [12:0]                           demod_sin_rdaddr,
	input      [31:0]                           demod_sin_rddata,
	output reg                                  demod_cos_rden,
	output reg [12:0]                           demod_cos_rdaddr,
	input      [31:0]                           demod_cos_rddata,

	input                                       apt_headen,
	input      [15:0]                           apt_head,
	input                                       apt_start,
	input                                       apt_last,
	input                                       apt_dataen,
	input      [31:0]                           apt_data,

	output                                      demod_headen,
	output     [15:0]                           demod_head,
	output                                      demod_start,
	output                                      demod_last,
	output                                      demod_dataen,
	output     [24:0]                           demod_i,
	output     [24:0]                           demod_q
);

reg        [31:0]       dly_headen;
reg        [31:0][15:0] dly_head;
reg        [31:0]       dly_start;
reg        [31:0]       dly_last;
reg        [31:0]       dly_dataen;
reg signed [26:0][31:0] dly_data;

always@(posedge clk)
begin
	dly_headen <= {dly_headen, apt_headen};
	// The upstream fixed header already carries the logical receive line 0..63.
	dly_head <= {dly_head, apt_head};
	dly_start <= {dly_start, apt_start};
	dly_last <= {dly_last, apt_last};
	dly_dataen <= {dly_dataen, apt_dataen};
	dly_data <= {dly_data, apt_data};
end

reg [12:0] cnt_dep;

always@(posedge clk)
begin
	if(!apt_dataen)
		cnt_dep <= 0;
	else
		cnt_dep <= cnt_dep + 1'b1;

	demod_sin_rden <= apt_dataen;
	demod_sin_rdaddr <= cnt_dep;
	demod_cos_rden <= apt_dataen;
	demod_cos_rdaddr <= cnt_dep;
end

reg  signed [16:0] data_sin;
reg  signed [16:0] data_cos;
reg  signed [19:0] data_apt;
wire        [36:0] data_msin;
wire        [36:0] data_mcos;

always@(posedge clk)
begin
	data_sin <= demod_sin_rddata[16:0];
	data_cos <= demod_cos_rddata[16:0];
	// apt_add already saturates PI data. Keep this independent second guard so
	// any future 32-bit source cannot wrap at the signed 20-bit multiplier.
	if($signed(dly_data[1]) > 32'sd524287)
		data_apt <= 20'sh7FFFF;
	else if($signed(dly_data[1]) < -32'sd524288)
		data_apt <= 20'sh80000;
	else
		data_apt <= dly_data[1][19:0];
end

mult_17bx20b_d3 mult_sin
(
	.CLK (clk),
	.A   (data_sin),
	.B   (data_apt),
	.P   (data_msin)
);

mult_17bx20b_d3 mult_cos
(
	.CLK (clk),
	.A   (data_cos),
	.B   (data_apt),
	.P   (data_mcos)
);

reg signed [24:0] data_cuti;
reg signed [24:0] data_cutq;

always@(posedge clk)
begin
	if((data_msin[36] == 1'b1) & (data_msin[35:34] != 2'b11))
		data_cutq <= {1'b1, 24'd0};
	else if((data_msin[36] == 1'b0) & (data_msin[35:34] != 2'b00))
		data_cutq <= 25'h0ff_ffff;
	else begin
		if((data_msin[36] == 1'b1) & (data_msin[9:0] == 10'h200))
			data_cutq <= data_msin[34:10];
		else
			data_cutq <= data_msin[34:10] + data_msin[9];
	end

	if((data_mcos[36] == 1'b1) & (data_mcos[35:34] != 2'b11))
		data_cuti <= {1'b1, 24'd0};
	else if((data_mcos[36] == 1'b0) & (data_mcos[35:34] != 2'b00))
		data_cuti <= 25'h0ff_ffff;
	else begin
		if((data_mcos[36] == 1'b1) & (data_mcos[9:0] == 10'h200))
			data_cuti <= data_mcos[34:10];
		else
			data_cuti <= data_mcos[34:10] + data_mcos[9];
	end
end

assign demod_headen = dly_headen[6];
// apt_head is inserted directly into dly_head.  Keep the data and valid
// selections on the same stage so the first fixed-header word is not skipped.
assign demod_head = dly_head[6];
assign demod_start = dly_start[6];
assign demod_last = dly_last[6];
assign demod_dataen = dly_dataen[6];
assign demod_i = data_cuti;
assign demod_q = data_cutq;

endmodule
