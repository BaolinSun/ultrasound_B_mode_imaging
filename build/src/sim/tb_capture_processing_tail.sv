`timescale 1ns/1ps
// Real apt_add + Standard FIFO model (from tb_apt_add_pi.sv). Compare old
// immediate disable against held processing enable, using identical RF data.
module tb_capture_processing_tail;
    logic clk=0,reset=1;
    always #20 clk=~clk;
    logic scan_active=0,bf_window=0;
    logic [1:0] phase=0;
    logic headen=0,head_d=0,start=0,last=0,valid=0;
    logic [15:0] head=0;
    logic signed [20:0] sample_data=0;
    wire busy,fault,apt_valid,apt_last,old_valid,old_last;
    wire enable=scan_active|busy;
    wire line_start=headen&&!head_d&&phase==3;
    always_ff @(posedge clk) begin
        if(reset)head_d<=0;else head_d<=headen;
    end
    capture_drain_tracker tracker(.clk_25(clk),.reset(reset),.scan_active(scan_active),
        .bf_window(bf_window),.shot_done(last),.line_start(line_start),.line_done(apt_last),
        .output_valid(apt_valid|apt_last),.busy(busy),.fault(fault));
    wire [31:0] result;
    apt_add held(.clk(clk),.rst(reset),.rtc_enable(enable),.trans_phase(phase),
        .trans_headen(headen),.trans_head(head),.trans_start(start),.trans_last(last),
        .trans_dataen(valid),.trans_data(sample_data),.apt_headen(),.apt_head(),.apt_start(),
        .apt_last(apt_last),.apt_dataen(apt_valid),.apt_data(result));
    apt_add original(.clk(clk),.rst(reset),.rtc_enable(scan_active),.trans_phase(phase),
        .trans_headen(headen),.trans_head(head),.trans_start(start),.trans_last(last),
        .trans_dataen(valid),.trans_data(sample_data),.apt_headen(),.apt_head(),.apt_start(),
        .apt_last(old_last),.apt_dataen(old_valid),.apt_data());
    integer received=0,ends=0,old_received=0,old_ends=0;
    always @(posedge clk) begin
        if(!reset) begin
            if(apt_valid) begin
                if(result!==32'd5)$fatal(1,"PI arithmetic changed at %0d: %h",received,result);
                received<=received+1;
            end
            if(apt_last)ends<=ends+1;
            if(old_valid)old_received<=old_received+1;
            if(old_last)old_ends<=old_ends+1;
            if(!enable && (tracker.shots_pending!=0||tracker.lines_pending!=0))
                $fatal(1,"PI disabled before all accepted work completed");
        end
    end
    initial begin
        repeat(5)@(negedge clk);reset=0;repeat(4)@(negedge clk);scan_active=1;
        for(integer p=0;p<4;p++)begin
            phase=p;bf_window=1;repeat(3)@(negedge clk);
            for(integer h=0;h<8;h++)begin headen=1;head=h;@(negedge clk);end
            headen=0;repeat(4)@(negedge clk);
            for(integer n=0;n<16;n++)begin
                valid=1;start=(n==0);last=(n==15);sample_data=p+1;
                if(p==3 && n==8)begin scan_active=0;bf_window=0;end
                @(negedge clk);
            end
            valid=0;start=0;last=0;bf_window=0;repeat(20)@(negedge clk);
        end
        repeat(20)@(negedge clk);
        if(received!=16||ends!=1||busy||fault||enable)$fatal(1,"tail did not drain correctly: samples=%0d ends=%0d busy=%b fault=%b",received,ends,busy,fault);
        if(old_received>=16||old_ends!=0)$fatal(1,"old PI premature clear was not reproduced");
        $display("PASS capture_processing_tail real PI: original stop truncates output, held enable drains exact samples before disable");
        $finish;
    end
    initial begin #100000;$fatal(1,"PI drain watchdog");end
endmodule
