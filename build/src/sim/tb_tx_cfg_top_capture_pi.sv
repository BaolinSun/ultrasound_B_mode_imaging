`timescale 1ns/1ps

module tb_tx_cfg_top_capture_pi;
reg clk = 1'b0;
reg reset = 1'b1;
reg scan_enable = 1'b0;
reg tx_cfg_req = 1'b0;
reg [6:0] rtc_line = 7'd0;
reg rtc_polarity = 1'b0;
wire tx_cfg_ready;
wire tx_cfg_done;
reg spi_start = 1'b0;
reg power_hold=0;
wire power_idle;

wire tx_st_reg_rden;
wire [8:0] tx_st_reg_rdaddr;
reg [31:0] tx_st_reg_rddata = 32'd0;
wire tx_rt_reg_rden;
wire [13:0] tx_rt_reg_rdaddr;
reg [31:0] tx_rt_reg_rddata = 32'd0;

wire tx_spi_trig0;
wire tx_spi_mode0;
wire [9:0] tx0_spi_addr0;
wire [31:0] tx0_spi_txdata0;
wire tx_spi_trig1;
wire tx_spi_mode1;
wire [9:0] tx0_spi_addr1;
wire [31:0] tx0_spi_txdata1;
wire [9:0] tx1_spi_addr0;
wire [31:0] tx1_spi_txdata0;
wire [9:0] tx1_spi_addr1;
wire [31:0] tx1_spi_txdata1;
reg tx_spi_done0 = 1'b0;
reg tx_spi_done1 = 1'b0;
reg tx_spi_rdnext = 1'b0;

reg [31:0] static_mem [0:7];
reg [31:0] dynamic_mem [0:7];
integer spi_delay = 0;
integer dynamic_writes = 0;
reg dynamic_phase = 1'b0;

always #2.5 clk = ~clk;

tx_cfg_top dut
(
    .power_hold(power_hold), .power_idle(power_idle),
	.clk(clk), .reset(reset), .scan_enable(scan_enable),
	.tx_cfg_req(tx_cfg_req), .rtc_line(rtc_line),
	.rtc_polarity(rtc_polarity), .tx_cfg_ready(tx_cfg_ready),
	.tx_cfg_done(tx_cfg_done), .spi_start(spi_start),
	.tx_st_reg_num(7'd1), .tx_st_reg_rden(tx_st_reg_rden),
	.tx_st_reg_rdaddr(tx_st_reg_rdaddr), .tx_st_reg_rddata(tx_st_reg_rddata),
	.tx_rt_reg_num(5'd1), .tx_rt_reg_rden(tx_rt_reg_rden),
	.tx_rt_reg_rdaddr(tx_rt_reg_rdaddr), .tx_rt_reg_rddata(tx_rt_reg_rddata),
	.tx_spi_rdreq(1'b0), .tx_spi_rdaddr(10'd0),
	.tx_spi_trig0(tx_spi_trig0), .tx_spi_mode0(tx_spi_mode0),
	.tx0_spi_addr0(tx0_spi_addr0), .tx0_spi_txdata0(tx0_spi_txdata0),
	.tx_spi_trig1(tx_spi_trig1), .tx_spi_mode1(tx_spi_mode1),
	.tx0_spi_addr1(tx0_spi_addr1), .tx0_spi_txdata1(tx0_spi_txdata1),
	.tx1_spi_addr0(tx1_spi_addr0), .tx1_spi_txdata0(tx1_spi_txdata0),
	.tx1_spi_addr1(tx1_spi_addr1), .tx1_spi_txdata1(tx1_spi_txdata1),
	.tx_spi_done0(tx_spi_done0), .tx_spi_done1(tx_spi_done1),
	.tx_spi_rdnext(tx_spi_rdnext)
);

always@(posedge clk)
begin
	if(tx_st_reg_rden)
		tx_st_reg_rddata <= static_mem[tx_st_reg_rdaddr];
	if(tx_rt_reg_rden)
		tx_rt_reg_rddata <= dynamic_mem[tx_rt_reg_rdaddr];
end

always@(posedge clk)
begin
	tx_spi_done0 <= 1'b0;
	tx_spi_done1 <= 1'b0;
	tx_spi_rdnext <= 1'b0;
	if(tx_spi_trig0) begin
		if(spi_delay != 0)
			$fatal(1, "overlapping SPI transaction");
		spi_delay <= 6;
		if(dynamic_phase &&
		   ({tx0_spi_addr0, tx0_spi_addr1, tx1_spi_addr0, tx1_spi_addr1} == {4{10'h018}})) begin
			dynamic_writes <= dynamic_writes + 1;
		end
	end else if(spi_delay != 0) begin
		spi_delay <= spi_delay - 1;
		if(spi_delay == 4)
			tx_spi_rdnext <= 1'b1;
		if(spi_delay == 1) begin
			tx_spi_done0 <= 1'b1;
			tx_spi_done1 <= 1'b1;
		end
	end
end

task load_static_values;
	input [31:0] data00;
	input [31:0] data01;
	input [31:0] data10;
	input [31:0] data11;
	begin
		static_mem[0] = 32'h0000_0018;
		static_mem[1] = data00;
		static_mem[2] = 32'h0000_0018;
		static_mem[3] = data01;
		static_mem[4] = 32'h0000_0018;
		static_mem[5] = data10;
		static_mem[6] = 32'h0000_0018;
		static_mem[7] = data11;
	end
endtask

task start_static_config;
	begin
		@(posedge clk);
		spi_start <= 1'b1;
		@(posedge clk);
		spi_start <= 1'b0;
		wait(tx_cfg_ready);
		@(posedge clk);
	end
endtask

task request_and_check;
	input request_polarity;
	input [31:0] expected00;
	input [31:0] expected01;
	input [31:0] expected10;
	input [31:0] expected11;
	begin
		rtc_polarity <= request_polarity;
		dynamic_phase <= 1'b1;
		@(posedge clk);
		tx_cfg_req <= 1'b1;
		@(posedge clk);
		tx_cfg_req <= 1'b0;
		wait(tx_spi_trig0 &&
		     ({tx0_spi_addr0, tx0_spi_addr1, tx1_spi_addr0, tx1_spi_addr1} == {4{10'h018}}));
		#1;
		if(tx0_spi_txdata0 !== expected00 || tx0_spi_txdata1 !== expected01 ||
		   tx1_spi_txdata0 !== expected10 || tx1_spi_txdata1 !== expected11)
			$fatal(1, "captured register 18h values are wrong: %h %h %h %h",
			       tx0_spi_txdata0, tx0_spi_txdata1,
			       tx1_spi_txdata0, tx1_spi_txdata1);
		wait(tx_cfg_done);
		@(posedge clk);
		dynamic_phase <= 1'b0;
	end
endtask

initial begin
	dynamic_mem[0] = 32'h0000_0100;
	dynamic_mem[1] = 32'hA000_0000;
	dynamic_mem[2] = 32'h0000_0200;
	dynamic_mem[3] = 32'hB000_0000;
	dynamic_mem[4] = 32'h0000_0300;
	dynamic_mem[5] = 32'hC000_0000;
	dynamic_mem[6] = 32'h0000_0400;
	dynamic_mem[7] = 32'hD000_0000;
	load_static_values(32'h008C0043, 32'h018C0043,
	                   32'h028C0043, 32'h038C0043);
	repeat(5) @(posedge clk);
	reset <= 1'b0;
	start_static_config();
	request_and_check(1'b0, 32'h008C0003, 32'h018C0003,
	                  32'h028C0003, 32'h038C0003);
	request_and_check(1'b1, 32'h008C0043, 32'h018C0043,
	                  32'h028C0043, 32'h038C0043);

	// Simulate CFG BEGIN/END: leave run state, load new static values, and
	// prove the four saved bases are cleared and replaced independently.
	scan_enable <= 1'b1;
	repeat(3) @(posedge clk);
	scan_enable <= 1'b0;
	repeat(4) @(posedge clk);
	load_static_values(32'h108C0043, 32'h118C0043,
	                   32'h128C0043, 32'h138C0043);
	start_static_config();
	request_and_check(1'b0, 32'h108C0003, 32'h118C0003,
	                  32'h128C0003, 32'h138C0003);
    wait(power_idle);@(negedge clk);power_hold=1;repeat(6)@(negedge clk);
    if(tx_cfg_ready || dut.reg18_valid!=4'b1111)$fatal(1,"power rearm lost retained register cache");
    power_hold=0;start_static_config();
    request_and_check(1'b1,32'h108C0043,32'h118C0043,32'h128C0043,32'h138C0043);
	if(dynamic_writes != 4)
$fatal(1, "expected four synthetic register 18h writes, got %0d", dynamic_writes);
	$display("PASS: four static register 18h values are captured, masked and refreshed");
	$finish;
end

initial begin
	#200000;
	$fatal(1, "timeout");
end
endmodule
