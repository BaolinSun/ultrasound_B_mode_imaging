`timescale 1ns/1ps
module tb_capture_drain_tracker;
    logic clk_25=0,reset=1;
    always #20 clk_25=~clk_25;
    logic scan_active=0,bf_window=0,shot_done=0,line_start=0,line_done=0,output_valid=0;
    wire busy,fault;
    capture_drain_tracker #(.COUNT_WIDTH(3)) dut(.*);
    // Original implementation is kept ONLY in this test to demonstrate the
    // exact stuck-tail failure, even when the data path completed correctly.
    logic old_tail;
    always @(posedge clk_25) begin
        if(reset) old_tail<=0;
        else if(bf_window) old_tail<=1;
        else if(line_done) old_tail<=0;
    end
    task tick(input integer cycles=1);
        repeat(cycles) @(negedge clk_25);
    endtask
    task clear;
        reset=1;scan_active=0;bf_window=0;shot_done=0;line_start=0;line_done=0;output_valid=0;
        tick(3);reset=0;tick(3);
        if(busy||fault) $fatal(1,"reset did not recover the tracker");
    endtask
    task begin_shot(input bit final_phase);
        bf_window=1;line_start=final_phase;tick();line_start=0;tick();
    endtask
    task finish_shot;
        shot_done=1;tick();shot_done=0;tick();
    endtask
    task finish_line;
        line_done=1;output_valid=1;tick();line_done=0;tick(2);output_valid=0;tick(3);
    endtask
    initial begin
        clear();
        // Ignore stale pipeline completion pulses before the first accepted
        // BF transaction after reset; no token may underflow or be invented.
        shot_done=1;line_done=1;tick();shot_done=0;line_done=0;tick(3);
        if(busy||fault)$fatal(1,"startup completion created work");
        // The old logic re-arms on EVERY high BF cycle after log_last, or
        // ignores log_last outright if both signals coincide. New logic must
        // drain for completion before/same-cycle/after the window falling.
        for(integer order=0;order<3;order++) begin
            clear();scan_active=1;begin_shot(1);
            if(order==0) begin finish_shot();finish_line();tick(8);bf_window=0;end
            else if(order==1) begin shot_done=1;line_done=1;tick();bf_window=0;shot_done=0;line_done=0;end
            else begin bf_window=0;scan_active=0;tick(10);if(!busy)$fatal(1,"lost delayed work");finish_shot();finish_line();end
            scan_active=0;tick(5);
            if(busy||fault) $fatal(1,"completion order %0d did not drain",order);
            if(order<2 && !old_tail) $fatal(1,"old stuck-tail regression was not reproduced");
        end
        // Real PI ratio: four physical RF transactions, one logical output.
        for(integer d=0;d<5;d++) begin
            clear();scan_active=1;
            for(integer phase=0;phase<4;phase++) begin
                begin_shot(phase==3);finish_shot();
                if(phase==3)finish_line();
                bf_window=0;tick(2);
                if(!busy||fault)$fatal(1,"scan/PI ratio dropped busy d=%0d phase=%0d",d,phase);
            end
            scan_active=0;tick(4);if(busy||fault)$fatal(1,"PI did not drain");
        end
        // Overlap and simultaneous completion/new start cannot erase tokens.
        clear();scan_active=1;begin_shot(1);bf_window=0;tick();
        bf_window=1;shot_done=1;line_start=1;line_done=1;tick();
        shot_done=0;line_start=0;line_done=0;bf_window=0;scan_active=0;tick(6);
        if(!busy||fault||dut.shots_pending!=1||dut.lines_pending!=1)$fatal(1,"simultaneous accounting");
        // Missing completion must NEVER look idle, no timeout-based clearing.
        tick(100);if(!busy)$fatal(1,"missing completion was discarded");
        finish_shot();if(!busy)$fatal(1,"logical tail disappeared");
        finish_line();if(busy||fault)$fatal(1,"overlap failed to drain");
        output_valid=1;tick(2);if(!busy)$fatal(1,"registered output not covered");
        output_valid=0;tick(3);if(busy)$fatal(1,"output did not drain");
        // Bounded counters fail closed; explicit reset is the recovery path.
        clear();
        for(integer n=0;n<8;n++) begin begin_shot(1);bf_window=0;tick(2);end
        tick(2);if(!fault||!busy)$fatal(1,"overflow must fail closed");
        clear();begin_shot(1);finish_shot();finish_line();bf_window=0;tick(3);
        shot_done=1;line_done=1;tick();shot_done=0;line_done=0;tick(2);
        if(!fault||!busy)$fatal(1,"unmatched completion must fail closed");
        clear();
        $display("PASS capture_drain_tracker old stuck-tail reproduced, all completion orders, PI ratio, overlap, missing completion, output tail, overflow, reset");
        $finish;
    end
    initial begin #1000000;$fatal(1,"capture drain watchdog");end
endmodule
