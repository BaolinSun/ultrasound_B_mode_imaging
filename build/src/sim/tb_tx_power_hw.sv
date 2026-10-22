`timescale 1ns/1ps
module tb_tx_power_hw;
    logic clk_ctrl=0,clk_tx=0,rst_n=0,tx_clock_en=1;
    always #5 clk_ctrl=~clk_ctrl;
    always #2.5 if(tx_clock_en)clk_tx=~clk_tx;
    logic [23:0] addr=0;
    logic [31:0] wdata=0,rdata;
    logic en=0;
    logic [3:0] we=0;
    logic tx_idle=0,ctrl_ready=1,tx_hold,tx_standby;
    tx_power_hw dut(.*);
    task wr(input [5:0] offset,input [31:0] value);
        @(negedge clk_ctrl);addr=24'h4c0340+offset;wdata=value;en=1;we=15;
        @(negedge clk_ctrl);en=0;we=0;
    endtask
    task check_ack(input [31:0] value);
        addr=24'h4c034c;
        repeat(100)begin @(negedge clk_ctrl);if(rdata==value)return;end
        $fatal(1,"ACK timeout expected %0d got %0d",value,rdata);
    endtask
    initial begin
        repeat(6)@(negedge clk_ctrl);rst_n=1;
        wr(4,3);wr(8,1);repeat(30)@(negedge clk_ctrl);
        if(tx_hold||tx_standby)$fatal(1,"standby before transaction drain");
        tx_idle=1;check_ack(1);if(!tx_hold||!tx_standby)$fatal;
        @(negedge clk_ctrl);addr=24'h4c0344;wdata=0;en=1;we=1;
        @(negedge clk_ctrl);en=0;we=0;
        if(dut.control!=3)$fatal(1,"partial control write accepted");
        wr(8,0);if(dut.cmd_seq!=1)$fatal(1,"zero request accepted");
        wr(8,1);repeat(15)@(negedge clk_ctrl);if(dut.req!=1)$fatal(1,"duplicate re-executed");
        // Partial register writes cannot commit; held data cannot change in flight.
        wr(4,1);tx_clock_en=0;wr(8,2);wr(4,3);wr(8,99);
        repeat(15)@(negedge clk_ctrl);if(dut.cmd_seq!=2||dut.control!=1)$fatal(1,"busy payload overwritten");
        tx_clock_en=1;check_ack(2);if(!tx_hold||tx_standby)$fatal;
        wr(4,0);wr(8,3);check_ack(3);if(tx_hold||tx_standby)$fatal;
        wr(4,2);wr(8,4);check_ack(4);addr=24'h4c0350;#1;
        if(!(rdata&16)||tx_standby)$fatal(1,"invalid state accepted");
        tx_idle=0;wr(4,3);wr(8,5);repeat(10)@(negedge clk_ctrl);
        if(!dut.busy)$fatal(1,"busy-reset setup");
        rst_n=0;repeat(6)@(negedge clk_ctrl);rst_n=1;tx_idle=1;
        wr(4,0);wr(8,100);check_ack(100);if(tx_hold||tx_standby)$fatal;
        $display("PASS tx_power_hw handshake idle/busy duplicate/reset clock-stop");$finish;
    end
    initial begin #100000;$fatal(1,"watchdog");end
    // No analog model: this asserts digital safety and transaction ordering only.
    always @(posedge clk_tx) if(rst_n && tx_standby && !tx_hold)$fatal(1,"sleep without inhibit");
endmodule
