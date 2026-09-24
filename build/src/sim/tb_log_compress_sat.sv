`timescale 1ns/1ps

module tb_log_compress_sat;
reg clk = 1'b0;
reg rst = 1'b1;
reg [15:0] log1_data = 16'd0;
reg [15:0] log2_data = 16'd0;
wire [15:0] log_data;

always #5 clk = ~clk;

log_compress dut (
    .clk(clk), .rst(rst),
    .log1_coef_rden(), .log1_coef_rdaddr(), .log1_coef_rddata(log1_data),
    .log2_coef_rden(), .log2_coef_rdaddr(), .log2_coef_rddata(log2_data),
    .down_headen(1'b0), .down_head(16'd0), .down_start(1'b0),
    .down_last(1'b0), .down_dataen(1'b0), .down_data(16'd0),
    .log_headen(), .log_head(), .log_start(), .log_last(),
    .log_dataen(), .log_data(log_data)
);

initial begin
    repeat(2) @(posedge clk);
    rst <= 1'b0;

    log1_data <= 16'h1234;
    log2_data <= 16'h0102;
    #1;
    if(log_data !== 16'h1336)
        $fatal(1, "non-overflow sum mismatch: %h", log_data);

    log1_data <= 16'hC0A9;
    log2_data <= 16'h7398;
    #1;
    if(log_data !== 16'hFFFF)
        $fatal(1, "overflow did not saturate: %h", log_data);

    $display("PASS: log compression addition saturates instead of wrapping");
    $finish;
end
endmodule
