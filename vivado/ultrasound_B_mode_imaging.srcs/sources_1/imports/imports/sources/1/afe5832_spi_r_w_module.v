`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company:
// Engineer:
//
// Create Date: 2025/06/08 22:10:40
// Design Name:
// Module Name: afe5832_spi_r_w_module
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


module afe5832_spi_r_w_module (
    input             rstn                         ,
    input             clk                          ,
    // 单独写或者是连续写脉冲信号
    output reg        to_ctrl_m_spi_module_idle    ,
    // 单独写
    input             to_spi_send_transcation_start, // 启动发送信号
    input      [ 7:0] single_write_addr            , // 单次读和写共享
    input      [15:0] single_write_data            ,
    output reg        from_spi_m_write_finish      ,
    // 单独读
    output reg [15:0] from_spi_read_data           ,
    output reg        from_spi_read_data_valid     ,
    input             to_spi_read_transcation_start, // 输出给spi模块
    output            SPI_CS                       ,
    output            SPI_O_CLK                    ,
    output            SPI_MOSI                     ,
    input             SPI_MISO
);
    localparam SPI_IDLE          = 4'b0000;
    localparam WRITE_TRANS_START = 4'b0001;
    localparam READ_TRANS_START  = 4'b0011;
    localparam SPI_WRITE_FINISH  = 4'b0010;
    localparam SPI_READ_FINISH   = 4'b0110;

    localparam WRITE_ENBIT_TRANS_START = 4'b1000;
    localparam RESET_READ_EN_BIT       = 4'b1001;

    // SPI 启动的工作模式
    localparam SPI_WORK_STATE_SINGAL_WRITE = 2'b10;
    localparam SPI_WORK_STATE_SINGAL_READ  = 2'b01;

    // 暂存参数量

    reg [3:0] state       ;
    reg       SPI_CS_reg  ;
    reg       SPI_MOSI_reg;

    reg spi_clk;
    assign SPI_O_CLK = spi_clk;
    assign SPI_CS    = SPI_CS_reg;
    assign SPI_MOSI  = SPI_MOSI_reg;

    wire to_spi_send_transcation_start_pulse;
    reg  to_spi_send_transcation_start_ff1  ;
    reg  to_spi_send_transcation_start_ff2  ;

    wire to_spi_read_transcation_start_pulse;
    reg  to_spi_read_transcation_start_ff1  ;
    reg  to_spi_read_transcation_start_ff2  ;
    // 发送脉冲
    assign to_spi_send_transcation_start_pulse = ~(to_spi_send_transcation_start_ff2) & to_spi_send_transcation_start_ff1;
    // 读取脉冲
    assign to_spi_read_transcation_start_pulse = ~to_spi_read_transcation_start_ff2 & to_spi_read_transcation_start_ff1;

    wire [1:0] spi_work_state;
    assign spi_work_state = {to_spi_send_transcation_start_pulse,to_spi_read_transcation_start_pulse};


/********************************输入打拍，用来检测上升沿**********************************************/
    always@(posedge  clk or negedge rstn) begin
        if (~rstn) begin
            to_spi_send_transcation_start_ff1 <= 'b0;
            to_spi_send_transcation_start_ff2 <= 'b0;
        end else begin
            to_spi_send_transcation_start_ff1 <= to_spi_send_transcation_start;
            to_spi_send_transcation_start_ff2 <= to_spi_send_transcation_start_ff1;
        end
    end
    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            to_spi_read_transcation_start_ff1 <= 0;
            to_spi_read_transcation_start_ff2 <= 0;
        end else begin
            to_spi_read_transcation_start_ff1 <= to_spi_read_transcation_start;
            to_spi_read_transcation_start_ff2 <= to_spi_read_transcation_start_ff1;
        end
    end

/************************************************************************************/

    // 1: r 0:w
    reg r_w_operation;

    reg [3:0] clk_divide;

    // 暂存端口数据

    reg [ 9:0] single_read_addr_temp ;
    reg [ 9:0] single_write_addr_temp;
    reg [15:0] single_write_data_temp;



    reg [15:0] read_data;

    reg [15:0] res_read_data;

    reg [7:0] tool_count;

    reg [3:0] cdiv_cnt;

    reg [7:0] write_status;



    reg trans_ctrl;
    // 状态机
    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            state                     <= 'b0;
            single_write_addr_temp    <= 'd0;
            single_write_data_temp    <= 'd0;
            res_read_data             <= 'd0;
            single_read_addr_temp     <= 'd0;
            to_ctrl_m_spi_module_idle <= 'b1;
            r_w_operation             <= 'b0;
            from_spi_read_data_valid  <= 'b0;
            from_spi_read_data        <= 'b0;
            clk_divide                <= 'b1;
            tool_count                <= 'b0;
            trans_ctrl                <= 'b0;
            from_spi_m_write_finish   <= 'b0;
        end else begin
            case (state)
                SPI_IDLE : begin
                    case(spi_work_state)
                        SPI_WORK_STATE_SINGAL_WRITE : begin
                            r_w_operation          <= 1'b0;
                            // 暂存端口数据
                            single_write_addr_temp <= single_write_addr;
                            single_write_data_temp <= single_write_data;

                            to_ctrl_m_spi_module_idle <= 'b0;

                            state <= WRITE_TRANS_START;
                        end
                        SPI_WORK_STATE_SINGAL_READ : begin
                            r_w_operation             <= 1'b1;
                            from_spi_read_data_valid  <= 1'b0;
                            single_read_addr_temp     <= single_write_addr;
                            to_ctrl_m_spi_module_idle <= 'b0;
                            state                     <= WRITE_ENBIT_TRANS_START;
                        end
                        default : begin
                            // 读数据有效性清0
                            from_spi_read_data_valid  <= 1'b0;
                            r_w_operation             <= 1'b0;
                            state                     <= SPI_IDLE;
                            to_ctrl_m_spi_module_idle <= 'b1;
                        end
                    endcase
                end
                WRITE_TRANS_START : begin
                    if(write_status == 'd51  ) begin
                        if(cdiv_cnt == clk_divide) begin
                            trans_ctrl <= 'b0;
                            state      <= SPI_WRITE_FINISH;
                        end else begin
                            trans_ctrl <= 'b0;
                        end
                    end else begin
                        clk_divide <= 'd10;
                        trans_ctrl <= 'b1;
                        state      <= WRITE_TRANS_START;

                    end
                end
                WRITE_ENBIT_TRANS_START : begin
                    if(write_status == 'd51 ) begin
                        if (cdiv_cnt == clk_divide) begin
                            trans_ctrl <= 'b0;
                            state      <= READ_TRANS_START;
                        end else begin
                            trans_ctrl <= 'b0;
                        end
                    end else begin
                        single_write_addr_temp <= 8'b00000000;
                        single_write_data_temp <= 16'h0002;
                        trans_ctrl             <= 'b1;
                        clk_divide             <= 'd10;
                        state                  <= WRITE_ENBIT_TRANS_START;
                    end

                end
                READ_TRANS_START : begin
                    if(write_status == 'd51) begin
                        if(cdiv_cnt == clk_divide) begin
                            trans_ctrl    <= 'b0;
                            state         <= RESET_READ_EN_BIT;
                            res_read_data <= read_data;
                        end else begin
                            trans_ctrl    <= 'b0;
                            res_read_data <= read_data;
                        end

                    end else begin
                        single_write_addr_temp <= single_read_addr_temp;
                        single_write_data_temp <= 16'h0000;
                        trans_ctrl             <= 'b1;
                        clk_divide             <= 'd10;
                        state                  <= READ_TRANS_START;
                    end


                end

                RESET_READ_EN_BIT : begin
                    if(write_status == 'd51 ) begin
                        if (cdiv_cnt == clk_divide) begin
                            trans_ctrl <= 'b0;
                            state      <= SPI_READ_FINISH;
                        end else begin
                            trans_ctrl <= 'b0;
                        end
                    end else begin
                        single_write_addr_temp <= 8'b00000000;
                        single_write_data_temp <= 16'h0000;
                        trans_ctrl             <= 'b1;
                        clk_divide             <= 'd10;
                        state                  <= RESET_READ_EN_BIT;
                    end
                end

                SPI_WRITE_FINISH : begin
                    if (tool_count == 'd4) begin
                        from_spi_m_write_finish <= 'b0;
                        tool_count              <= 'd0;
                        state                   <= SPI_IDLE;
                    end else begin
                        tool_count                <= tool_count +1'b1;
                        to_ctrl_m_spi_module_idle <= 'd1;
                        from_spi_m_write_finish   <= 'b1;
                    end
                end
                SPI_READ_FINISH : begin
                    if (tool_count == 'd4) begin
                        tool_count                <= 'd0;
                        to_ctrl_m_spi_module_idle <= 'd1;
                        state                     <= SPI_IDLE;
                    end else begin
                        from_spi_read_data       <= res_read_data;
                        from_spi_read_data_valid <= 'b1;
                        tool_count               <= tool_count + 1'b1;
                        state                    <= SPI_READ_FINISH;
                    end
                end
                default : begin
                    state <= SPI_IDLE;
                end
            endcase
        end
    end



    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            cdiv_cnt     <= 'd1;
            write_status <= 'd0;
            SPI_CS_reg   <= 'b0;
            spi_clk      <= 1'b0;
            SPI_MOSI_reg <= 'b0;
            read_data    <= 'd0;
        end else begin
            case(write_status)
                0 : begin
                    if (trans_ctrl == 1'b1) begin
                        if (cdiv_cnt == clk_divide) begin
                            write_status <= write_status +1'b1;
                            cdiv_cnt     <= 'd1;
                        end else begin
                            cdiv_cnt <= cdiv_cnt + 1'b1;
                        end
                        SPI_CS_reg <= 'b0;

                    end else begin
                        write_status <= 'd0;
                        SPI_CS_reg   <= 'b1;
                        spi_clk      <= 'b0;
                    end
                end
                1 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[7];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                2 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                3 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[6];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                4 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                5 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[5];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                6 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                7 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[4];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                8 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                9 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[3];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                10 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                11 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[2];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                12 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                13 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[1];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                14 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                15 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_addr_temp[0];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                16 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +2'd3;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end

                19 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[15];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                20 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[15] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                21 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[14];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                22 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[14] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                23 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[13];

                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                24 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[13] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                25 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[12];


                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                26 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[12] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                27 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[11];



                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                28 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[11] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                29 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[10];



                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                30 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[10] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                31 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[9];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                32 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[9] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end

                33 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[8];

                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                34 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[8] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                35 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[7];


                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                36 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[7] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                37 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[6];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                38 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[6] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                39 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[5];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                40 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[5] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                41 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[4];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                42 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[4] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                43 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[3];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                44 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[3] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                45 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[2];

                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                46 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[2] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                47 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[1];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                48 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[1] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                49 : begin
                    spi_clk      <= 1'b0;
                    SPI_MOSI_reg <= single_write_data_temp[0];
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                50 : begin
                    spi_clk <= 1'b1;
                    if (cdiv_cnt == 1'b1) begin
                        read_data[0] <= SPI_MISO;
                    end
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= write_status +1'b1;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end
                end
                51 : begin
                    SPI_CS_reg <= 'b1;
                    if (cdiv_cnt == clk_divide) begin
                        write_status <= 'd0;
                        cdiv_cnt     <= 'd1;
                    end else begin
                        cdiv_cnt <= cdiv_cnt + 1'b1;
                    end

                end
            endcase
        end
    end

endmodule