`timescale 1ns/1ps

module tb_filter_iq_gain;
reg clk = 1'b0;
reg reset = 1'b1;
reg [3:0] gain_shift = 4'd0;
wire [15:0] filter_i;
wire [15:0] filter_q;

always #5 clk = ~clk;

filter_iq dut (
    .clk(clk), .reset(reset), .rtc_rxen(1'b0), .gain_shift(gain_shift),
    .filter_coef_rden(), .filter_coef_rdaddr(), .filter_coef_rddata(16'd0),
    .demod_headen(1'b0), .demod_head(16'd0), .demod_start(1'b0),
    .demod_last(1'b0), .demod_dataen(1'b0), .demod_i(25'd0), .demod_q(25'd0),
    .filter_headen(), .filter_head(), .filter_start(), .filter_last(),
    .filter_dataen(), .filter_i(filter_i), .filter_q(filter_q)
);

task wait_gain;
    input [3:0] expected;
    integer timeout;
    begin
        timeout = 0;
        while(dut.gain_shift_q !== expected && timeout < 12) begin
            @(posedge clk);
            timeout = timeout + 1;
        end
        if(dut.gain_shift_q !== expected)
            $fatal(1, "gain CDC did not settle to %0d", expected);
    end
endtask

task check_sum;
    input signed [42:0] sample;
    input signed [15:0] expected;
    begin
        force dut.sum_i = sample;
        force dut.sum_q = sample;
        repeat(3) @(posedge clk);
        #1;
        if($signed(filter_i) !== expected || $signed(filter_q) !== expected)
            $fatal(1, "gain/saturation mismatch sample=%0d I=%0d Q=%0d expected=%0d",
                   sample, $signed(filter_i), $signed(filter_q), expected);
        release dut.sum_i;
        release dut.sum_q;
    end
endtask

initial begin
    repeat(3) @(posedge clk);
    reset <= 1'b0;
    wait_gain(4'd0);
    check_sum(43'sd52428800, 16'sd100);       // 100 << 19, unity
    check_sum(-43'sd52428800, -16'sd100);

    gain_shift <= 4'd2;
    wait_gain(4'd2);
    check_sum(43'sd52428800, 16'sd400);
    check_sum(43'sd20971520000, 16'sh7FFF);   // +40000 at unity, then x4
    check_sum(-43'sd20971520000, 16'sh8000);

    reset <= 1'b1;
    @(posedge clk);
    #1;
    if(dut.gain_shift_q !== 4'd0)
        $fatal(1, "gain synchronizer did not reset to zero");
    $display("PASS: gain CDC, unity behavior and signed saturation");
    $finish;
end

initial begin
    #10000;
    $fatal(1, "timeout");
end
endmodule
