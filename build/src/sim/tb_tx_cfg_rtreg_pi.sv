`timescale 1ns/1ps

module tb_tx_cfg_rtreg_pi;
reg clk = 1'b0;
reg reset = 1'b1;
reg [6:0] rtc_line = 7'd5;
reg polarity = 1'b0;
reg cfg_req = 1'b0;
wire cfg_done;
wire bram_rden;
wire [13:0] bram_addr;
reg [31:0] bram_data = 32'd0;
wire spi_trig;
wire spi_mode;
wire [9:0] addr00, addr01, addr10, addr11;
wire [31:0] data00, data01, data10, data11;
reg spi_done = 1'b0;
reg spi_rdnext = 1'b0;

reg [31:0] memory [0:16383];
integer i;
integer base;
always #2.5 clk = ~clk;

tx_cfg_rtreg dut
(
	.clk(clk), .reset(reset), .rtc_line(rtc_line), .polarity(polarity),
	.rtreg_cfg_en(cfg_req), .rtreg_cfg_done(cfg_done),
	.reg18_base_tx0_0(32'h008C0003),
	.reg18_base_tx0_1(32'h018C0003),
	.reg18_base_tx1_0(32'h028C0003),
	.reg18_base_tx1_1(32'h038C0003),
	.tx_rt_reg_num(5'd9), .tx_rt_reg_rden(bram_rden),
	.tx_rt_reg_rdaddr(bram_addr), .tx_rt_reg_rddata(bram_data),
	.tx_rtreg_spi_trig(spi_trig), .tx_rtreg_spi_mode(spi_mode),
	.tx0_rtreg_spi_addr0(addr00), .tx0_rtreg_spi_txdata0(data00),
	.tx0_rtreg_spi_addr1(addr01), .tx0_rtreg_spi_txdata1(data01),
	.tx1_rtreg_spi_addr0(addr10), .tx1_rtreg_spi_txdata0(data10),
	.tx1_rtreg_spi_addr1(addr11), .tx1_rtreg_spi_txdata1(data11),
	.tx_spi_done(spi_done), .tx_spi_rdnext(spi_rdnext)
);

always@(posedge clk)
begin
	if(bram_rden)
		bram_data <= memory[bram_addr];
end

integer spi_delay = 0;
always@(posedge clk)
begin
	spi_done <= 1'b0;
	spi_rdnext <= 1'b0;
	if(spi_trig) begin
		if(spi_delay != 0)
			$fatal(1, "overlapping SPI writes");
		spi_delay <= 6;
	end else if(spi_delay != 0) begin
		spi_delay <= spi_delay - 1;
		if(spi_delay == 4)
			spi_rdnext <= 1'b1;
		if(spi_delay == 1)
			spi_done <= 1'b1;
	end
end

integer trigger_count = 0;
reg [31:0] positive00, positive01, positive10, positive11;
always@(posedge clk)
begin
	if(spi_trig) begin
		trigger_count <= trigger_count + 1;
		if((trigger_count % 10) == 9) begin
			if({addr00,addr01,addr10,addr11} !== {4{10'h018}})
				$fatal(1, "synthetic transaction did not target register 18h");
			if(trigger_count == 9) begin
				positive00 <= data00;
				positive01 <= data01;
				positive10 <= data10;
				positive11 <= data11;
				if(data00 !== 32'h008C0003)
					$fatal(1, "positive PAT_INV value is wrong: %h", data00);
			end else begin
				if((positive00 ^ data00) !== 32'h0000_0040 ||
				   (positive01 ^ data01) !== 32'h0000_0040 ||
				   (positive10 ^ data10) !== 32'h0000_0040 ||
				   (positive11 ^ data11) !== 32'h0000_0040)
					$fatal(1, "PAT_INV changed bits other than bit 6");
				if(data00 !== 32'h008C0043)
					$fatal(1, "negative PAT_INV value is wrong: %h", data00);
			end
		end
	end
end

task issue_request;
	input request_polarity;
	begin
		polarity <= request_polarity;
		@(posedge clk);
		cfg_req <= 1'b1;
		@(posedge clk);
		cfg_req <= 1'b0;
		wait(cfg_done);
		@(posedge clk);
	end
endtask

initial begin
	for(i = 0; i < 16384; i = i + 1)
		memory[i] = 32'd0;
	base = 5 * 128;
	for(i = 0; i < 9; i = i + 1) begin
		memory[base+i*8+0] = 32'h0000_0100 + i;
		memory[base+i*8+1] = 32'h1000_0000 + i;
		memory[base+i*8+2] = 32'h0000_0200 + i;
		memory[base+i*8+3] = 32'h2000_0000 + i;
		memory[base+i*8+4] = 32'h0000_0300 + i;
		memory[base+i*8+5] = 32'h3000_0000 + i;
		memory[base+i*8+6] = 32'h0000_0400 + i;
		memory[base+i*8+7] = 32'h4000_0000 + i;
	end
	repeat(5) @(posedge clk);
	reset <= 1'b0;
	issue_request(1'b0);
	issue_request(1'b1);
	if(trigger_count != 20)
		$fatal(1, "expected 20 SPI writes, got %0d", trigger_count);
	$display("PASS: 9 BRAM groups plus PAT_INV, positive/negative differ only by 0x40");
	$finish;
end

initial begin
	#200000;
	$fatal(1, "timeout");
end
endmodule
