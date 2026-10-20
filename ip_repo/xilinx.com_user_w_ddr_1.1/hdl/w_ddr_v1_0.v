
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
	// Frame-store control is synchronous to m00_axi_aclk.
    input wire in_header, input wire in_last, input wire in_busy,
    input wire [23:0] ctrl_addr, input wire [31:0] ctrl_wdata,
    input wire ctrl_en, input wire [3:0] ctrl_we,
    output wire [31:0] ctrl_rdata,
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

    ddr_frame_store #(.FIFO_DEPTH(FIFO_DEPTH)) frame_store_i(
        .clk_axi(m00_axi_aclk),.rst_n(m00_axi_aresetn),.clk_in(in_data_clk),
        .in_data(in_data),.in_valid(in_data_valid),.in_header(in_header),
        .in_last(in_last),.in_busy(in_busy),
        .ctrl_addr(ctrl_addr),.ctrl_wdata(ctrl_wdata),.ctrl_en(ctrl_en),.ctrl_we(ctrl_we),
        .ctrl_rdata(ctrl_rdata),.awaddr(m00_axi_awaddr),.awlen(m00_axi_awlen),
        .awvalid(m00_axi_awvalid),.awready(m00_axi_awready),
        .wdata(m00_axi_wdata),.wvalid(m00_axi_wvalid),.wready(m00_axi_wready),
        .wlast(m00_axi_wlast),.bvalid(m00_axi_bvalid),.bresp(m00_axi_bresp),
        .bready(m00_axi_bready),.frame_done(m00_axi_txn_done));
    assign m00_axi_awid=0;
    assign m00_axi_awsize=3;
    assign m00_axi_awburst=1;
    assign m00_axi_awlock=0;
    assign m00_axi_awcache=4'b0011;
    assign m00_axi_awprot=0;
    assign m00_axi_awqos=0;
    assign m00_axi_awuser=0;
    assign m00_axi_wstrb=8'hff;
    assign m00_axi_wuser=0;
    assign m00_axi_arid=0;
    assign m00_axi_araddr=0;
    assign m00_axi_arlen=0;
    assign m00_axi_arsize=3;
    assign m00_axi_arburst=1;
    assign m00_axi_arlock=0;
    assign m00_axi_arcache=0;
    assign m00_axi_arprot=0;
    assign m00_axi_arqos=0;
    assign m00_axi_aruser=0;
    assign m00_axi_arvalid=0;
    assign m00_axi_rready=1;
endmodule
