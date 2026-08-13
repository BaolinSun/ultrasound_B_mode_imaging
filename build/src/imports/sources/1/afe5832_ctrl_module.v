`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company:
// Engineer:
//
// Create Date: 2024/11/17 20:43:39
// Design Name:
// Module Name: tx7332_ctrl_module
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


module afe5832_ctrl_module #(
    parameter ADDR_WIDTH = 8,
    parameter DATA_WIDTH = 16
) (
    input                       clk                             ,
    input                       rstn                            ,
    // 读取部分
    input                       from_axi_ps_read_start          , // 来自axi-lite模块的reg3[10],读启动信号
    input      [ADDR_WIDTH-1:0] from_axi_ps_single_read_addr    , // 来自axi-lite模块的reg3[9:0] 读取的地址
    input      [ADDR_WIDTH-1:0] from_axi_ps_single_write_addr   , // 来自axi-lite模块的reg1[9:0] 单次写地址信号
    input      [DATA_WIDTH-1:0] from_axi_ps_single_write_data   , // 来自axi-lite模块的reg0[31:0] 单次写数据信号
    input                       from_axi_ps_single_write_start  , // 来自axi-lite模块的reg1[21] 单次写启动信号，模块监测上升沿，每次启动先清0，再写1
    output reg [DATA_WIDTH-1:0] to_axi_ps_single_read_data      , // 输出给axi-lite reg4[31:0]
    output reg                  to_axi_ps_single_read_data_valid, // 输出给axi-lite reg3[22]
    output reg                  to_axi_ps_write_finish          , // 反馈给PS进行写完成判断,读reg3[11],1为写完，每次开始前清0
    input      [DATA_WIDTH-1:0] from_spi_m_read_data            ,
    input                       from_spi_m_read_data_valid      ,
    input                       from_spi_m_spi_module_idle      , // 来自spi模块的空闲电平，1表示空闲
    input                       from_spi_m_write_finish         ,
    output reg                  to_spi_m_read_transcation_start , // 输出给spi模块
    output reg                  to_spi_m_send_transcation_start , // 给SPI 模块的控制信号，启动一次传输
    output reg [ADDR_WIDTH-1:0] to_spi_m_single_w_r_addr        , // 单次写的地址
    output reg [DATA_WIDTH-1:0] to_spi_m_single_write_data       // 单次写的数据

);


/***********************************输入上升沿检测*********************************************/
    reg  from_axi_ps_single_write_start_ff1  ;
    reg  from_axi_ps_single_write_start_ff2  ;
    wire from_axi_ps_single_write_start_pulse;
    assign from_axi_ps_single_write_start_pulse = ~from_axi_ps_single_write_start_ff2 & from_axi_ps_single_write_start_ff1;
    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            from_axi_ps_single_write_start_ff1 <= 0;
            from_axi_ps_single_write_start_ff2 <= 0;
        end else begin
            from_axi_ps_single_write_start_ff1 <= from_axi_ps_single_write_start;
            from_axi_ps_single_write_start_ff2 <= from_axi_ps_single_write_start_ff1;
        end
    end

    reg  from_spi_m_write_finish_ff1  ;
    reg  from_spi_m_write_finish_ff2  ;
    wire from_spi_m_write_finish_pulse;
    assign from_spi_m_write_finish_pulse = ~from_spi_m_write_finish_ff2 & from_spi_m_write_finish_ff1;
    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            from_spi_m_write_finish_ff1 <= 0;
            from_spi_m_write_finish_ff2 <= 0;
        end else begin
            from_spi_m_write_finish_ff1 <= from_spi_m_write_finish;
            from_spi_m_write_finish_ff2 <= from_spi_m_write_finish_ff1;
        end
    end


    reg  from_axi_ps_read_start_ff1  ;
    reg  from_axi_ps_read_start_ff2  ;
    wire from_axi_ps_read_start_pulse;
    assign from_axi_ps_read_start_pulse = ~from_axi_ps_read_start_ff2 & from_axi_ps_read_start_ff1;
    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            from_axi_ps_read_start_ff1 <= 0;
            from_axi_ps_read_start_ff2 <= 0;
        end else begin
            from_axi_ps_read_start_ff1 <= from_axi_ps_read_start;
            from_axi_ps_read_start_ff2 <= from_axi_ps_read_start_ff1;
        end
    end

    reg  from_spi_m_read_data_valid_ff   ;
    reg  from_spi_m_read_data_valid_ff2  ;
    wire from_spi_m_read_data_valid_pulse;
    assign from_spi_m_read_data_valid_pulse = ~from_spi_m_read_data_valid_ff2 & from_spi_m_read_data_valid_ff;
    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            from_spi_m_read_data_valid_ff  <= 'b0;
            from_spi_m_read_data_valid_ff2 <= 'b0;
        end else begin
            from_spi_m_read_data_valid_ff  <= from_spi_m_read_data_valid;
            from_spi_m_read_data_valid_ff2 <= from_spi_m_read_data_valid_ff;
        end
    end



/*********************************************************************************************************/
/******************************************脉冲时间保持****************************************************/

    reg [3:0] state;

    localparam AFE5832_IDLE              = 4'b0000;
    localparam AFE5832_SINGLE_WRITE      = 4'b0001;
    localparam AFE5832_WAIT_WRITE_FINISH = 4'b0011;
    localparam AFE5832_SINGLE_READ       = 4'b0010;
    localparam AFE5832_WAIT_READ_FINISH  = 4'b0110;

    reg [7:0] reg_count;

    reg [ADDR_WIDTH-1:0] from_axi_ps_single_write_addr_temp;
    reg [DATA_WIDTH-1:0] from_axi_ps_single_write_data_temp;

    always @(posedge clk or negedge rstn) begin
        if(~rstn) begin
            state <= AFE5832_IDLE;

            reg_count                          <= 'b0;
            to_axi_ps_write_finish             <= 'b0;
            to_spi_m_send_transcation_start    <= 'b0;
            to_spi_m_single_w_r_addr           <= 'b0;
            to_spi_m_single_write_data         <= 'b0;
            to_spi_m_read_transcation_start    <= 'b0;
            to_axi_ps_single_read_data         <= 'b0;
            to_axi_ps_single_read_data_valid   <= 'b0;
            from_axi_ps_single_write_addr_temp <= 'b0;
            from_axi_ps_single_write_data_temp <= 'b0;
        end else begin
            case (state)
                AFE5832_IDLE : begin // 空闲态
                    to_axi_ps_write_finish           <= 1'b0;
                    to_axi_ps_single_read_data_valid <= 1'b0;

                    if (from_axi_ps_single_write_start_pulse == 1'b1) begin
                        from_axi_ps_single_write_addr_temp <= from_axi_ps_single_write_addr;
                        from_axi_ps_single_write_data_temp <= from_axi_ps_single_write_data;
                        state                              <= AFE5832_SINGLE_WRITE;
                    end
                    else if (from_axi_ps_read_start_pulse == 1'b1) begin
                        state <= AFE5832_SINGLE_READ;
                    end else begin
                        state                              <= AFE5832_IDLE;
                        from_axi_ps_single_write_addr_temp <= 'b0;
                        from_axi_ps_single_write_data_temp <= 'b0;
                    end
                end
                AFE5832_SINGLE_WRITE : begin  // 单独写
                    // 如果模块空闲
                    if (from_spi_m_spi_module_idle == 1'b1 && (to_spi_m_send_transcation_start == 1'b0)) begin
                        to_spi_m_single_w_r_addr        <= from_axi_ps_single_write_addr_temp;
                        to_spi_m_single_write_data      <= from_axi_ps_single_write_data_temp;
                        to_spi_m_send_transcation_start <= 1'b1;
                        state                           <= AFE5832_WAIT_WRITE_FINISH;
                    end
                    else begin
                        state <= AFE5832_SINGLE_WRITE;
                    end
                end
                AFE5832_WAIT_WRITE_FINISH : begin
                    to_spi_m_send_transcation_start <= 1'b0;
                    if (from_spi_m_write_finish_pulse == 1'b1) begin
                        to_axi_ps_write_finish <= 'b1;
                        state                  <= AFE5832_IDLE;
                    end else begin
                        to_axi_ps_write_finish <= 'b0;
                        state                  <= AFE5832_WAIT_WRITE_FINISH;
                    end
                end
                AFE5832_SINGLE_READ : begin
                    // 空闲并且未启动读事务
                    if(from_spi_m_spi_module_idle == 1'b1 && to_spi_m_read_transcation_start == 1'b0) begin
                        // 写地址
                        to_spi_m_single_w_r_addr        <= from_axi_ps_single_read_addr;
                        to_spi_m_read_transcation_start <= 1'b1;
                        state                           <= AFE5832_WAIT_READ_FINISH;
                    end
                    else begin
                        state <= AFE5832_SINGLE_READ;
                    end
                end
                AFE5832_WAIT_READ_FINISH : begin
                    to_spi_m_read_transcation_start <= 1'b0;
                    if (from_spi_m_read_data_valid_ff == 1'b1) begin
                        to_axi_ps_single_read_data       <= from_spi_m_read_data;
                        to_axi_ps_single_read_data_valid <= 1'b1;
                        state                            <= AFE5832_IDLE;
                    end else begin
                        to_axi_ps_single_read_data       <= 'b0;
                        to_axi_ps_single_read_data_valid <= 1'b0;
                        state                            <= AFE5832_WAIT_READ_FINISH;
                    end
                end
                default : begin
                    state <= AFE5832_IDLE;
                end
            endcase
        end
    end

endmodule
