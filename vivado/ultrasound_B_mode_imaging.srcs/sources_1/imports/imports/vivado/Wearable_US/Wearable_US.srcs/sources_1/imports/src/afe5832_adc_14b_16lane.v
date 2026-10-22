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


module afe5832_adc_14b_16lane #(parameter integer IDELAY_TAPS   = 0              // 0..31 (姣? tap 姝ヨ繘渚濊禆鍣ㄤ欢/妯″紡)
) (
    input                   ref_200M_clk     ,
    input  wire             rst_n            ,
    input wire clk_ctrl,
    input wire [23:0] power_addr,
    input wire [31:0] power_wdata,
    input wire power_en,
    input wire [3:0] power_we,
    output wire [31:0] power_rdata,
    // LVDS clock from AFE
    input  wire             dclk_p           ,
    input  wire             dclk_n           ,
    // LVDS frame clock
    input  wire             fclk_p           ,
    input  wire             fclk_n           ,
    // 16 LVDS data pairs
    input  wire     [ 15:0] data_p           ,
    input  wire     [ 15:0] data_n           ,
    /**********杈撳嚭鏁版嵁 axi-stream鎺ュ彛*********************/
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
    wire out_data_clk;

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

    //鐢? MMCM 浜х敓 clkdiv_mmcm = dclk /

    wire mmcm_out_data_locked;
    wire mmcm_rst            ;


    wire clkdiv2_mmcm;

// 鍙洿鎺ョ敤澶栭儴澶嶄綅鎴栦笂鐢靛浣?
    wire afe_mmcm_reset, afe_rx_reset;
    wire afe_idelay_ready;
    wire afe_rx_rst_n = rst_n && !afe_rx_reset;
    assign mmcm_rst = ~rst_n | afe_mmcm_reset;

// dclk=350MHz, clkdiv=50MHz
// 浣犲彲浠ョ敤 Clocking Wizard 鑷姩绠楀ソ杩欎簺鏁帮紝鐒跺悗鏇挎崲涓嬮潰鐨勫??
    MMCME2_BASE #(
        .BANDWIDTH         ("OPTIMIZED"),
        .CLKFBOUT_MULT_F   (3          ),   // VCO = 350 * 3/1 = 1050 MHz
        .CLKFBOUT_PHASE    (0.0        ),
        .CLKIN1_PERIOD     (2.857      ),   // dclk 鍛ㄦ湡(ns)锛?350MHz 鈮? 2.857ns
        .DIVCLK_DIVIDE     (1          ),
        .CLKOUT0_DIVIDE_F  (3          ),   // 1050 / 3 = 350 MHz = dclk
        .CLKOUT0_PHASE     (0.0        ),
        .CLKOUT0_DUTY_CYCLE(0.5        ),
        // 鍏朵粬杈撳嚭涓嶇敤灏遍殢渚垮～
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
        .CLKIN1  (dclk_se             ),   // 鐩存帴鐢? dclk_se 鍋氳緭鍏?
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

//  澶嶄綅淇″彿
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
    always @(posedge clkdiv or negedge afe_rx_rst_n) begin
        if(~afe_rx_rst_n) begin
            rst_clkdiv_reg <= 3'b111;
        end else begin
            rst_clkdiv_reg <= {rst_clkdiv_reg[1:0],1'b0};
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
        .RDY   (afe_idelay_ready),   // 1-bit output: Ready output
        .REFCLK(ref_200M_clk  ),   // 1-bit input: Reference clock input
        .RST   (idelayctrl_rst)    // 1-bit input: Active high reset input
    );


    // -------------------------
    // 3) ISERDES14 for FCLK and data lanes (share bitslip)
    // -------------------------
    wire [13:0] fclk14;

    wire [13:0] data14[15:0];


    assign dbg_fclk_data = fclk14;

    // Only this receiver is reset. AXI/DDR/PS/JTAG retain their own reset.
    wire afe_raw_clk;
    BUFG afe_raw_buf (.I(dclk_se),.O(afe_raw_clk));
    afe_power_hw afe_power_i (
        .clk_ctrl(clk_ctrl),.rst_n(rst_n),.clk_raw(afe_raw_clk),.clk_rx(clkdiv),
        .mmcm_locked(mmcm_out_data_locked),.idelay_ready(afe_idelay_ready),
        .rx_locked(out_data_locked),.fclk_word(fclk14),
        .ctrl_addr(power_addr),.ctrl_wdata(power_wdata),.ctrl_en(power_en),.ctrl_we(power_we),
        .ctrl_rdata(power_rdata),.mmcm_reset(afe_mmcm_reset),.rx_reset(afe_rx_reset)
    );

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

    // 鍗婂抚缂撳瓨锛?16 lane * 16bit
    reg [15:0] even_buf[15:0]; // 瀛樺伓鏁伴?氶亾(0,2,...,30)瀵瑰簲鐨? 16 lane 鏁版嵁
    reg [15:0] odd_buf [15:0]; // 瀛樺鏁伴?氶亾(1,3,...,31)瀵瑰簲鐨? 16 lane 鏁版嵁

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
            // The AFE serializes 14 bits per frame but the ADC resolution is lower,
            // so only part of the frame carries signal. DIGOUT is MSB-aligned in a
            // 16-bit word with the unused LSBs zero, and the AFE's truncation block
            // keeps the MSBs, so the low bits of data14 are structural zeros.
            // KEEP IN SYNC with AFE5832_init() reg 0x04 [1:0]:
            //   ADC_10_BITS -> data14[13:4]   ADC_12_BITS -> data14[13:2]
            // data14 is two's complement, so this sign-extends (not zero-extends).
            assign data16[k] = {{4{data14[k][13]}}, data14[k][13:2]};   // 12-bit mode
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
            out_data_valid <= 1'b0; // 榛樿涓嶈緭鍑?

            // 澶遍攣/涓嶅榻愭椂锛屽己鍒跺洖鍒扮瓑寰呭伓鍗婂抚
            if (!out_data_locked || !word_aligned) begin
                snap_st <= WAIT_EVEN;
            end else begin
                case (snap_st)

                    WAIT_EVEN : begin
                        // 鍙帴鍙楀伓鍗婂抚锛團CLK 楂橈級
                        if (fclk_all_zero) begin
                            for (k=0; k<16; k=k+1) begin
                                even_buf[k] <= data16[k];
                            end
                            snap_st <= WAIT_ODD;
                        end
                        // 鑻ユ鏃舵潵鐨勬槸濂囧崐甯э紙FCLK 浣庯級锛岀洿鎺ュ拷鐣ワ紙淇濇寔 WAIT_EVEN锛?
                    end
                    WAIT_ODD : begin
                        if (fclk_all_ones) begin
                            // 鎺ユ敹濂囧崐甯?
                            for (k=0; k<16; k=k+1) begin
                                odd_buf[k] <= data16[k];
                            end

                            // 杈撳嚭瀹屾暣 32ch 蹇収锛堥?氶亾0鍦ㄦ渶浣?16bit锛?
                            for (k=0; k<16; k=k+1) begin
                                out_data[(16*(2*k))+:16]   <= even_buf[k]; // ch 2k
                                out_data[(16*(2*k+1))+:16] <= data16[k];   // ch 2k+1锛岀敤鏈懆鏈熷垰閲囧埌鐨? odd 鏁版嵁
                            end

                            out_data_valid <= 1'b1;      // 鏈懆鏈熶负瀹屾暣蹇収
                            snap_st        <= WAIT_EVEN; // 瀹屾垚鍚庡洖鍒扮瓑寰呭伓
                        end else if (fclk_all_zero) begin
                            // 鏈熸湜濂囧嵈鍙堟潵浜嗗伓锛氳涓哄簭鍒楅敊璇紝涓㈠純宸茬紦瀛樼殑鍋讹紝閲嶆柊浠庡伓寮?濮?
                            for (k=0; k<16; k=k+1) begin
                                even_buf[k] <= data16[k];
                            end
                            snap_st <= WAIT_ODD;
                        end else begin
                            // FCLK 涓嶅叏1/鍏?0锛堢悊璁轰笂涓嶈鍑虹幇涓? word_aligned 搴斾负0锛夛紝淇濆畧璧疯閲嶇疆
                            snap_st <= WAIT_EVEN;
                        end
                    end

                    default : snap_st <= WAIT_EVEN;
                endcase
            end
        end
    end



endmodule
