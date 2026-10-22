`timescale 1ns/1ps
module tb_afe_power_hw;
    logic clk_ctrl=0,clk_raw=0,clk_rx=0,rst_n=0,run_clock=1;
    always #5 clk_ctrl=~clk_ctrl;
    always #1.428 if(run_clock) clk_raw=~clk_raw; else clk_raw=0;
    always #10 if(run_clock) clk_rx=~clk_rx; else clk_rx=0;
    logic mmcm_locked=0,idelay_ready=0,rx_locked=0;
    logic [13:0] fclk_word=0;
    logic stuck=0;
    always @(negedge clk_rx) if(!stuck) fclk_word<=~fclk_word;
    logic [23:0] ctrl_addr=0;
    logic [31:0] ctrl_wdata=0,ctrl_rdata;
    logic ctrl_en=0;logic[3:0]ctrl_we=0;
    wire mmcm_reset,rx_reset;
    afe_power_hw dut(.*);
    logic [7:0] previous_gray;
    always @(posedge clk_raw or negedge rst_n) begin
        if(!rst_n) previous_gray<=0;
        else begin
            assert($onehot0(previous_gray^dut.raw_gray)) else $fatal(1,"raw clock Gray transition");
            previous_gray<=dut.raw_gray;
        end
    end
    task automatic write_reg(input [23:0] a,input [31:0] v);
        @(negedge clk_ctrl);ctrl_addr=a;ctrl_wdata=v;ctrl_en=1;ctrl_we=15;
        @(negedge clk_ctrl);ctrl_en=0;ctrl_we=0;
    endtask
    task automatic check(input logic condition,input string msg);
        if(!condition) $fatal(1,"%s",msg);
    endtask
    task automatic check_status(input [31:0] mask,input [31:0] expected);
        @(negedge clk_ctrl);ctrl_addr=24'h4c030c;#1;
        check((ctrl_rdata&mask)==expected,"unexpected status");
    endtask
    initial begin
        #50;rst_n=1;mmcm_locked=1;idelay_ready=1;rx_locked=1;
        #6000;check_status(31,31);
        write_reg(24'h4c0304,3);run_clock=0;
        #60000;check_status(17,0);check(mmcm_reset&&rx_reset,"local reset missing");
        // Keep stale locked pins high deliberately: no raw clock must not pass.
        write_reg(24'h4c0308,1);write_reg(24'h4c0304,2);
        #2000;check_status(31,6);
        run_clock=1;#400;write_reg(24'h4c0304,0);
        #6000;check_status(31,31);
        ctrl_addr=24'h4c0310;#1;check(ctrl_rdata==1,"fresh epoch not acknowledged");
        stuck=1;#200;check_status(24,0);
        stuck=0;#6000;check_status(31,31);
        rx_locked=0;#200;check_status(24,0);rx_locked=1;#6000;
        write_reg(24'h4c0304,3);write_reg(24'h4c0308,2);
        write_reg(24'h4c0304,0);#6000;check_status(31,31);
        ctrl_addr=24'h4c0310;#1;check(ctrl_rdata==2,"second wake epoch");
        // A PL reset with absent AFE clocks must not report an old epoch/lock.
        run_clock=0;rst_n=0;#100;rst_n=1;#1000;check_status(31,6);
        run_clock=1;#6000;check_status(31,31);
        ctrl_addr=24'h4c0310;#1;check(ctrl_rdata==0,"PL reset must invalidate old epoch");
        $display("PASS afe_power_hw stop/restart stale-lock FCLK-stuck epochs");$finish;
    end
    initial begin #500000;$fatal(1,"timeout");end
endmodule
