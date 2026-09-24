`timescale 1ns/1ps

// Behavioral model of the common-clock Standard FIFO used by apt_add.
module fifo_32bx8192
(
	input clk,
	input rst,
	input [31:0] din,
	input wr_en,
	input rd_en,
	output reg [31:0] dout,
	output full,
	output empty
);
reg [31:0] mem [0:8191];
integer wr_ptr;
integer rd_ptr;
integer count;
assign full = (count == 8192);
assign empty = (count == 0);

always@(posedge clk or posedge rst)
begin
	if(rst) begin
		wr_ptr <= 0;
		rd_ptr <= 0;
		count <= 0;
		dout <= 32'd0;
	end else begin
		if(rd_en) begin
			if(count == 0)
				$fatal(1, "FIFO underflow");
			dout <= mem[rd_ptr];
			rd_ptr <= (rd_ptr == 8191) ? 0 : rd_ptr + 1;
		end
		if(wr_en) begin
			if(count == 8192)
				$fatal(1, "FIFO overflow");
			mem[wr_ptr] <= din;
			wr_ptr <= (wr_ptr == 8191) ? 0 : wr_ptr + 1;
		end
		case({wr_en,rd_en})
			2'b10: count <= count + 1;
			2'b01: count <= count - 1;
			default: count <= count;
		endcase
	end
end
endmodule
module tb_apt_add_pi;
reg clk = 1'b0;
reg rst = 1'b1;
reg rtc_enable = 1'b0;
reg [1:0] trans_phase = 2'd0;
reg trans_headen = 1'b0;
reg [15:0] trans_head = 16'd0;
reg trans_start = 1'b0;
reg trans_last = 1'b0;
reg trans_dataen = 1'b0;
reg signed [20:0] trans_data = 21'sd0;
wire apt_headen;
wire [15:0] apt_head;
wire apt_start;
wire apt_last;
wire apt_dataen;
wire [31:0] apt_data;

always #5 clk = ~clk;

apt_add dut
(
	.clk(clk), .rst(rst), .rtc_enable(rtc_enable),
	.trans_phase(trans_phase),
	.trans_headen(trans_headen), .trans_head(trans_head),
	.trans_start(trans_start), .trans_last(trans_last),
	.trans_dataen(trans_dataen), .trans_data(trans_data),
	.apt_headen(apt_headen), .apt_head(apt_head),
	.apt_start(apt_start), .apt_last(apt_last),
	.apt_dataen(apt_dataen), .apt_data(apt_data)
);

function integer harmonic_a;
	input integer sample;
	harmonic_a = (sample % 29) - 14;
endfunction

function integer harmonic_b;
	input integer sample;
	harmonic_b = (sample % 31) - 15;
endfunction

function integer shot_value;
	input integer phase;
	input integer sample;
	integer fundamental_a;
	integer fundamental_b;
	begin
		fundamental_a = (sample % 101) - 50;
		fundamental_b = (sample % 77) - 38;
		case(phase)
			0: shot_value =  fundamental_a + harmonic_a(sample);
			1: shot_value = -fundamental_a + harmonic_a(sample);
			2: shot_value =  fundamental_b + harmonic_b(sample);
			default: shot_value = -fundamental_b + harmonic_b(sample);
		endcase
	end
endfunction

task send_header;
	input integer shot;
	integer h;
	begin
		trans_phase <= shot[1:0];
		@(posedge clk);
		for(h = 0; h < 8; h = h + 1) begin
			@(posedge clk);
			trans_headen <= 1'b1;
			case(h)
				0: trans_head <= 16'h1C00;
				1: trans_head <= 16'd6144;
				2: trans_head <= shot[7:2];
				3: trans_head <= 16'h0003;
				4: trans_head <= 16'h0000;
				5: trans_head <= 16'h0000;
				6: trans_head <= 16'h3333;
				default: trans_head <= 16'h4444;
			endcase
		end
		@(posedge clk);
		trans_headen <= 1'b0;
		trans_head <= 16'd0;
	end
endtask

task send_normal_shot;
	input integer shot;
	integer sample;
	integer phase;
	begin
		phase = shot & 3;
		send_header(shot);
		repeat(5) @(posedge clk);
		for(sample = 0; sample < 6144; sample = sample + 1) begin
			@(posedge clk);
			trans_dataen <= 1'b1;
			trans_start <= (sample == 0);
			trans_last <= (sample == 6143);
			trans_data <= shot_value(phase, sample);
		end
		@(posedge clk);
		trans_dataen <= 1'b0;
		trans_start <= 1'b0;
		trans_last <= 1'b0;
		trans_data <= 21'sd0;
		repeat(8) @(posedge clk);
	end
endtask

task send_saturation_shot;
	input integer shot;
	integer sample;
	begin
		send_header(shot);
		repeat(5) @(posedge clk);
		for(sample = 0; sample < 2; sample = sample + 1) begin
			@(posedge clk);
			trans_dataen <= 1'b1;
			trans_start <= (sample == 0);
			trans_last <= (sample == 1);
			trans_data <= (sample == 0) ? 21'sh0FFFFF : 21'sh100000;
		end
		@(posedge clk);
		trans_dataen <= 1'b0;
		trans_start <= 1'b0;
		trans_last <= 1'b0;
		trans_data <= 21'sd0;
		repeat(8) @(posedge clk);
	end
endtask

integer output_headers = 0;
integer header_index = 0;
integer output_samples = 0;
integer expected;
integer frame_headers = 0;
integer frame_shot;
reg frame_header_mode = 1'b0;
reg signed [31:0] actual;
integer expected_line;
reg [15:0] expected_header;

always@(posedge clk)
begin
	if(apt_headen) begin
		if(frame_header_mode)
			expected_line = frame_headers;
		else if(output_headers == 0)
			expected_line = 7;
		else
			expected_line = 8;

		case(header_index)
			0: expected_header = 16'h1C00;
			1: expected_header = 16'h1800;
			2: expected_header = expected_line[15:0];
			3: expected_header = 16'h0003;
			4: expected_header = 16'h0000;
			5: expected_header = 16'h0000;
			6: expected_header = 16'h3333;
			default: expected_header = 16'h4444;
		endcase
		if(apt_head !== expected_header)
			$fatal(1, "header[%0d] mismatch: expected %h got %h",
			       header_index, expected_header, apt_head);
		header_index <= header_index + 1;
		if(header_index == 7) begin
			header_index <= 0;
			output_headers <= output_headers + 1;
			if(frame_header_mode)
				frame_headers <= frame_headers + 1;
		end
	end

	if(apt_dataen) begin
		actual = $signed(apt_data);
		if(output_headers == 1) begin
			expected = harmonic_a(output_samples) + harmonic_b(output_samples);
			if(actual !== expected)
				$fatal(1, "sample %0d mismatch: expected %0d got %0d", output_samples, expected, actual);
			if(apt_start !== (output_samples == 0))
				$fatal(1, "start marker mismatch at sample %0d", output_samples);
			if(apt_last !== (output_samples == 6143))
				$fatal(1, "last marker mismatch at sample %0d", output_samples);
		end else if(output_headers == 2) begin
			expected = (output_samples == 6144) ? 524287 : -524288;
			if(actual !== expected)
				$fatal(1, "saturation mismatch: expected %0d got %0d", expected, actual);
		end
		output_samples <= output_samples + 1;
	end
end

initial begin
	repeat(5) @(posedge clk);
	rst <= 1'b0;
	rtc_enable <= 1'b1;
	send_normal_shot(28);
	send_normal_shot(29);
	send_normal_shot(30);
	send_normal_shot(31);
	if(output_samples != 6144)
		$fatal(1, "expected 6144 PI samples, got %0d", output_samples);
	if(!dut.fifo_empty)
		$fatal(1, "FIFO not empty after B- phase");

	send_saturation_shot(32);
	send_saturation_shot(33);
	send_saturation_shot(34);
	send_saturation_shot(35);
	repeat(12) @(posedge clk);
	if(output_samples != 6146)
		$fatal(1, "expected 6146 total output samples, got %0d", output_samples);
	if(output_headers != 2)
		$fatal(1, "expected exactly two output headers, got %0d", output_headers);
	if(!dut.fifo_empty)
		$fatal(1, "FIFO retained data after final phase");

	// 256 physical headers must produce only 64 logical B- headers, 0..63.
	frame_header_mode <= 1'b1;
	for(frame_shot = 0; frame_shot < 256; frame_shot = frame_shot + 1)
		send_header(frame_shot);
	repeat(12) @(posedge clk);
	if(frame_headers != 64)
		$fatal(1, "expected 64 logical line headers, got %0d", frame_headers);
	if(output_headers != 66)
		$fatal(1, "unexpected total output header count: %0d", output_headers);
	frame_header_mode <= 1'b0;

	rtc_enable <= 1'b0;
	repeat(3) @(posedge clk);
	if(!dut.fifo_empty)
		$fatal(1, "FIFO did not clear when scanning stopped");
	$display("PASS: 21-bit RF, 6144 aligned samples, PI cancellation, /2, saturation, 64-line headers");
	$finish;
end

initial begin
	#2000000;
	$fatal(1, "timeout");
end
endmodule
