`timescale 1ns/1ps
`include "fpga_define.vh"

module tb_tx_readback_cdc;
reg clk_para = 1'b0;
reg clk_200 = 1'b0;
reg reset = 1'b1;
reg para_wren = 1'b0;
reg [19:0] para_addr = 20'd0;
reg [31:0] para_wrdata = 32'd0;
wire req_toggle;
wire [31:0] req_addr;
wire [31:0] result_data;
wire done_toggle;
reg accept_seen = 1'b0;

always #5.5 clk_para = ~clk_para;
always #2.5 clk_200 = ~clk_200;
always @(posedge clk_200)
    if (tx_dut.tx_spi_rdaccept)
        accept_seen <= 1'b1;

para_reg para_dut (
    .clk(clk_para), .reset(reset),
    .para_reg_wren(para_wren), .para_reg_rden(1'b0),
    .para_reg_addr(para_addr), .para_reg_wrdata(para_wrdata),
    .para_reg_rddata(), .para_reg_rdvalid(),
    .tx_spi_rdreq(req_toggle), .tx_spi_rdaddr(req_addr),
    .tx_spi_rxdata(result_data), .tx_spi_rxvalid(done_toggle),
    .tx_cfg_rddata_debug(), .scan_enable(), .rtc_enable(),
    .spi_start(), .tx_hw_reset()
);

tx_top tx_dut (
    .power_hold(1'b0), .power_standby(1'b0), .power_idle(),
    .clk(clk_200), .reset(reset), .scan_enable(1'b0),
    .tx_cfg_req(1'b0), .rtc_line(7'd0), .rtc_polarity(1'b0),
    .tx_cfg_ready(), .tx_cfg_done(), .spi_start(1'b0),
    .tx_st_reg_num(7'd0), .tx_st_reg_rden(), .tx_st_reg_rdaddr(),
    .tx_st_reg_rddata(32'd0), .tx_rt_reg_num(5'd0),
    .tx_rt_reg_rden(), .tx_rt_reg_rdaddr(), .tx_rt_reg_rddata(32'd0),
    .tx_spi_rdreq(req_toggle), .tx_spi_rdaddr(req_addr[9:0]),
    .tx_spi_rxdata(result_data), .tx_spi_rxvalid(done_toggle),
    .tx_cfg_rddata_debug(32'd0), .tx_cfg_reset(1'b0),
    .tx_dsel0(), .tx_dsel1(), .tx_reset(), .tr_en(), .tx_stdby(),
    .tx_cw_en(), .ctrl_spi_rdy(), .tx_sclk(), .tx_sen0(), .tx_sen1(),
    .tx_dat0(), .tx_dat1(), .tx_sdout(2'b00)
);

task issue_read;
    input [9:0] address;
    begin
        @(posedge clk_para);
        para_addr <= `ADDR_TX_CFG_RD_REQ;
        para_wrdata <= address;
        para_wren <= 1'b1;
        @(posedge clk_para);
        para_wren <= 1'b0;
    end
endtask

task return_die_data;
    input [31:0] die1;
    input [31:0] die2;
    begin
        repeat(3) @(posedge clk_200);
        force tx_dut.tx_spi_rxdata0 = die1;
        force tx_dut.tx_spi_rxvalid0 = 1'b1;
        @(posedge clk_200);
        release tx_dut.tx_spi_rxvalid0;
        release tx_dut.tx_spi_rxdata0;
        repeat(2) @(posedge clk_200);
        force tx_dut.tx_spi_rxdata1 = die2;
        force tx_dut.tx_spi_rxvalid1 = 1'b1;
        @(posedge clk_200);
        release tx_dut.tx_spi_rxvalid1;
        release tx_dut.tx_spi_rxdata1;
    end
endtask

reg first_toggle;
initial begin
    repeat(5) @(posedge clk_200);
    reset <= 1'b0;
    issue_read(10'h018);
    wait(para_dut.tx_spi_req_busy === 1'b1);
    first_toggle = req_toggle;

    // A second request while busy must not change the source toggle/address.
    issue_read(10'h120);
    repeat(3) @(posedge clk_para);
    if(req_toggle !== first_toggle || req_addr[9:0] !== 10'h018)
        $fatal(1, "outstanding read was overwritten");
    if(accept_seen !== 1'b1)
        $fatal(1, "request toggle was not accepted in the 200 MHz domain");

    return_die_data(32'h00F0_00F0, 32'h0F00_0F00);
    wait(para_dut.tx_cfg_rddata == 32'h0FF0_0FF0);
    if(para_dut.tx_spi_req_busy !== 1'b0)
        $fatal(1, "completion toggle did not release source busy state");

    $display("PASS: asynchronous request/completion toggles preserve bundled address/data");
    $finish;
end

initial begin
    #200000;
    $fatal(1, "timeout");
end
endmodule
