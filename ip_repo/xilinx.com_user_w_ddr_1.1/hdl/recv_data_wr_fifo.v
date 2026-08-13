`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company:
// Engineer:
//
// Create Date: 2025/07/29 22:30:51
// Design Name:
// Module Name: recv_data_wr_fifo
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


module recv_data_wr_fifo #(parameter RECV_DATA_WIDTH = 16,
    parameter integer RD_DATA_WIDTH = 64,
    parameter integer WR_DATA_WIDTH = 16,
    parameter integer USE_PRE_READ = 0,
    parameter integer RD_DATA_COUNT_WIDTH = 13  ,
    parameter integer WR_DATA_COUNT_WIDTH = 12) (
    input                            rstn            ,
    input      [RECV_DATA_WIDTH-1:0] data            ,
    input                            data_clk        ,
    input                            data_valid      ,
    input      [               15:0] data_length     ,
    input                            fifo_wr_rst_busy,
    input                            fifo_rd_rst_busy,
    input                            fifo_full       ,
    input                            fifo_empty      ,
    input      [               RD_DATA_COUNT_WIDTH-1:0] rd_data_cnt     ,
    output                           data_wr_clk     ,
    output     [RECV_DATA_WIDTH-1:0] wr_data         ,
    output                           wr_en           ,
    output                           wr_full         ,
    output reg                       fifo_rst        ,
    output                           wr_in_ddr_start
);

    localparam IDLE           = 2'b00;
    localparam START_TRANSFER = 2'b01;
    localparam TRANSFERING    = 2'b11;
    localparam END_TRANSFER   = 2'b10;

    function integer clogb2 (input integer bit_depth);
        begin
            for(clogb2=0; bit_depth>0; clogb2=clogb2+1)
                bit_depth = bit_depth >> 1;
        end
    endfunction
    
    // reg  [15:0] count           ;
    wire        fifo_rst_busy   ;
    reg         wr_ddr_start    ;

    reg fifo_rd_rst_busy_ff;
    reg fifo_wr_rst_busy_ff;
    reg fifo_full_ff       ;
    reg fifo_empty_ff      ;
    reg rstn_ff            ;

    wire wr_over_length;
    // 判断写入是否超过限制
    wire [15:0] length_threshold;

    generate
        if (RD_DATA_WIDTH > WR_DATA_WIDTH) begin
            localparam integer RD_LEN_TO_WR_LEN_BIT_WIDTHS  = clogb2(RD_DATA_WIDTH/WR_DATA_WIDTH-1);
            assign length_threshold = data_length>>RD_LEN_TO_WR_LEN_BIT_WIDTHS;
        end else begin
            localparam integer RD_LEN_TO_WR_LEN_BIT_WIDTHS  = clogb2(WR_DATA_WIDTH/RD_DATA_WIDTH-1);
            assign length_threshold = data_length<<RD_LEN_TO_WR_LEN_BIT_WIDTHS;
        end
    endgenerate
    generate
        if (USE_PRE_READ == 1) begin
            assign wr_over_length = (rd_data_cnt >= (length_threshold >>1 -1));
        end else begin
            assign wr_over_length = (rd_data_cnt >= (length_threshold -1));
        end
    endgenerate
    

    // 写使能判断信号
    assign wr_en = data_valid & ~fifo_full_ff & ~fifo_rst_busy;

    assign fifo_rst_busy = fifo_rd_rst_busy_ff | fifo_wr_rst_busy_ff;

    assign data_wr_clk     = data_clk;
    assign wr_data         = data;
    assign wr_in_ddr_start = wr_ddr_start;

    always @(posedge data_clk) begin
        rstn_ff <= rstn;
    end

    always @(posedge data_clk) begin
        if(~rstn_ff) begin
            fifo_rd_rst_busy_ff <= 0;
            fifo_wr_rst_busy_ff <= 0;
            fifo_full_ff        <= 0;
            fifo_empty_ff       <= 0;
        end else begin
            fifo_rd_rst_busy_ff <= fifo_rd_rst_busy;
            fifo_wr_rst_busy_ff <= fifo_wr_rst_busy;
            fifo_full_ff        <= fifo_full;
            fifo_empty_ff       <= fifo_empty;
        end
    end

    always @(posedge data_clk) begin
        if(~rstn_ff) begin
            fifo_rst <= 'b1;
        end else begin
            fifo_rst <= 'b0;
        end
    end

    // always @(posedge data_clk) begin
    //     if (~rstn_ff) begin
    //         count <= 0;
    //     end else begin
    //         if(data_valid == 1'b1 && ~fifo_rst_busy) begin
    //             if (~wr_over_length) begin
    //                 count <= count + 'b1;
    //             end else begin
    //                 count <= 'd0;
    //             end
    //         end else begin
    //             count <= count;
    //         end
    //     end
    // end

    always @(posedge data_clk or negedge rstn_ff) begin
        if(~rstn_ff) begin
            wr_ddr_start <= 'b0;
        end else begin
            if (wr_over_length == 1'b1) begin
                wr_ddr_start <= 'b1;
            end else begin
                wr_ddr_start <= 'b0;
            end
        end
    end

endmodule
