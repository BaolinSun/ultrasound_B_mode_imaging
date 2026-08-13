`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company:
// Engineer:
//
// Create Date: 2025/12/22 21:45:08
// Design Name:
// Module Name: afe5832_adc_14b_16lane
// Project Name:
// Target Devices:
// Tool Versions:
// Description:
//// Dependencies:
//
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
//
//////////////////////////////////////////////////////////////////////////////////


module afe5832_adc_14b_16lane #(parameter integer IDELAY_TAPS   = 0              // 0..31 (�? tap 步进依赖器件/模式)
) (
    input                   ref_200M_clk     ,
    input  wire             rst_n            ,
    // LVDS clock from AFE
    input  wire             dclk_p           ,
    input  wire             dclk_n           ,
    // LVDS frame clock
    input  wire             fclk_p           ,
    input  wire             fclk_n           ,
    // 16 LVDS data pairs
    input  wire     [ 15:0] data_p           ,
    input  wire     [ 15:0] data_n           ,
    /**********输出数据 axi-stream接口*********************/
    output wire     [511:0] m_axis_adc_tdata ,
    output wire             m_axis_adc_tvalid,
    input  wire             m_axis_adc_tready,
    output wire             m_axis_adc_tclk  ,
    /*****************************************************/
    output      reg         out_data_locked  ,
    output wire     [ 13:0] dbg_fclk_data
);


    wire rst_200MCLK;
    wire rst_clkdiv ;

    wire rst = ~rst_n;


    reg         out_data_valid;
    reg [511:0] out_data      ;


    assign m_axis_adc_tdata  = out_data;
    assign m_axis_adc_tvalid = out_data_valid;



    // -------------------------
    // 1) DCLK buffering and clocking
    // -------------------------
    wire dclk_se;

    IBUFDS #(
        .DIFF_TERM   ("TRUE" ),
        .IBUF_LOW_PWR("FALSE"),
        .IOSTANDARD  ("LVDS" )
    ) u_ibufds_dclk (
        .I (dclk_p ),
        .IB(dclk_n ),
        .O (dclk_se)
    );

    // High-speed clock for ISERDES
    wire clkfb;

    wire dclk_mmcm  ;
    wire clkdiv_mmcm;

    wire dclk  ;
    wire clkdiv;

    // BUFIO u_bufio (
    //     .I(dclk_se),
    //     .O(dclk  )
    // );
    // wire dclk_b = ~dclk;

    // // CLKDIV = DCLK/7 for 14-bit DDR (14 bits per 7 DCLK cycles)
    // BUFR #(
    //     .BUFR_DIVIDE("2"      ),   // Values: "BYPASS, 1, 2, 3, 4, 5, 6, 7, 8"
    //     .SIM_DEVICE ("7SERIES")    // Must be set to "7SERIES" )
    // ) u_bufr (
    //     .I  (out_data_clk),
    //     .O  (m_axis_adc_tclk ),
    //     .CE (1'b1   ),
    //     .CLR(1'b0   )
    // );

    //�? MMCM 产生 clkdiv_mmcm = dclk /

    wire mmcm_out_data_locked;
    wire mmcm_rst            ;


    wire clkdiv2_mmcm;

// 可直接用外部复位或上电复�?
    assign mmcm_rst = ~rst_n;

// dclk=350MHz, clkdiv=50MHz
// 你可以用 Clocking Wizard 自动算好这些数，然后替换下面的�??
    MMCME2_BASE #(
        .BANDWIDTH         ("OPTIMIZED"),
        .CLKFBOUT_MULT_F   (3          ),   // VCO = 350 * 3/1 = 1050 MHz
        .CLKFBOUT_PHASE    (0.0        ),
        .CLKIN1_PERIOD     (2.857      ),   // dclk 周期(ns)�?350MHz �? 2.857ns
        .DIVCLK_DIVIDE     (1          ),
        .CLKOUT0_DIVIDE_F  (3          ),   // 1050 / 3 = 350 MHz = dclk
        .CLKOUT0_PHASE     (0.0        ),
        .CLKOUT0_DUTY_CYCLE(0.5        ),
        // 其他输出不用就随便填
        .CLKOUT1_DIVIDE    (21         ),   // 1050 / 21 = 50 MHz = dclk/7
        .CLKOUT1_PHASE     (0.0        ),
        .CLKOUT1_DUTY_CYCLE(0.5        ),
        .CLKOUT2_DIVIDE    (42         ),
        .CLKOUT2_PHASE     (0.0        ),
        .CLKOUT2_DUTY_CYCLE(0.5        ),
        .CLKOUT3_DIVIDE    (21         ),
        .CLKOUT3_PHASE     (0.0        ),
        .CLKOUT3_DUTY_CYCLE(0.5        ),
        .CLKOUT4_DIVIDE    (21         ),
        .CLKOUT4_PHASE     (0.0        ),
        .CLKOUT4_DUTY_CYCLE(0.5        ),
        .CLKOUT5_DIVIDE    (21         ),
        .CLKOUT5_PHASE     (0.0        ),
        .CLKOUT5_DUTY_CYCLE(0.5        ),
        .CLKOUT6_DIVIDE    (21         ),
        .CLKOUT6_PHASE     (0.0        ),
        .CLKOUT6_DUTY_CYCLE(0.5        ),
        .REF_JITTER1       (0.01       ),
        .STARTUP_WAIT      ("FALSE"    )
    ) u_mmcm (
        .CLKIN1  (dclk_se             ),   // 直接�? dclk_se 做输�?
        .CLKFBIN (clkfb               ),
        .CLKFBOUT(clkfb               ),
        .CLKOUT0 (dclk_mmcm           ),
        .CLKOUT0B(                    ),
        .CLKOUT1 (clkdiv_mmcm         ),
        .CLKOUT1B(                    ),
        .CLKOUT2 (clkdiv2_mmcm        ),
        .CLKOUT2B(                    ),
        .CLKOUT3 (                    ),
        .CLKOUT3B(                    ),
        .CLKOUT4 (                    ),
        .CLKOUT5 (                    ),
        .CLKOUT6 (                    ),
        .LOCKED  (mmcm_out_data_locked),
        .PWRDWN  (1'b0                ),
        .RST     (mmcm_rst            )
    );

    BUFG dclk_BUFG_inst (
        .O(dclk     ),   // 1-bit output: Clock output
        .I(dclk_mmcm)    // 1-bit input: Clock input
    );

    BUFG dclk_div_BUFG_inst (
        .O(clkdiv     ),   // 1-bit output: Clock output
        .I(clkdiv_mmcm)    // 1-bit input: Clock input
    );

    BUFG dclk_div2_BUFG_inst (
        .O(out_data_clk),   // 1-bit output: Clock output
        .I(clkdiv2_mmcm)    // 1-bit input: Clock input
    );



    // -------------------------
    // assign out_data_clk = clkdiv;

    assign m_axis_adc_tclk = out_data_clk;
    // 2) IBUFDS for FCLK and DATA lanes
    // -------------------------
    wire fclk_se;
    IBUFDS #(
        .DIFF_TERM   ("TRUE" ),
        .IBUF_LOW_PWR("FALSE"),
        .IOSTANDARD  ("LVDS" )
    ) u_ibufds_fclk (
        .I (fclk_p ),
        .IB(fclk_n ),
        .O (fclk_se)
    );

    wire [15:0] data_se;
    genvar i;
    generate
        for (i=0; i<16; i=i+1) begin : G_IBUFDS_DATA
            IBUFDS #(
                .DIFF_TERM("TRUE"),
                .IBUF_LOW_PWR("FALSE"),
                .IOSTANDARD("LVDS")
            ) u_ibufds_data (
                .I(data_p[i]),
                .IB(data_n[i]),
                .O(data_se[i])
            );
        end
    endgenerate

//  复位信号
    (* ASYNC_REG = "TRUE" *) reg [2:0] rst_200MCLK_reg;
    always @(posedge ref_200M_clk or negedge rst_n) begin
        if(~rst_n) begin
            rst_200MCLK_reg <= 3'b000;
        end else begin
            rst_200MCLK_reg <= {rst_200MCLK_reg[1:0],rst_n};
        end
    end
    assign rst_200MCLK = rst_200MCLK_reg[2];


    (* ASYNC_REG = "TRUE" *) reg [2:0] rst_clkdiv_reg;
    always @(posedge clkdiv or negedge rst_n) begin
        if(~rst_n) begin
            rst_clkdiv_reg <= 3'b111;
        end else begin
            rst_clkdiv_reg <= {rst_clkdiv_reg[1:0],~rst_n};
        end
    end
    assign rst_clkdiv = rst_clkdiv_reg[2];



    // -------------------------
    // ) IDELAYCTRL (required for IDELAYE2)
    // -------------------------
    reg [7:0] idelay_rst_cnt;
    reg       idelayctrl_rst;

    always @(posedge ref_200M_clk) begin
        if (~rst_200MCLK) begin
            idelay_rst_cnt <= 8'd0;
            idelayctrl_rst <= 1'b1;
        end else begin
            // hold reset for some cycles after global reset deassert
            if (idelay_rst_cnt != 8'h5F) begin
                idelay_rst_cnt <= idelay_rst_cnt + 1'b1;
                idelayctrl_rst <= 1'b1;
            end else begin
                idelayctrl_rst <= 1'b0;
            end
        end
    end





    (* IODELAY_GROUP                                                                           = "IODELAY_GROUP " *)
    IDELAYCTRL IDELAYCTRL_inst (
        .RDY   (              ),   // 1-bit output: Ready output
        .REFCLK(ref_200M_clk  ),   // 1-bit input: Reference clock input
        .RST   (idelayctrl_rst)    // 1-bit input: Active high reset input
    );


    // -------------------------
    // 3) ISERDES14 for FCLK and data lanes (share bitslip)
    // -------------------------
    wire [13:0] fclk14;

    wire [13:0] data14[15:0];


    assign dbg_fclk_data = fclk14;

    reg bitslip_pulse;

    iserdese14_ddr #(.IDELAY_TAPS(IDELAY_TAPS)) u_fclk14 (
        .ioclk  (dclk         ),
        .ioclk_b(~dclk        ),
        .clkdiv (clkdiv       ),
        .rst    (rst_clkdiv   ),
        .bitslip(bitslip_pulse),
        .din    (fclk_se      ),
        .q14    (fclk14       )
    );

    generate
        for (i=0; i<16; i=i+1) begin : G_LANES
            iserdese14_ddr #(
                .IDELAY_TAPS(IDELAY_TAPS)
            ) u_data14 (
                .ioclk  (dclk        ),
                .ioclk_b(~dclk      ),
                .clkdiv (clkdiv       ),
                .rst    (rst_clkdiv    ),
                .bitslip(bitslip_pulse),
                .din    (data_se[i]   ),
                .q14    (data14[i]    )
            );
        end
    endgenerate

// -------------------------
    // 5) Alignment check: FCLK all-1 or all-0
    // -------------------------
    wire fclk_all_ones = &fclk14                      ;
    wire fclk_all_zero = ~(|fclk14)                   ;
    wire word_aligned  = fclk_all_ones | fclk_all_zero;

    // -------------------------
    // 6) BITSLIP FSM in CLKDIV domain
    // -------------------------
    localparam S_SEARCH = 2'b00;
    localparam S_PULSE  = 2'b01;
    localparam S_WAIT   = 2'b11;
    localparam S_LOCK   = 2'b10;
    reg [1:0]  st              ;

    reg                [3:0] slip_cnt    ;
    reg                [1:0] wait_cnt    ;
    reg                [2:0] good_cnt    ;
    localparam integer       GOOD_N   = 4;

    // 半帧缓存�?16 lane * 16bit
    reg [15:0] even_buf[15:0]; // 存偶数�?�道(0,2,...,30)对应�? 16 lane 数据
    reg [15:0] odd_buf [15:0]; // 存奇数�?�道(1,3,...,31)对应�? 16 lane 数据

// reg have_even;
// reg have_odd;


    always @(posedge clkdiv) begin
        if (rst_clkdiv) begin
            st              <= S_SEARCH;
            bitslip_pulse   <= 1'b0;
            slip_cnt        <= 4'd0;
            wait_cnt        <= 2'd0;
            good_cnt        <= 3'd0;
            out_data_locked <= 1'b0;
        end else begin
            bitslip_pulse <= 1'b0;
            case (st)
                S_SEARCH : begin
                    if (word_aligned) begin
                        good_cnt <= good_cnt + 1'b1;
                        if (good_cnt == (GOOD_N-1)) begin
                            out_data_locked <= 1'b1;
                            st              <= S_LOCK;
                        end
                    end else begin
                        good_cnt <= 3'd0;
                        st       <= S_PULSE;
                    end
                end

                S_PULSE : begin
                    bitslip_pulse <= 1'b1;       // 1 clkdiv pulse
                    slip_cnt      <= slip_cnt + 1'b1;
                    wait_cnt      <= 2'd3;       // allow pipeline settle
                    st            <= S_WAIT;
                end

                S_WAIT : begin
                    if (wait_cnt != 0) wait_cnt <= wait_cnt - 1'b1;
                    else               st       <= S_SEARCH;
                end

                S_LOCK : begin
                    // Optional auto re-lock
                    if (!word_aligned) begin
                        out_data_locked <= 1'b0;
                        st              <= S_SEARCH;
                        slip_cnt        <= 4'd0;
                        good_cnt        <= 3'd0;
                    end
                end
            endcase
        end
    end

    // -------------------------
    // 7) Output register + channel split (CLKDIV domain)
    // -------------------------

    wire [15:0] data16 [15:0];
    generate
        for (genvar k=0; k<16; k=k+1) begin
            assign data16[k] = {{6{data14[k][13]}}, data14[k][13:4]};   // right aligned, zero-extend on MSBs
        end
    endgenerate

    localparam WAIT_EVEN = 1'b0;
    localparam WAIT_ODD  = 1'b1;
    reg        snap_st         ;

    integer k;
    always @(posedge clkdiv) begin
        if (rst_clkdiv) begin
            out_data_valid <= 1'b0;
            out_data       <= 'd0;
            snap_st        <= WAIT_EVEN;
        end else begin
            out_data_valid <= 1'b0; // 默认不输�?

            // 失锁/不对齐时，强制回到等待偶半帧
            if (!out_data_locked || !word_aligned) begin
                snap_st <= WAIT_EVEN;
            end else begin
                case (snap_st)

                    WAIT_EVEN : begin
                        // 只接受偶半帧（FCLK 高）
                        if (fclk_all_zero) begin
                            for (k=0; k<16; k=k+1) begin
                                even_buf[k] <= data16[k];
                            end
                            snap_st <= WAIT_ODD;
                        end
                        // 若此时来的是奇半帧（FCLK 低），直接忽略（保持 WAIT_EVEN�?
                    end
                    WAIT_ODD : begin
                        if (fclk_all_ones) begin
                            // 接收奇半�?
                            for (k=0; k<16; k=k+1) begin
                                odd_buf[k] <= data16[k];
                            end

                            // 输出完整 32ch 快照（�?�道0在最�?16bit�?
                            for (k=0; k<16; k=k+1) begin
                                out_data[(16*(2*k))+:16]   <= even_buf[k]; // ch 2k
                                out_data[(16*(2*k+1))+:16] <= data16[k];   // ch 2k+1，用本周期刚采到�? odd 数据
                            end

                            out_data_valid <= 1'b1;      // 本周期为完整快照
                            snap_st        <= WAIT_EVEN; // 完成后回到等待偶
                        end else if (fclk_all_zero) begin
                            // 期望奇却又来了偶：认为序列错误，丢弃已缓存的偶，重新从偶�?�?
                            for (k=0; k<16; k=k+1) begin
                                even_buf[k] <= data16[k];
                            end
                            snap_st <= WAIT_ODD;
                        end else begin
                            // FCLK 不全1/�?0（理论上不该出现�? word_aligned 应为0），保守起见重置
                            snap_st <= WAIT_EVEN;
                        end
                    end

                    default : snap_st <= WAIT_EVEN;
                endcase
            end
        end
    end



endmodule
