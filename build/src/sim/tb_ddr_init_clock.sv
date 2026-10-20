`timescale 1ns/1ps
// Self-checking diagnosis: INIT must not fake completion with a stopped source
// clock; the retained CDC request must complete after that clock returns.
module tb_ddr_init_clock;
    reg clk_axi=0,clk_in=0,rst_n=0,clock_running=0;
    always #5 clk_axi=~clk_axi;
    always #20 if(clock_running) clk_in=~clk_in; else clk_in=0;
    reg [15:0] in_data=0;
    reg in_valid=0,in_header=0,in_last=0,in_busy=0;
    reg [23:0] ctrl_addr=0;
    reg [31:0] ctrl_wdata=0;
    reg ctrl_en=0;
    reg [3:0] ctrl_we=0;
    wire [31:0] ctrl_rdata,awaddr;
    wire [7:0] awlen;
    wire [63:0] wdata;
    wire awvalid,wvalid,wlast,bready,frame_done;
    reg awready=1,wready=1,bvalid=0;
    reg [1:0] bresp=0;
    ddr_frame_store #(.FIFO_DEPTH(512)) dut(.*);
    task write_reg(input integer offset,input reg [31:0] value);
        @(negedge clk_axi);
        ctrl_addr=24'h4c0200+offset;ctrl_wdata=value;ctrl_en=1;ctrl_we=15;
        @(negedge clk_axi);ctrl_en=0;ctrl_we=0;
    endtask
    task await_ack(input integer sequence_id);
        for(integer n=0;n<5000;n=n+1) begin
            @(negedge clk_axi);
            if(dut.ack==sequence_id) begin
                if(!dut.initialized || dut.result!=0 || dut.maintenance!=0)
                    $fatal(1,"ACK published without completed initialization");
                return;
            end
        end
        $fatal(1,"source clock resumed but INIT never completed");
    endtask
    task check_wait(input integer old_ack);
        repeat(2000) @(negedge clk_axi);
        if(dut.ack!=old_ack || dut.maintenance!=3 || !dut.paused)
            $fatal(1,"missing source clock not held safely: ack=%d maint=%d",dut.ack,dut.maintenance);
        if(awvalid || wvalid || frame_done)
            $fatal(1,"INIT with stopped clock generated image writes");
    endtask
    task check_read(input integer offset,input reg [31:0] expected);
        @(negedge clk_axi);ctrl_addr=24'h4c0200+offset;
        #1;if(ctrl_rdata!==expected) $fatal(1,"diagnostic read offset=%0d got=%h expected=%h",offset,ctrl_rdata,expected);
    endtask
    integer previous_samples;
    initial begin
        repeat(8) @(negedge clk_axi);rst_n=1;
        check_read(72,32'h55424431);
        // Diagnostic addresses must be read-only, including their magic.
        write_reg(72,32'hffffffff);check_read(72,32'h55424431);
        write_reg(4,1);write_reg(8,1);
        check_wait(0);
        check_read(80,1);check_read(84,1);check_read(88,1);
        if(dut.diag_samples!=0 || dut.diag_age==0) $fatal(1,"absent input clock not reported stale");
        if(dut.initialized) $fatal(1,"cold INIT faked success without source clock");
        clock_running=1;await_ack(1);
        repeat(300) @(negedge clk_axi);
        if(dut.diag_samples<2 || !dut.diag_input[9] || !dut.diag_input[0])
            $fatal(1,"coherent source snapshot missing");
        if(dut.diag_input[1]) $fatal(1,"reserved diagnostic bit not zero");
        @(negedge clk_in);clock_running=0;
        repeat(100) @(negedge clk_axi);previous_samples=dut.diag_samples;
        write_reg(4,1);write_reg(8,2);check_wait(1);
        if(dut.diag_samples!=previous_samples || dut.diag_age<1900)
            $fatal(1,"stopped input clock diagnostic kept changing");
        clock_running=1;await_ack(2);
        repeat(300) @(negedge clk_axi);
        if(dut.diag_samples<=previous_samples) $fatal(1,"source snapshots did not recover");
        $display("PASS ddr INIT: absent/stopped source clock blocks flush ACK, clock restoration completes handshake, no DDR writes");
        $finish;
    end
    initial begin #1000000;$fatal(1,"test watchdog");end
endmodule
