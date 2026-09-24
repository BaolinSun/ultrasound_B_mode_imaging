`timescale 1ns/1ps

module tb_add_rf_head_pi;
reg clk = 1'b0;
reg reset = 1'b1;
reg rtc_enable = 1'b0;
reg rtc_trig = 1'b0;
reg [31:0] rtc_rfdot = 32'h0000_1800;
reg [7:0] rtc_shot = 8'd3;
reg [3:0] rtc_down = 4'd3;
reg rf_dataen = 1'b0;
reg [20:0] rf_data = 21'd0;
wire trans_headen;
wire [15:0] trans_head;
wire trans_start;
wire trans_last;
wire trans_dataen;
wire [20:0] trans_data;
wire [1:0] trans_phase;

integer header_index = 0;
reg [15:0] expected_header;

always #5 clk = ~clk;

add_rf_head dut
(
	.clk(clk), .reset(reset), .rtc_enable(rtc_enable),
	.rtc_trig(rtc_trig), .rtc_rfdot(rtc_rfdot),
	.rtc_shot(rtc_shot), .rtc_down(rtc_down),
	.rf_dataen(rf_dataen), .rf_data(rf_data),
	.trans_headen(trans_headen), .trans_head_d(trans_head),
	.trans_start(trans_start), .trans_last(trans_last),
	.trans_dataen(trans_dataen), .trans_data(trans_data),
	.trans_phase(trans_phase)
);

always@(posedge clk)
begin
	if(trans_headen) begin
		case(header_index)
			0: expected_header = 16'h1C00;
			1: expected_header = 16'h1800;
			2: expected_header = 16'h0000;
			3: expected_header = 16'h0003;
			4: expected_header = 16'h0000;
			5: expected_header = 16'h0000;
			6: expected_header = 16'h0000;
			default: expected_header = 16'h0000;
		endcase
		if(trans_head !== expected_header)
			$fatal(1, "header[%0d] mismatch: expected %h got %h",
			       header_index, expected_header, trans_head);
		if(trans_phase !== 2'd3)
			$fatal(1, "PI phase sideband mismatch: expected 3 got %0d", trans_phase);
		header_index <= header_index + 1;
	end
end

initial begin
	repeat(5) @(posedge clk);
	reset <= 1'b0;
	rtc_enable <= 1'b1;
	// Capture a deterministic zero timestamp before the RF burst.
	@(posedge clk);
	rtc_trig <= 1'b1;
	@(posedge clk);
	rtc_trig <= 1'b0;
	repeat(6) @(posedge clk);
	rf_dataen <= 1'b1;
	rf_data <= 21'sd17;
	repeat(4) @(posedge clk);
	rf_dataen <= 1'b0;
	rf_data <= 21'd0;
	wait(header_index == 8);
	repeat(4) @(posedge clk);
	$display("PASS: add_rf_head preserves 1C00/1800 and all eight protocol words");
	$finish;
end

initial begin
	#100000;
	$fatal(1, "timeout");
end
endmodule
