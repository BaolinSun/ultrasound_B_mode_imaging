`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company:
// Engineer:
//
// Create Date: 2025/12/22 22:06:23
// Design Name:
// Module Name: iserdese14_ddr
// Project Name:
// Target Devices:
// Tool Versions:
// Description:
//
// Dependencies:
//
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
//
//////////////////////////////////////////////////////////////////////////////////


module iserdese14_ddr #(
    parameter integer IDELAY_TAPS   = 0              // 0..31 (每 tap 步进依赖器件/模式)
) (
    input  wire        ioclk  , // BUFIO output
    input  wire        ioclk_b, // inverted ioclk
    input  wire        clkdiv , // DCLK/7 (BUFR)
    input  wire        rst    , // synchronous to clkdiv is recommended
    input  wire        bitslip, // synchronous to clkdiv (one-cycle pulse)
    input  wire        din    , // single-ended after IBUFDS (or IDELAY->DDLY if used)
    output wire [13:0] q14
);
    // Master outputs Q1..Q8
    wire m_q1,m_q2,m_q3,m_q4,m_q5,m_q6,m_q7,m_q8;
    // Slave outputs use Q3..Q8 for width expansion
    wire s_q3,s_q4,s_q5,s_q6,s_q7,s_q8;

    wire sh1, sh2;

    wire din_delayed;
    (* IODELAY_GROUP                                                                           = "IODELAY_GROUP " *)
    IDELAYE2 #(
        .CINVCTRL_SEL         ("FALSE"    ),   // Enable dynamic clock inversion (FALSE, TRUE)
        .DELAY_SRC            ("IDATAIN"  ),   // Delay input (IDATAIN, DATAIN)
        .HIGH_PERFORMANCE_MODE("TRUE"     ),   // Reduced jitter ("TRUE"), Reduced power ("FALSE")
        .IDELAY_TYPE          ("FIXED"    ),   // FIXED, VARIABLE, VAR_LOAD, VAR_LOAD_PIPE
        .IDELAY_VALUE         (IDELAY_TAPS),   // Input delay tap setting (0-31)
        .PIPE_SEL             ("FALSE"    ),   // Select pipelined mode, FALSE, TRUE
        .REFCLK_FREQUENCY     (200.0      ),   // IDELAYCTRL clock input frequency in MHz (190.0-210.0, 290.0-310.0).
        .SIGNAL_PATTERN       ("DATA"     )    // DATA, CLOCK input signal
    ) u_idelay (
        //
        .CNTVALUEOUT(           ),   // 5-bit output: Counter value output
        //
        .DATAOUT    (din_delayed),   // 1-bit output: Delayed data output,
        //
        .C          (clkdiv     ),   // 1-bit input: Clock input
        .CE         (1'b0       ),   // 1-bit input: Active high enable increment/decrement input
        .CINVCTRL   (1'b0       ),   // 1-bit input: Dynamic clock inversion input
        //
        .CNTVALUEIN (5'd0          ),   // 5-bit input: Counter value input
        //
        .DATAIN     (1'b0       ),   // 1-bit input: Internal delay data input
        .IDATAIN    (din        ),   // 1-bit input: Data input from the I/O
        .INC        (1'b0       ),   // 1-bit input: Increment / Decrement tap delay input
        .LD         (1'b0       ),   // 1-bit input: Load IDELAY_VALUE input
        .LDPIPEEN   (1'b0       ),   // 1-bit input: Enable PIPELINE register to load data input
        .REGRST     (1'b0       )    // 1-bit input: Active-high reset tap-delay input
    );

    // MASTER: must be the _P side of the differential input pair in placement (XDC LOC matters)
    ISERDESE2 #(
        .DATA_RATE        ("DDR"       ),   // DDR, SDR
        .DATA_WIDTH       (14          ),   // Parallel data width (2-8,10,14)
        .DYN_CLKDIV_INV_EN("FALSE"     ),   // Enable DYNCLKDIVINVSEL inversion (FALSE, TRUE)
        .DYN_CLK_INV_EN   ("FALSE"     ),   // Enable DYNCLKINVSEL inversion (FALSE, TRUE)
        // INIT_Q1 - INIT_Q4: Initial value on the Q outputs (0/1)
        .INIT_Q1          (1'b0        ),
        .INIT_Q2          (1'b0        ),
        .INIT_Q3          (1'b0        ),
        .INIT_Q4          (1'b0        ),
        .INTERFACE_TYPE   ("NETWORKING"),   // MEMORY, MEMORY_DDR3, MEMORY_QDR, NETWORKING, OVERSAMPLE
        .IOBDELAY         ("IFD"      ),   // NONE, BOTH, IBUF, IFD
        .NUM_CE           (1           ),   // Number of clock enables (1,2)
        .OFB_USED         ("FALSE"     ),   // Select OFB path (FALSE, TRUE)
        .SERDES_MODE      ("MASTER"    ),   // MASTER, SLAVE
        // SRVAL_Q1 - SRVAL_Q4: Q output values when SR is used (0/1)
        .SRVAL_Q1         (1'b0        ),
        .SRVAL_Q2         (1'b0        ),
        .SRVAL_Q3         (1'b0        ),
        .SRVAL_Q4         (1'b0        )
    ) u_m (
        .O           (           ),
        .Q1          (m_q1       ),
        .Q2          (m_q2       ),
        .Q3          (m_q3       ),
        .Q4          (m_q4       ),
        .Q5          (m_q5       ),
        .Q6          (m_q6       ),
        .Q7          (m_q7       ),
        .Q8          (m_q8       ),
        .SHIFTOUT1   (sh1        ),
        .SHIFTOUT2   (sh2        ),
        .BITSLIP     (bitslip    ),
        .CE1         (1'b1       ),
        .CE2         (        ),
        .CLKDIVP     (1'b0       ),
        .CLK         (ioclk      ),
        .CLKB        (ioclk_b    ),
        .CLKDIV      (clkdiv     ),
        .OCLK        (1'b0       ),   // NETWORKING: tie off
        .DYNCLKDIVSEL(1'b0       ),
        .DYNCLKSEL   (1'b0       ),
        .D           (1'b0       ),
        .DDLY        (din_delayed),
        .OFB         (           ),
        .OCLKB       (1'b0       ),
        .RST         (rst        ),
        .SHIFTIN1    (1'b0       ),
        .SHIFTIN2    (1'b0       )
    );

    ISERDESE2 #(
        .DATA_RATE     ("DDR"       ),
        .DATA_WIDTH    (14          ),
        .INTERFACE_TYPE("NETWORKING"),
        .SERDES_MODE   ("SLAVE"     ),
        .NUM_CE        (1           ),
        .IOBDELAY      ("NONE"      )
    ) u_s (
        .O           (       ),
        .Q1          (       ),
        .Q2          (       ),
        .Q3          (s_q3   ),
        .Q4          (s_q4   ),
        .Q5          (s_q5   ),
        .Q6          (s_q6   ),
        .Q7          (s_q7   ),
        .Q8          (s_q8   ),
        .SHIFTOUT1   (    ),
        .SHIFTOUT2   (    ),
        .BITSLIP     (bitslip),
        .CE1         (1'b1   ),
        .CE2         (1'b0   ),
        .CLKDIVP     (1'b0   ),
        .CLK         (ioclk  ),
        .CLKB        (ioclk_b),
        .CLKDIV      (clkdiv ),
        .OCLK        (1'b0   ),   // NETWORKING: tie off
        .DYNCLKDIVSEL(1'b0   ),
        .DYNCLKSEL   (1'b0   ),
        .D           (1'b0   ),
        .DDLY        (1'b0   ),
        .OFB         (       ),
        .OCLKB       (1'b0   ),
        .RST         (rst    ),
        .SHIFTIN1    (sh1   ),
        .SHIFTIN2    (sh2   )
    );

    // Bit ordering note:
    // Xilinx notes "first data bit received appears on the highest order Q output" in general. :contentReference[oaicite:5]{index=5}
    // For bring-up, treat q14 as raw and verify MSB/LSB with AFE test pattern.
    //
    // Here is a common concatenation (master 8 + slave 6).
    assign q14 = { s_q8, s_q7, s_q6, s_q5, s_q4, s_q3,  m_q8, m_q7, m_q6, m_q5, m_q4, m_q3, m_q2, m_q1 };

endmodule