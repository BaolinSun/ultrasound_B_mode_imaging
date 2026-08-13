`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company:
// Engineer:
//
// Create Date: 2025/07/31 20:04:17
// Design Name:
// Module Name: full_tb
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


module full_tb ();

    // Parameters
    parameter FIFO_DEPTH                       = 8192        ;
    parameter FIFO_RD_WIDTH                    = 64          ;
    parameter FIFO_WR_WIDTH                    = 16          ;
    parameter C_M00_AXI_TARGET_SLAVE_BASE_ADDR = 32'h20000000;
    parameter C_M_TARGET_SLAVE_PER_LINE_INC_ADDR = 32'h00004000;
    parameter CP_LINE_NUMBERS = 128;
    parameter C_M00_AXI_BURST_LEN              = 256         ;
    parameter C_M00_AXI_ID_WIDTH               = 1           ;
    parameter C_M00_AXI_ADDR_WIDTH             = 32          ;
    parameter C_M00_AXI_DATA_WIDTH             = 64          ;
    parameter C_M00_AXI_AWUSER_WIDTH           = 0           ;
    parameter C_M00_AXI_ARUSER_WIDTH           = 0           ;
    parameter C_M00_AXI_WUSER_WIDTH            = 0           ;
    parameter C_M00_AXI_RUSER_WIDTH            = 0           ;
    parameter C_M00_AXI_BUSER_WIDTH            = 0           ;

    // Clock and reset
    reg clk    = 0;
    reg resetn = 0;


    // User inputs
    reg [FIFO_WR_WIDTH-1:0] in_data      ;
    reg                     in_data_valid;
    reg                     in_data_clk  ;
    reg [             15:0] data_length  ;
    reg [             15:0] lines_num    ;

    // AXI control
    reg  m00_axi_init_axi_txn;
    wire m00_axi_txn_done    ;
    wire m00_axi_error       ;


    // AXI signals
    wire [    C_M00_AXI_ID_WIDTH-1:0] m00_axi_awid   ;
    wire [  C_M00_AXI_ADDR_WIDTH-1:0] m00_axi_awaddr ;
    wire [                       7:0] m00_axi_awlen  ;
    wire [                       2:0] m00_axi_awsize ;
    wire [                       1:0] m00_axi_awburst;
    wire                              m00_axi_awlock ;
    wire [                       3:0] m00_axi_awcache;
    wire [                       2:0] m00_axi_awprot ;
    wire [                       3:0] m00_axi_awqos  ;
    wire [C_M00_AXI_AWUSER_WIDTH-1:0] m00_axi_awuser ;
    wire                              m00_axi_awvalid;
    reg                               m00_axi_awready;

    wire [  C_M00_AXI_DATA_WIDTH-1:0] m00_axi_wdata ;
    wire [C_M00_AXI_DATA_WIDTH/8-1:0] m00_axi_wstrb ;
    wire                              m00_axi_wlast ;
    wire [ C_M00_AXI_WUSER_WIDTH-1:0] m00_axi_wuser ;
    wire                              m00_axi_wvalid;
    reg                               m00_axi_wready;

    reg  [   C_M00_AXI_ID_WIDTH-1:0] m00_axi_bid    = 0;
    reg  [                      1:0] m00_axi_bresp  = 0;
    reg  [C_M00_AXI_BUSER_WIDTH-1:0] m00_axi_buser  = 0;
    reg                              m00_axi_bvalid = 0;
    wire                             m00_axi_bready    ;

    wire [    C_M00_AXI_ID_WIDTH-1:0] m00_axi_arid       ;
    wire [  C_M00_AXI_ADDR_WIDTH-1:0] m00_axi_araddr     ;
    wire [                       7:0] m00_axi_arlen      ;
    wire [                       2:0] m00_axi_arsize     ;
    wire [                       1:0] m00_axi_arburst    ;
    wire                              m00_axi_arlock     ;
    wire [                       3:0] m00_axi_arcache    ;
    wire [                       2:0] m00_axi_arprot     ;
    wire [                       3:0] m00_axi_arqos      ;
    wire [C_M00_AXI_ARUSER_WIDTH-1:0] m00_axi_aruser     ;
    wire                              m00_axi_arvalid    ;
    reg                               m00_axi_arready = 0;

    reg  [   C_M00_AXI_ID_WIDTH-1:0] m00_axi_rid    = 0;
    reg  [ C_M00_AXI_DATA_WIDTH-1:0] m00_axi_rdata  = 0;
    reg  [                      1:0] m00_axi_rresp  = 0;
    reg                              m00_axi_rlast  = 0;
    reg  [C_M00_AXI_RUSER_WIDTH-1:0] m00_axi_ruser  = 0;
    reg                              m00_axi_rvalid = 0;
    wire                             m00_axi_rready    ;
    w_ddr_v1_0 #(
            .FIFO_DEPTH(FIFO_DEPTH),
            .FIFO_RD_WIDTH(FIFO_RD_WIDTH),
            .FIFO_WR_WIDTH(FIFO_WR_WIDTH),
            .C_M_TARGET_SLAVE_PER_LINE_INC_ADDR(C_M_TARGET_SLAVE_PER_LINE_INC_ADDR),
            .CP_LINE_NUMBERS(CP_LINE_NUMBERS),
            .USE_PRE_READ                      (0),
            .BURST_BUFFER_NUMS                 (500),
            .C_M00_AXI_TARGET_SLAVE_BASE_ADDR(C_M00_AXI_TARGET_SLAVE_BASE_ADDR),
            .C_M00_AXI_BURST_LEN(C_M00_AXI_BURST_LEN),
            .C_M00_AXI_ID_WIDTH(C_M00_AXI_ID_WIDTH),
            .C_M00_AXI_ADDR_WIDTH(C_M00_AXI_ADDR_WIDTH),
            .C_M00_AXI_DATA_WIDTH(C_M00_AXI_DATA_WIDTH),
            .C_M00_AXI_AWUSER_WIDTH(C_M00_AXI_AWUSER_WIDTH),
            .C_M00_AXI_ARUSER_WIDTH(C_M00_AXI_ARUSER_WIDTH),
            .C_M00_AXI_WUSER_WIDTH(C_M00_AXI_WUSER_WIDTH),
            .C_M00_AXI_RUSER_WIDTH(C_M00_AXI_RUSER_WIDTH),
            .C_M00_AXI_BUSER_WIDTH(C_M00_AXI_BUSER_WIDTH)
        ) inst_w_ddr_v1_0 (
            .in_data          (in_data),
            .in_data_valid    (in_data_valid),
            .in_data_clk      (in_data_clk),
            .data_length      (data_length),
            .m00_axi_txn_done (m00_axi_txn_done),
            .m00_axi_aclk     (clk),
            .m00_axi_aresetn  (resetn),
            .m00_axi_awid     (m00_axi_awid),
            .m00_axi_awaddr   (m00_axi_awaddr),
            .m00_axi_awlen    (m00_axi_awlen),
            .m00_axi_awsize   (m00_axi_awsize),
            .m00_axi_awburst  (m00_axi_awburst),
            .m00_axi_awlock   (m00_axi_awlock),
            .m00_axi_awcache  (m00_axi_awcache),
            .m00_axi_awprot   (m00_axi_awprot),
            .m00_axi_awqos    (m00_axi_awqos),
            .m00_axi_awuser   (m00_axi_awuser),
            .m00_axi_awvalid  (m00_axi_awvalid),
            .m00_axi_awready  (m00_axi_awready),
            .m00_axi_wdata    (m00_axi_wdata),
            .m00_axi_wstrb    (m00_axi_wstrb),
            .m00_axi_wlast    (m00_axi_wlast),
            .m00_axi_wuser    (m00_axi_wuser),
            .m00_axi_wvalid   (m00_axi_wvalid),
            .m00_axi_wready   (m00_axi_wready),
            .m00_axi_bid      (m00_axi_bid),
            .m00_axi_bresp    (m00_axi_bresp),
            .m00_axi_buser    (m00_axi_buser),
            .m00_axi_bvalid   (m00_axi_bvalid),
            .m00_axi_bready   (m00_axi_bready),
            .m00_axi_arid     (m00_axi_arid),
            .m00_axi_araddr   (m00_axi_araddr),
            .m00_axi_arlen    (m00_axi_arlen),
            .m00_axi_arsize   (m00_axi_arsize),
            .m00_axi_arburst  (m00_axi_arburst),
            .m00_axi_arlock   (m00_axi_arlock),
            .m00_axi_arcache  (m00_axi_arcache),
            .m00_axi_arprot   (m00_axi_arprot),
            .m00_axi_arqos    (m00_axi_arqos),
            .m00_axi_aruser   (m00_axi_aruser),
            .m00_axi_arvalid  (m00_axi_arvalid),
            .m00_axi_arready  (m00_axi_arready),
            .m00_axi_rid      (m00_axi_rid),
            .m00_axi_rdata    (m00_axi_rdata),
            .m00_axi_rresp    (m00_axi_rresp),
            .m00_axi_rlast    (m00_axi_rlast),
            .m00_axi_ruser    (m00_axi_ruser),
            .m00_axi_rvalid   (m00_axi_rvalid),
            .m00_axi_rready   (m00_axi_rready)
        );
    // Clock generation
    always #2.5 clk = ~clk;
    always #5 in_data_clk = ~in_data_clk;

    reg [15:0] cnt;
    reg [1:0] valid_cnt;
    reg [15:0] valid_cnt_reg;
    // Stimulus
    initial begin
        $display("Test started.");
        m00_axi_bvalid = 0;
        m00_axi_bresp = 2'b00;
        m00_axi_bid = 0;
        valid_cnt =0;

        cnt <= 0;
        in_data_clk = 0;
        in_data_valid = 0;
        in_data = 0;
        m00_axi_init_axi_txn = 0;
        data_length = 8192;
        lines_num = 1;
        m00_axi_awready = 1;
        m00_axi_wready  = 1;
valid_cnt_reg = 0;
        // Apply reset
        resetn = 0;
        #100;
        resetn = 1;
        #10000;


        // Write some data into the FIFO
        // in_data_valid = 1;
       while(1) begin
            @(posedge in_data_clk);
            valid_cnt = valid_cnt + 1;
            
            // in_data = in_data + 1'b1;
            if (valid_cnt == 3) begin
                valid_cnt_reg = valid_cnt_reg +1;
                 in_data_valid = 1;
                 in_data = cnt;
                 if (cnt > 2056) begin
                     cnt = 0;
                 end else begin
                 cnt = cnt +1;
                     
                 end
                 // in_data = {32{cnt}};
                 // if (valid_cnt_reg > data_length) begin
                 //     in_data_valid = 0;
                 //     #250000;
                 // end
            end else begin
                in_data_valid = 0;
            end
        end
        in_data_valid = 0;

        // Wait for transaction to complete
        wait(m00_axi_txn_done == 1);

        $display("AXI Transaction done.");

        #100;
        $finish;
    end

    

    // 模拟 AXI slave 返回 BVALID 响应
    initial begin
        #300

        while (m00_axi_txn_done==0 ) begin
            wait(m00_axi_wvalid && m00_axi_wready);
             @(posedge clk);

            // 等待写最后一个数据完成（WLAST=1）
            while (m00_axi_wlast == 0) begin
                @(posedge clk);
            end

            m00_axi_bvalid = 1;
            m00_axi_bresp = 2'b00; // OKAY response
            // 模拟 Slave 响应 BVALID
            @(posedge clk);
            wait(m00_axi_bready);  // 等待 DUT 接收写响应
            @(posedge clk);
            m00_axi_bvalid = 0;
        end
    end

endmodule
