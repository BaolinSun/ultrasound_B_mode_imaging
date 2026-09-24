`timescale 1ns/1ps

module tb_scan_sync_pi;
reg clk = 1'b0;
reg reset = 1'b1;
reg rtc_enable = 1'b0;
reg tx_cfg_ready = 1'b1;
reg tx_cfg_done = 1'b0;
wire tx_cfg_req;
wire rtc_trig;
wire rtc_txen;
wire rtc_rxen;
wire rtc_bfen;
wire rtc_tgc_slope;
wire rtc_tgc_updown;
wire [6:0] rtc_line;
wire [7:0] rtc_shot;
wire beamform_active;

always #2.5 clk = ~clk;

scan_sync dut
(
	.clk(clk), .reset(reset), .rtc_enable(rtc_enable),
	.rtc_period(32'd2), .rt_length(16'd1),
	.tx_start(16'd0), .tx_length(16'd1),
	.rx_start(16'd0), .rx_length(32'd1),
	.bf_start(32'd0), .bf_length(32'd1),
	.tx_cfg_ready(tx_cfg_ready), .tx_cfg_done(tx_cfg_done),
	.tx_cfg_req(tx_cfg_req), .rtc_trig(rtc_trig),
	.rtc_txen(rtc_txen), .rtc_rxen(rtc_rxen), .rtc_bfen(rtc_bfen),
	.rtc_tgc_slope(rtc_tgc_slope), .rtc_tgc_updown(rtc_tgc_updown),
	.rtc_line(rtc_line), .rtc_shot(rtc_shot),
	.beamform_active(beamform_active)
);

integer cfg_delay = 0;
integer cfg_requests = 0;
reg cfg_complete = 1'b0;

always@(posedge clk)
begin
	tx_cfg_done <= 1'b0;
	if(tx_cfg_req) begin
		if(rtc_trig || rtc_txen || rtc_rxen || rtc_bfen)
			$fatal(1, "TX config request overlapped a timed shot");
		cfg_requests <= cfg_requests + 1;
		cfg_delay <= 3;
		cfg_complete <= 1'b0;
	end else if(cfg_delay != 0) begin
		cfg_delay <= cfg_delay - 1;
		if(cfg_delay == 1) begin
			tx_cfg_done <= 1'b1;
			cfg_complete <= 1'b1;
		end
	end
end

reg rtc_trig_d = 1'b0;
integer shots_seen = 0;
always@(posedge clk)
begin
	rtc_trig_d <= rtc_trig;
	if(rtc_trig && !rtc_trig_d) begin
		if(!cfg_complete)
			$fatal(1, "shot %0d started before TX configuration completed", rtc_shot);
		if(rtc_shot !== shots_seen[7:0])
			$fatal(1, "shot order error: expected %0d got %0d", shots_seen, rtc_shot);
		if(rtc_line !== (rtc_shot >> 1))
			$fatal(1, "legacy line mapping error at shot %0d", rtc_shot);
		if(rtc_shot[1:0] !== shots_seen[1:0])
			$fatal(1, "A+/A-/B+/B- phase mapping error");
		shots_seen <= shots_seen + 1;
		if(shots_seen == 5)
			rtc_enable <= 1'b0;
	end
end

initial begin
	repeat(5) @(posedge clk);
	reset <= 1'b0;
	repeat(3) @(posedge clk);
	rtc_enable <= 1'b1;

	wait(shots_seen == 256);
	wait(!beamform_active);
	repeat(4) @(posedge clk);
	if(cfg_requests != 256)
		$fatal(1, "expected 256 config requests, got %0d", cfg_requests);
	if(rtc_shot != 8'd255)
		$fatal(1, "stop did not finish on physical shot 255");
	$display("PASS: 256 shots map to 64 A+/A-/B+/B- groups and stop at shot 255");
	$finish;
end

initial begin
	#200000;
	$fatal(1, "timeout");
end
endmodule
