//--------------------------------------------------------------------------------------------------
`include "fpga_define.vh"
//--------------------------------------------------------------------------------
module para_reg
(
	input									clk,
	input									reset,
	input									para_reg_wren,
	input									para_reg_rden,
	input		[19:0]						para_reg_addr,
	input		[31:0]						para_reg_wrdata,
	output	reg	[31:0]						para_reg_rddata,
	output	reg								para_reg_rdvalid,

	output	reg								tx_spi_rdreq,
	output	reg	[31:0]						tx_spi_rdaddr,
	input		[31:0]						tx_spi_rxdata,
	input									tx_spi_rxvalid,
	output		[31:0]						tx_cfg_rddata_debug,
	output	reg								scan_enable,
	output	reg								rtc_enable,
	output	reg								spi_start,
	output	reg								tx_hw_reset
);
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
reg											para_fanout_wren;
reg											para_fanout_rden;
reg				[19:0]						para_fanout_wraddr;
reg				[19:0]						para_fanout_rdaddr;
reg				[31:0]						para_fanout_wrdata;
reg				[31:0]						para_fanout_rddata;
reg											para_fanout_rdvalid;
always@(posedge clk)
begin
	para_fanout_wren	<=	para_reg_wren;
	para_fanout_rden	<=	para_reg_rden;
	para_fanout_wraddr	<=	para_reg_addr;
	para_fanout_rdaddr	<=	para_reg_addr;
	para_fanout_wrdata	<=	para_reg_wrdata;
	
	para_reg_rddata		<=	para_fanout_rddata;
	para_reg_rdvalid	<=	para_fanout_rdvalid;
end
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
reg				[31:0]				tx_bypass;
reg				[31:0]				tx_cfg_rddata;
(* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO", DONT_TOUCH = "TRUE" *) reg [1:0] tx_spi_rxvalid_sync;
reg tx_spi_rxvalid_seen;
reg tx_spi_req_busy;
wire tx_spi_request_write = para_fanout_wren &&
							(para_fanout_wraddr == `ADDR_TX_CFG_RD_REQ);
assign tx_cfg_rddata_debug = tx_cfg_rddata;
always@(posedge clk)
begin
	if(reset)
	begin
		tx_spi_rdreq <= 1'b0;
		tx_spi_rdaddr <= 32'd0;
		tx_spi_rxvalid_sync <= 2'b00;
		tx_spi_rxvalid_seen <= 1'b0;
		tx_spi_req_busy <= 1'b0;
		tx_cfg_rddata <= 32'd0;
	end
	else
	begin
		tx_spi_rxvalid_sync	<= {tx_spi_rxvalid_sync[0], tx_spi_rxvalid};

		// Request is a toggle and the address remains stable until the matching
		// completion toggle returns from clk_200.  A second software request is
		// ignored while one transaction is outstanding.
		if(tx_spi_request_write && !tx_spi_req_busy) begin
			tx_spi_rdaddr <= para_fanout_wrdata;
			tx_spi_rdreq <= ~tx_spi_rdreq;
			tx_spi_req_busy <= 1'b1;
		end

		// The 32-bit result has been held stable for two destination clocks by
		// the time this synchronized completion toggle changes.
		if(tx_spi_rxvalid_sync[1] != tx_spi_rxvalid_seen) begin
			tx_cfg_rddata <= tx_spi_rxdata;
			tx_spi_rxvalid_seen <= tx_spi_rxvalid_sync[1];
			tx_spi_req_busy <= 1'b0;
		end
	end
end

always@(posedge clk)
begin
		 if(para_fanout_wren & (para_fanout_wraddr == `ADDR_SCAN_ENABLE		))		scan_enable		<=	para_fanout_wrdata;
		 if(para_fanout_wren & (para_fanout_wraddr == `ADDR_TX_ENABLE		))		rtc_enable		<=	para_fanout_wrdata;
		 if(para_fanout_wren & (para_fanout_wraddr == `ADDR_SPI_START		))		spi_start		<=	para_fanout_wrdata;
		 if(para_fanout_wren & (para_fanout_wraddr == `ADDR_TX_RESET		))		tx_hw_reset		<=	para_fanout_wrdata;else tx_hw_reset<=1'b0;
		 
		 if(para_fanout_rden & (para_fanout_rdaddr == `ADDR_TX_CFG_RD_REQ		))	para_fanout_rddata	<=	tx_spi_rdaddr	;
	else if(para_fanout_rden & (para_fanout_rdaddr == `ADDR_TX_CFG_RDDATA		))	para_fanout_rddata	<=	tx_cfg_rddata	;
	else if(para_fanout_rden & (para_fanout_rdaddr == `ADDR_SCAN_ENABLE			))	para_fanout_rddata	<=	scan_enable		;
	else if(para_fanout_rden & (para_fanout_rdaddr == `ADDR_SPI_START			))	para_fanout_rddata	<=	spi_start		;
	else if(para_fanout_rden & (para_fanout_rdaddr == `ADDR_TX_RESET			))	para_fanout_rddata	<=	tx_hw_reset		;
	else if(para_fanout_rden)														para_fanout_rddata	<=	32'heeee_eeee;
	
	para_fanout_rdvalid <= para_fanout_rden;
end

endmodule
