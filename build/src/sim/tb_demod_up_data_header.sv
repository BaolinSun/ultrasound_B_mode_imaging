`timescale 1ns / 1ps

// Arithmetic IP models are intentionally minimal: this test exercises only
// the real line-header pipelines from demod_iq through log_compress.
module mult_17bx20b_d3(
	input CLK,
	input signed [16:0] A,
	input signed [19:0] B,
	output reg signed [36:0] P
);
	always @(posedge CLK)
		P <= A * B;
endmodule

module mult_26x12b_d3(
	input CLK,
	input signed [25:0] A,
	input signed [11:0] B,
	output reg signed [37:0] P
);
	always @(posedge CLK)
		P <= A * B;
endmodule

module mult_16bx16b_d3(
	input CLK,
	input signed [15:0] A,
	input signed [15:0] B,
	output reg signed [31:0] P
);
	always @(posedge CLK)
		P <= A * B;
endmodule

module square_33b_d17(
	input aclk,
	input s_axis_cartesian_tvalid,
	input [39:0] s_axis_cartesian_tdata,
	output reg m_axis_dout_tvalid,
	output reg [23:0] m_axis_dout_tdata
);
	always @(posedge aclk) begin
		m_axis_dout_tvalid <= s_axis_cartesian_tvalid;
		m_axis_dout_tdata <= 24'd0;
	end
endmodule
module tb_demod_up_data_header;
	reg clk = 1'b0;
	reg rst = 1'b1;
	always #5 clk = ~clk;

	reg apt_headen = 1'b0;
	reg [15:0] apt_head = 16'd0;
	reg apt_start = 1'b0;
	reg apt_last = 1'b0;
	reg apt_dataen = 1'b0;
	reg [31:0] apt_data = 32'd0;

	wire demod_sin_rden;
	wire [12:0] demod_sin_rdaddr;
	wire demod_cos_rden;
	wire [12:0] demod_cos_rdaddr;
	wire demod_headen;
	wire [15:0] demod_head;
	wire demod_start;
	wire demod_last;
	wire demod_dataen;
	wire [24:0] demod_i;
	wire [24:0] demod_q;

	demod_iq demod_iq_i (
		.clk(clk),
		.demod_sin_rden(demod_sin_rden),
		.demod_sin_rdaddr(demod_sin_rdaddr),
		.demod_sin_rddata(32'd0),
		.demod_cos_rden(demod_cos_rden),
		.demod_cos_rdaddr(demod_cos_rdaddr),
		.demod_cos_rddata(32'd0),
		.apt_headen(apt_headen),
		.apt_head(apt_head),
		.apt_start(apt_start),
		.apt_last(apt_last),
		.apt_dataen(apt_dataen),
		.apt_data(apt_data),
		.demod_headen(demod_headen),
		.demod_head(demod_head),
		.demod_start(demod_start),
		.demod_last(demod_last),
		.demod_dataen(demod_dataen),
		.demod_i(demod_i),
		.demod_q(demod_q)
	);

	wire filter_coef_rden;
	wire [12:0] filter_coef_rdaddr;
	wire filter_headen;
	wire [15:0] filter_head;
	wire filter_start;
	wire filter_last;
	wire filter_dataen;
	wire [15:0] filter_i;
	wire [15:0] filter_q;

	filter_iq filter_iq_i (
		.clk(clk),
		.rtc_rxen(1'b0),
		.gain_shift(4'd0),
		.filter_coef_rden(filter_coef_rden),
		.filter_coef_rdaddr(filter_coef_rdaddr),
		.filter_coef_rddata(16'd0),
		.demod_headen(demod_headen),
		.demod_head(demod_head),
		.demod_start(demod_start),
		.demod_last(demod_last),
		.demod_dataen(demod_dataen),
		.demod_i(demod_i),
		.demod_q(demod_q),
		.filter_headen(filter_headen),
		.filter_head(filter_head),
		.filter_start(filter_start),
		.filter_last(filter_last),
		.filter_dataen(filter_dataen),
		.filter_i(filter_i),
		.filter_q(filter_q)
	);

	wire envelop_headen;
	wire [15:0] envelop_head;
	wire envelop_start;
	wire envelop_last;
	wire envelop_dataen;
	wire [15:0] envelop_data;

	envelop_iq envelop_iq_i (
		.clk(clk),
		.rst(rst),
		.filter_headen(filter_headen),
		.filter_head(filter_head),
		.filter_start(filter_start),
		.filter_last(filter_last),
		.filter_dataen(filter_dataen),
		.filter_i(filter_i),
		.filter_q(filter_q),
		.envelop_headen(envelop_headen),
		.envelop_head(envelop_head),
		.envelop_start(envelop_start),
		.envelop_last(envelop_last),
		.envelop_dataen(envelop_dataen),
		.envelop_data(envelop_data)
	);

	wire down_headen;
	wire [15:0] down_head;
	wire down_start;
	wire down_last;
	wire down_dataen;
	wire [15:0] down_data;

	downsample downsample_i (
		.clk(clk),
		.rst(rst),
		.env_headen(envelop_headen),
		.env_head(envelop_head),
		.env_start(envelop_start),
		.env_last(envelop_last),
		.env_dataen(envelop_dataen),
		.env_data(envelop_data),
		.down_headen(down_headen),
		.down_head(down_head),
		.down_start(down_start),
		.down_last(down_last),
		.down_dataen(down_dataen),
		.down_data(down_data)
	);

	wire log1_coef_rden;
	wire [9:0] log1_coef_rdaddr;
	wire log2_coef_rden;
	wire [5:0] log2_coef_rdaddr;
	wire log_headen;
	wire [15:0] log_head;
	wire log_start;
	wire log_last;
	wire log_dataen;
	wire [15:0] log_data;

	log_compress log_compress_i (
		.clk(clk),
		.rst(rst),
		.log1_coef_rden(log1_coef_rden),
		.log1_coef_rdaddr(log1_coef_rdaddr),
		.log1_coef_rddata(16'd0),
		.log2_coef_rden(log2_coef_rden),
		.log2_coef_rdaddr(log2_coef_rdaddr),
		.log2_coef_rddata(16'd0),
		.down_headen(down_headen),
		.down_head(down_head),
		.down_start(down_start),
		.down_last(down_last),
		.down_dataen(down_dataen),
		.down_data(down_data),
		.log_headen(log_headen),
		.log_head(log_head),
		.log_start(log_start),
		.log_last(log_last),
		.log_dataen(log_dataen),
		.log_data(log_data)
	);

	// Exact proc_top output mux, included so the self-check observes the same
	// registered up_data/up_dataen interface used by the ILA and PC path.
	reg up_dataen = 1'b0;
	reg [15:0] up_data = 16'd0;
	always @(posedge clk) begin
		if(log_headen)
			up_dataen <= 1'b1;
		else if(log_dataen)
			up_dataen <= 1'b1;
		else
			up_dataen <= 1'b0;

		if(log_headen)
			up_data <= log_head;
		else if(log_dataen)
			up_data <= log_data;
		else
			up_data <= 16'd0;
	end

	reg [15:0] expected [0:7];
	integer demod_count = 0;
	integer up_count = 0;
	integer idle_count = 0;

	always @(negedge clk) begin
		if(demod_headen) begin
			if(demod_count >= 8)
				$fatal(1, "demod_iq emitted more than eight header words");
			if(demod_head !== expected[demod_count])
				$fatal(1, "demod header[%0d]: expected %04h got %04h",
				       demod_count, expected[demod_count], demod_head);
			demod_count = demod_count + 1;
		end

		if(up_dataen) begin
			if(up_count >= 8)
				$fatal(1, "up_data emitted more than eight header words");
			if(up_data !== expected[up_count])
				$fatal(1, "up_data header[%0d]: expected %04h got %04h",
				       up_count, expected[up_count], up_data);
			up_count = up_count + 1;
			idle_count = 0;
		end else if(up_count == 8) begin
			idle_count = idle_count + 1;
		end
	end

	integer i;
	initial begin
		expected[0] = 16'h1C00;
		expected[1] = 16'h1800;
		expected[2] = 16'h002B;
		expected[3] = 16'h0000;
		expected[4] = 16'h0000;
		expected[5] = 16'h0000;
		expected[6] = 16'h0011;
		expected[7] = 16'h43F0;

		// Flush resetless legacy delay arrays to a known idle state.
		repeat(220) @(negedge clk);
		rst = 1'b0;
		repeat(10) @(negedge clk);

		for(i = 0; i < 8; i = i + 1) begin
			apt_headen = 1'b1;
			apt_head = expected[i];
			@(negedge clk);
		end
		apt_headen = 1'b0;
		apt_head = 16'd0;

		fork
			begin
				wait(idle_count >= 12);
				if(demod_count != 8)
					$fatal(1, "demod_iq emitted %0d header words, expected 8", demod_count);
				if(up_count != 8)
					$fatal(1, "up_data emitted %0d header words, expected 8", up_count);
				$display("PASS: demod_iq and up_data preserve all eight fixed-header words");
				$finish;
			end
			begin
				repeat(500) @(negedge clk);
				$fatal(1, "timeout waiting for complete up_data header");
			end
		join_any
	end
endmodule
