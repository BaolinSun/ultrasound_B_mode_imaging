// Release-build substitutes for debug-only ILA cores.
// The installed Vivado BASIC license does not permit these ILA instances.
// These modules intentionally contain no logic and do not affect functional paths.

module ila_bf (
    input wire        clk,
    input wire [16:0] probe0,
    input wire [1:0]  probe1,
    input wire [16:0] probe2,
    input wire [31:0] probe3,
    input wire [6:0]  probe4
);
endmodule

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
