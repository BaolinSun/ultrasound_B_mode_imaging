`timescale 1ns / 1ps

module apt_add
(
	input                           clk,
	input                           rst,
	input                           rtc_enable,
	input             [1:0]        trans_phase,

	input                           trans_headen,
	input             [15:0]        trans_head,
	input                           trans_start,
	input                           trans_last,
	input                           trans_dataen,
	input      signed [20:0]        trans_data,

	output reg                      apt_headen,
	output reg        [15:0]        apt_head,
	output reg                      apt_start,
	output reg                      apt_last,
	output reg                      apt_dataen,
	output reg        [31:0]        apt_data
);

// PI phase is carried out-of-band so the fixed eight-word line header is
// preserved bit-for-bit. Latch it at the first header word of each shot.
reg       trans_headen_d;
reg [1:0] active_phase;

always@(posedge clk)
begin
	if(rst || !rtc_enable) begin
		trans_headen_d <= 1'b0;
		active_phase <= 2'd0;
	end else begin
		trans_headen_d <= trans_headen;
		if(trans_headen && !trans_headen_d)
			active_phase <= trans_phase;
	end
end

reg                      fifo_wren;
reg        [31:0]        fifo_wrdata;
reg                      fifo_rden;
wire       [31:0]        fifo_rddata;
wire                     fifo_full;
wire                     fifo_empty;
wire                     fifo_reset = rst | !rtc_enable;

fifo_32bx8192 fifo_apt_i
(
	.clk      (clk),
	.rst      (fifo_reset),
	.din      (fifo_wrdata),
	.wr_en    (fifo_wren),
	.rd_en    (fifo_rden),
	.dout     (fifo_rddata),
	.full     (fifo_full),
	.empty    (fifo_empty)
);

// The FIFO is Standard mode. These three RF pipeline stages plus the
// fifo_rddata register preserve sample-for-sample alignment on read/add/write.
reg signed [31:0] trans_data_d0;
reg signed [31:0] trans_data_d1;
reg signed [31:0] trans_data_d2;
reg signed [31:0] fifo_rddata_d1;

reg        [5:0]        trans_dataen_dly;
reg        [5:0]        trans_headen_dly;
reg        [5:0]        trans_start_dly;
reg        [5:0]        trans_last_dly;
reg        [5:0][15:0]  trans_head_dly;

always@(posedge clk)
begin
	if(rst || !rtc_enable) begin
		trans_data_d0 <= 32'sd0;
		trans_data_d1 <= 32'sd0;
		trans_data_d2 <= 32'sd0;
		fifo_rddata_d1 <= 32'sd0;
		trans_dataen_dly <= 6'd0;
		trans_headen_dly <= 6'd0;
		trans_start_dly <= 6'd0;
		trans_last_dly <= 6'd0;
		trans_head_dly <= '0;
	end else begin
		trans_data_d0 <= {{11{trans_data[20]}}, trans_data};
		trans_data_d1 <= trans_data_d0;
		trans_data_d2 <= trans_data_d1;
		fifo_rddata_d1 <= fifo_rddata;
		trans_dataen_dly <= {trans_dataen_dly, trans_dataen};
		trans_headen_dly <= {trans_headen_dly, trans_headen};
		trans_start_dly <= {trans_start_dly, trans_start};
		trans_last_dly <= {trans_last_dly, trans_last};
		trans_head_dly <= {trans_head_dly, trans_head};
	end
end

wire signed [31:0] sum_comb = fifo_rddata_d1 + trans_data_d2;

// phase0 stores A+. phases1/2 pop, add and append. phase3 pops, adds and
// deliberately does not write back, so the FIFO is empty after every line.
always@(posedge clk)
begin
	if(rst || !rtc_enable) begin
		fifo_wren <= 1'b0;
		fifo_wrdata <= 32'd0;
		fifo_rden <= 1'b0;
	end else begin
		case(active_phase)
			2'd0: begin
				fifo_wren <= trans_dataen;
				fifo_wrdata <= {{11{trans_data[20]}}, trans_data};
				fifo_rden <= 1'b0;
			end
			2'd1, 2'd2: begin
				fifo_wren <= trans_dataen_dly[2];
				fifo_wrdata <= sum_comb;
				fifo_rden <= trans_dataen;
			end
			default: begin
				fifo_wren <= 1'b0;
				fifo_wrdata <= 32'd0;
				fifo_rden <= trans_dataen;
			end
		endcase
	end
end

function automatic signed [19:0] sat_round_div2;
	input signed [31:0] value;
	reg signed [31:0] scaled;
	begin
		// For odd values, positive adds one before shifting; signed negative
		// arithmetic shift already rounds away from zero.
		if(value[31])
			scaled = value >>> 1;
		else
			scaled = (value + 32'sd1) >>> 1;

		if(scaled > 32'sd524287)
			sat_round_div2 = 20'sh7FFFF;
		else if(scaled < (-32'sd524288))
			sat_round_div2 = 20'sh80000;
		else
			sat_round_div2 = scaled[19:0];
	end
endfunction

wire signed [19:0] pi_rf = sat_round_div2(sum_comb);

always@(posedge clk)
begin
	if(rst || !rtc_enable) begin
		apt_data <= 32'd0;
		apt_dataen <= 1'b0;
		apt_headen <= 1'b0;
		apt_start <= 1'b0;
		apt_last <= 1'b0;
		apt_head <= 16'd0;
	end else if(active_phase == 2'd3) begin
		apt_data <= {{12{pi_rf[19]}}, pi_rf};
		apt_dataen <= trans_dataen_dly[2];
		apt_headen <= trans_headen_dly[2];
		apt_start <= trans_start_dly[2];
		apt_last <= trans_last_dly[2];
		apt_head <= trans_head_dly[2];
	end else begin
		apt_data <= 32'd0;
		apt_dataen <= 1'b0;
		apt_headen <= 1'b0;
		apt_start <= 1'b0;
		apt_last <= 1'b0;
		apt_head <= 16'd0;
	end
end

endmodule
