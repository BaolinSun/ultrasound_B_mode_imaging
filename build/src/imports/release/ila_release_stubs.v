// Unused TX/timing debug cores remain empty to avoid unrelated resource changes.
// ila_bf is a REAL IP core; never define an ila_bf stub in this file.

module ila_tx (
    input wire        clk,
    input wire        probe0,
    input wire [9:0]  probe1,
    input wire [2:0]  probe2,
    input wire [31:0] probe3,
    input wire [31:0] probe4,
    input wire [31:0] probe5,
    input wire [2:0]  probe6,
    input wire [2:0]  probe7,
    input wire [2:0]  probe8,
    input wire [31:0] probe9
);
endmodule

module ila_timing (
    input wire        clk,
    input wire        probe0,
    input wire [1:0]  probe1,
    input wire [31:0] probe2,
    input wire [31:0] probe3,
    input wire [6:0]  probe4
);
endmodule
