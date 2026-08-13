
`timescale 1 ns / 1 ps

module w_ddr_v1_0 #(
	// Users to add parameters here
	parameter         FIFO_DEPTH                         = 8192        ,
	parameter         FIFO_RD_WIDTH                      = 32          ,
	parameter         FIFO_WR_WIDTH                      = 16          ,
	parameter         C_M_TARGET_SLAVE_PER_LINE_INC_ADDR = 32'h00004000,
	parameter integer CP_LINE_NUMBERS                    = 128         ,
	parameter integer USE_PRE_READ = 0,
	parameter integer  BURST_BUFFER_NUMS = 128,
	// User parameters ends
	// Do not modify the parameters beyond this line
	// Parameters of Axi Master Bus Interface M00_AXI
	parameter         C_M00_AXI_TARGET_SLAVE_BASE_ADDR   = 32'h20000000,
	parameter integer C_M00_AXI_BURST_LEN                = 256         ,
	parameter integer C_M00_AXI_ID_WIDTH                 = 1           ,
	parameter integer C_M00_AXI_ADDR_WIDTH               = 32          ,
	parameter integer C_M00_AXI_DATA_WIDTH               = 32          ,
	parameter integer C_M00_AXI_AWUSER_WIDTH             = 0           ,
	parameter integer C_M00_AXI_ARUSER_WIDTH             = 0           ,
	parameter integer C_M00_AXI_WUSER_WIDTH              = 0           ,
	parameter integer C_M00_AXI_RUSER_WIDTH              = 0           ,
	parameter integer C_M00_AXI_BUSER_WIDTH              = 0
) (
	// Users to add ports here
	input       [         FIFO_WR_WIDTH-1:0] in_data         ,
	input                                    in_data_valid   ,
	input                                    in_data_clk     ,
	input       [                      15:0] data_length     ,
	// input [15:0] lines_num,
	// User ports ends
	// Do not modify the ports beyond this line Ports of Axi Master Bus Interface M00_AXI
	// input  wire                              m00_axi_init_axi_txn,
	output wire                              m00_axi_txn_done,
	// output wire                              m00_axi_error    ,
	input  wire                              m00_axi_aclk    ,
	input  wire                              m00_axi_aresetn ,
	output wire [    C_M00_AXI_ID_WIDTH-1:0] m00_axi_awid    ,
	output wire [  C_M00_AXI_ADDR_WIDTH-1:0] m00_axi_awaddr  ,
	output wire [                       7:0] m00_axi_awlen   ,
	output wire [                       2:0] m00_axi_awsize  ,
	output wire [                       1:0] m00_axi_awburst ,
	output wire                              m00_axi_awlock  ,
	output wire [                       3:0] m00_axi_awcache ,
	output wire [                       2:0] m00_axi_awprot  ,
	output wire [                       3:0] m00_axi_awqos   ,
	output wire [C_M00_AXI_AWUSER_WIDTH-1:0] m00_axi_awuser  ,
	output wire                              m00_axi_awvalid ,
	input  wire                              m00_axi_awready ,
	output wire [  C_M00_AXI_DATA_WIDTH-1:0] m00_axi_wdata   ,
	output wire [C_M00_AXI_DATA_WIDTH/8-1:0] m00_axi_wstrb   ,
	output wire                              m00_axi_wlast   ,
	output wire [ C_M00_AXI_WUSER_WIDTH-1:0] m00_axi_wuser   ,
	output wire                              m00_axi_wvalid  ,
	input  wire                              m00_axi_wready  ,
	input  wire [    C_M00_AXI_ID_WIDTH-1:0] m00_axi_bid     ,
	input  wire [                       1:0] m00_axi_bresp   ,
	input  wire [ C_M00_AXI_BUSER_WIDTH-1:0] m00_axi_buser   ,
	input  wire                              m00_axi_bvalid  ,
	output wire                              m00_axi_bready  ,
	output wire [    C_M00_AXI_ID_WIDTH-1:0] m00_axi_arid    ,
	output wire [  C_M00_AXI_ADDR_WIDTH-1:0] m00_axi_araddr  ,
	output wire [                       7:0] m00_axi_arlen   ,
	output wire [                       2:0] m00_axi_arsize  ,
	output wire [                       1:0] m00_axi_arburst ,
	output wire                              m00_axi_arlock  ,
	output wire [                       3:0] m00_axi_arcache ,
	output wire [                       2:0] m00_axi_arprot  ,
	output wire [                       3:0] m00_axi_arqos   ,
	output wire [C_M00_AXI_ARUSER_WIDTH-1:0] m00_axi_aruser  ,
	output wire                              m00_axi_arvalid ,
	input  wire                              m00_axi_arready ,
	input  wire [    C_M00_AXI_ID_WIDTH-1:0] m00_axi_rid     ,
	input  wire [  C_M00_AXI_DATA_WIDTH-1:0] m00_axi_rdata   ,
	input  wire [                       1:0] m00_axi_rresp   ,
	input  wire                              m00_axi_rlast   ,
	input  wire [ C_M00_AXI_RUSER_WIDTH-1:0] m00_axi_ruser   ,
	input  wire                              m00_axi_rvalid  ,
	output wire                              m00_axi_rready
);

	function integer clogb2 (input integer bit_depth);
		begin
			for(clogb2=0; bit_depth>0; clogb2=clogb2+1)
				bit_depth = bit_depth >> 1;
		end
	endfunction

	localparam integer FIFO_READ_DEPTH = FIFO_DEPTH * FIFO_WR_WIDTH / FIFO_RD_WIDTH;
	localparam integer RD_DATA_COUNT_WIDTH = clogb2(FIFO_READ_DEPTH-1) +1;
	localparam integer WR_DATA_COUNT_WIDTH = clogb2(FIFO_DEPTH-1)  +1   ;

	wire                     wr_in_ddr_start;
	wire [FIFO_RD_WIDTH-1:0] rd_data        ;
	wire                     rd_en          ;
	wire                     rd_clk         ;
	wire                     m00_axi_error  ;
	wire [FIFO_WR_WIDTH-1:0] data            ;
	wire [FIFO_WR_WIDTH-1:0] wr_data         ;
	wire                     fifo_full       ;
	wire                     fifo_empty      ;
	wire                     fifo_rst        ;
	wire                     fifo_wr_rst_busy;
	wire                     fifo_rd_rst_busy;
	wire                     data_wr_clk     ;
	wire                     wr_en           ;
	wire                     wr_full         ;
	wire [             RD_DATA_COUNT_WIDTH-1:0] rd_data_count   ;


// 	ila_0 ila_inst (
// 	.clk(m00_axi_aclk), // input wire clk

// 	.probe0(wr_in_ddr_start), // input wire [0:0] probe0
// 	.probe1(rd_data_count), // input wire [0:0] probe0
// 	.probe2(rd_en)
// );


// Instantiation of Axi Bus Interface M00_AXI
	w_ddr_v1_0_M00_AXI #(
		.AXI_RD_FIFO_DATA_WIDTH            (FIFO_RD_WIDTH                     ),
		.FIFO_WR_WIDTH                     (FIFO_WR_WIDTH                     ),
		.BURST_BUFFER_NUMS                 (BURST_BUFFER_NUMS),
		.C_M_TARGET_SLAVE_BASE_ADDR        (C_M00_AXI_TARGET_SLAVE_BASE_ADDR  ),
		.C_M_TARGET_SLAVE_PER_LINE_INC_ADDR(C_M_TARGET_SLAVE_PER_LINE_INC_ADDR),
		.CP_LINE_NUMBERS                   (CP_LINE_NUMBERS                   ),
		.C_M_AXI_BURST_LEN                 (C_M00_AXI_BURST_LEN               ),
		.C_M_AXI_ID_WIDTH                  (C_M00_AXI_ID_WIDTH                ),
		.C_M_AXI_ADDR_WIDTH                (C_M00_AXI_ADDR_WIDTH              ),
		.C_M_AXI_DATA_WIDTH                (FIFO_RD_WIDTH                     ),
		.C_M_AXI_AWUSER_WIDTH              (C_M00_AXI_AWUSER_WIDTH            ),
		.C_M_AXI_ARUSER_WIDTH              (C_M00_AXI_ARUSER_WIDTH            ),
		.C_M_AXI_WUSER_WIDTH               (C_M00_AXI_WUSER_WIDTH             ),
		.C_M_AXI_RUSER_WIDTH               (C_M00_AXI_RUSER_WIDTH             ),
		.C_M_AXI_BUSER_WIDTH               (C_M00_AXI_BUSER_WIDTH             )
	) w_ddr_v1_0_M00_AXI_inst (
		.rd_data        (rd_data         ),
		.rd_en          (rd_en           ),
		.rd_clk         (rd_clk          ),
		.fifo_empty     (fifo_empty      ),
		.wr_in_ddr_start(wr_in_ddr_start ),
		.rd_data_length (data_length     ),
		.TXN_DONE       (m00_axi_txn_done),
		.ERROR          (m00_axi_error   ),
		.M_AXI_ACLK     (m00_axi_aclk    ),
		.M_AXI_ARESETN  (m00_axi_aresetn ),
		.M_AXI_AWID     (m00_axi_awid    ),
		.M_AXI_AWADDR   (m00_axi_awaddr  ),
		.M_AXI_AWLEN    (m00_axi_awlen   ),
		.M_AXI_AWSIZE   (m00_axi_awsize  ),
		.M_AXI_AWBURST  (m00_axi_awburst ),
		.M_AXI_AWLOCK   (m00_axi_awlock  ),
		.M_AXI_AWCACHE  (m00_axi_awcache ),
		.M_AXI_AWPROT   (m00_axi_awprot  ),
		.M_AXI_AWQOS    (m00_axi_awqos   ),
		.M_AXI_AWUSER   (m00_axi_awuser  ),
		.M_AXI_AWVALID  (m00_axi_awvalid ),
		.M_AXI_AWREADY  (m00_axi_awready ),
		.M_AXI_WDATA    (m00_axi_wdata   ),
		.M_AXI_WSTRB    (m00_axi_wstrb   ),
		.M_AXI_WLAST    (m00_axi_wlast   ),
		.M_AXI_WUSER    (m00_axi_wuser   ),
		.M_AXI_WVALID   (m00_axi_wvalid  ),
		.M_AXI_WREADY   (m00_axi_wready  ),
		.M_AXI_BID      (m00_axi_bid     ),
		.M_AXI_BRESP    (m00_axi_bresp   ),
		.M_AXI_BUSER    (m00_axi_buser   ),
		.M_AXI_BVALID   (m00_axi_bvalid  ),
		.M_AXI_BREADY   (m00_axi_bready  ),
		.M_AXI_ARID     (m00_axi_arid    ),
		.M_AXI_ARADDR   (m00_axi_araddr  ),
		.M_AXI_ARLEN    (m00_axi_arlen   ),
		.M_AXI_ARSIZE   (m00_axi_arsize  ),
		.M_AXI_ARBURST  (m00_axi_arburst ),
		.M_AXI_ARLOCK   (m00_axi_arlock  ),
		.M_AXI_ARCACHE  (m00_axi_arcache ),
		.M_AXI_ARPROT   (m00_axi_arprot  ),
		.M_AXI_ARQOS    (m00_axi_arqos   ),
		.M_AXI_ARUSER   (m00_axi_aruser  ),
		.M_AXI_ARVALID  (m00_axi_arvalid ),
		.M_AXI_ARREADY  (m00_axi_arready ),
		.M_AXI_RID      (m00_axi_rid     ),
		.M_AXI_RDATA    (m00_axi_rdata   ),
		.M_AXI_RRESP    (m00_axi_rresp   ),
		.M_AXI_RLAST    (m00_axi_rlast   ),
		.M_AXI_RUSER    (m00_axi_ruser   ),
		.M_AXI_RVALID   (m00_axi_rvalid  ),
		.M_AXI_RREADY   (m00_axi_rready  )
	);

	recv_data_wr_fifo #(
		.RECV_DATA_WIDTH    (FIFO_WR_WIDTH      ),
		.RD_DATA_WIDTH      (FIFO_RD_WIDTH      ),
		.WR_DATA_WIDTH      (FIFO_WR_WIDTH      ),
		.USE_PRE_READ       (USE_PRE_READ),
		.RD_DATA_COUNT_WIDTH(RD_DATA_COUNT_WIDTH),
		.WR_DATA_COUNT_WIDTH(WR_DATA_COUNT_WIDTH)
	) inst_recv_data_wr_fifo (
		.rstn            (m00_axi_aresetn ),
		.data            (in_data         ),
		.data_clk        (in_data_clk     ),
		.data_valid      (in_data_valid   ),
		.data_length     (data_length     ),
		.fifo_full       (fifo_full       ),
		.fifo_empty      (fifo_empty      ),
		.data_wr_clk     (data_wr_clk     ),
		.rd_data_cnt     (rd_data_count   ),
		.wr_data         (wr_data         ),
		.wr_en           (wr_en           ),
		.wr_full         (wr_full         ),
		.fifo_rst        (fifo_rst        ),
		.fifo_wr_rst_busy(fifo_wr_rst_busy),
		.fifo_rd_rst_busy(fifo_rd_rst_busy),
		.wr_in_ddr_start (wr_in_ddr_start )
	);

	// wire [12:0] wr_data_count;

// fifo_generator_0 your_instance_name (
//   .rst(fifo_rst),                      // input wire rst
//   .wr_clk(data_wr_clk),                // input wire wr_clk
//   .rd_clk(rd_clk),                // input wire rd_clk
//   .din(wr_data),                      // input wire [511 : 0] din
//   .wr_en(wr_en),                  // input wire wr_en
//   .rd_en(rd_en),                  // input wire rd_en
//   .dout(rd_data),                    // output wire [63 : 0] dout
//   .full(fifo_full ),                    // output wire full
//   .empty( fifo_empty),                  // output wire empty
//   .rd_data_count(rd_data_count),  // output wire [15 : 0] rd_data_count
//   .wr_data_count(wr_data_count),  // output wire [12 : 0] wr_data_count
//   .wr_rst_busy(fifo_wr_rst_busy),      // output wire wr_rst_busy
//   .rd_rst_busy(fifo_rd_rst_busy)      // output wire rd_rst_busy
// );

	m_fifo #(
		.FIFO_DEPTH         (FIFO_DEPTH         ),
		.FIFO_RD_WIDTH      (FIFO_RD_WIDTH      ),
		.FIFO_WR_WIDTH      (FIFO_WR_WIDTH      ),
		.RD_DATA_COUNT_WIDTH(RD_DATA_COUNT_WIDTH),
		.WR_DATA_COUNT_WIDTH(WR_DATA_COUNT_WIDTH)
	) inst_m_fifo (
		.rd_clk       (rd_clk          ),
		.wr_clk       (data_wr_clk     ),
		.rd_en        (rd_en           ),
		.wr_en        (wr_en           ),
		.din          (wr_data         ),
		.rst          (fifo_rst        ),
		.dout         (rd_data         ),
		.rd_data_count(rd_data_count   ),
		.rd_rst_busy  (fifo_rd_rst_busy),
		.wr_rst_busy  (fifo_wr_rst_busy),
		.fifo_full    (fifo_full       ),
		.fifo_empty   (fifo_empty      )
	);

	// Add user logic here

	// User logic ends

endmodule
