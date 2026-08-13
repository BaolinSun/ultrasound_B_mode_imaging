module tx_cfg_rdreg
(
	input									clk,
	input									reset,
	output	reg								rdreg_cfg_done,			//波形配置结束

	input									tx_spi_rdreq,
	input			[9:0]					tx_spi_rdaddr,

	output	reg								tx_rdreg_spi_trig0,
	output	reg								tx_rdreg_spi_mode0,			//	0=tx	1=rx
	output	reg		[9:0]					tx0_rdreg_spi_addr0,
	output	reg		[31:0]					tx0_rdreg_spi_txdata0,
	output	reg								tx_rdreg_spi_trig1,
	output	reg								tx_rdreg_spi_mode1,			//	0=tx	1=rx
	output	reg		[9:0]					tx0_rdreg_spi_addr1,
	output	reg		[31:0]					tx0_rdreg_spi_txdata1,
	output	reg		[9:0]					tx1_rdreg_spi_addr0,
	output	reg		[31:0]					tx1_rdreg_spi_txdata0,
	output	reg		[9:0]					tx1_rdreg_spi_addr1,
	output	reg		[31:0]					tx1_rdreg_spi_txdata1,
	input									tx_spi_done0,
	input									tx_spi_done1
	);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
localparam			REGCFG_IDLE				=	11'b000_0000_0000;
localparam			REGCFG_RDEN1			=	11'b000_0000_0001;
localparam			REGCFG_WAIT0			=	11'b000_0000_0010;
localparam			REGCFG_READ1			=	11'b000_0000_0100;
localparam			REGCFG_WAIT1			=	11'b000_0000_1000;
localparam			REGCFG_RDEN2			=	11'b000_0001_0000;
localparam			REGCFG_WAIT2			=	11'b000_0010_0000;
localparam			REGCFG_READ2			=	11'b000_0100_0000;
localparam			REGCFG_WAIT3			=	11'b000_1000_0000;
localparam			REGCFG_RDDIS			=	11'b001_0000_0000;
localparam			REGCFG_WAIT4			=	11'b010_0000_0000;
localparam			REGCFG_DONE				=	11'b100_0000_0000;
//--------------------------------------------------------------------------------------------------
reg					[10:0]					ST_CONFIG;
reg					[10:0]					NT_CONFIG;
reg					[6:0]					word_cnt;
reg					[3:0]					page_cnt;

always@(posedge clk)
begin
	if(reset)
		ST_CONFIG <= REGCFG_IDLE;
	else
		ST_CONFIG <= NT_CONFIG;
end

always @(*) 
begin
	case(ST_CONFIG)
		REGCFG_IDLE:	if(tx_spi_rdreq)							NT_CONFIG = REGCFG_RDEN1;
					else											NT_CONFIG = REGCFG_IDLE;
		REGCFG_RDEN1:												NT_CONFIG = REGCFG_WAIT0;
		REGCFG_WAIT0:	if(tx_spi_done0)							NT_CONFIG = REGCFG_READ1;
					else											NT_CONFIG = REGCFG_WAIT0;
		REGCFG_READ1:												NT_CONFIG = REGCFG_WAIT1;
		REGCFG_WAIT1:	if(tx_spi_done0)							NT_CONFIG = REGCFG_RDEN2;
					else											NT_CONFIG = REGCFG_WAIT1;
		REGCFG_RDEN2:												NT_CONFIG = REGCFG_WAIT2;
		REGCFG_WAIT2:	if(tx_spi_done0)							NT_CONFIG = REGCFG_READ2;
					else											NT_CONFIG = REGCFG_WAIT2;
		REGCFG_READ2:												NT_CONFIG = REGCFG_WAIT3;
		REGCFG_WAIT3:	if(tx_spi_done1)							NT_CONFIG = REGCFG_RDDIS;
					else											NT_CONFIG = REGCFG_WAIT3;
		REGCFG_RDDIS:												NT_CONFIG = REGCFG_WAIT4;
		REGCFG_WAIT4:	if(tx_spi_done0)							NT_CONFIG = REGCFG_DONE;
					else											NT_CONFIG = REGCFG_WAIT4;
		REGCFG_DONE:												NT_CONFIG = REGCFG_IDLE;
		default:													NT_CONFIG = REGCFG_IDLE;
	endcase	
end
//==================================================================================================
//==================================================================================================
always @(posedge clk) 
begin
	if(NT_CONFIG == REGCFG_RDEN1)									tx_rdreg_spi_trig0 <= 1'b1;
	else if(NT_CONFIG == REGCFG_READ1)								tx_rdreg_spi_trig0 <= 1'b1;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx_rdreg_spi_trig0 <= 1'b1;
	else if(NT_CONFIG == REGCFG_READ2)								tx_rdreg_spi_trig0 <= 1'b0;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx_rdreg_spi_trig0 <= 1'b1;
	else															tx_rdreg_spi_trig0 <= 1'b0;

	if(NT_CONFIG == REGCFG_RDEN1)									tx_rdreg_spi_trig1 <= 1'b1;
	else if(NT_CONFIG == REGCFG_READ1)								tx_rdreg_spi_trig1 <= 1'b0;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx_rdreg_spi_trig1 <= 1'b1;
	else if(NT_CONFIG == REGCFG_READ2)								tx_rdreg_spi_trig1 <= 1'b1;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx_rdreg_spi_trig1 <= 1'b1;
	else															tx_rdreg_spi_trig1 <= 1'b0;

	if(NT_CONFIG == REGCFG_RDEN1)									tx_rdreg_spi_mode0 <= 1'b0;
	else if(NT_CONFIG == REGCFG_READ1)								tx_rdreg_spi_mode0 <= 1'b1;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx_rdreg_spi_mode0 <= 1'b0;
	else if(NT_CONFIG == REGCFG_READ2)								tx_rdreg_spi_mode0 <= 1'b0;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx_rdreg_spi_mode0 <= 1'b0;

	if(NT_CONFIG == REGCFG_RDEN1)									tx_rdreg_spi_mode1 <= 1'b0;
	else if(NT_CONFIG == REGCFG_READ1)								tx_rdreg_spi_mode1 <= 1'b0;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx_rdreg_spi_mode1 <= 1'b0;
	else if(NT_CONFIG == REGCFG_READ2)								tx_rdreg_spi_mode1 <= 1'b1;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx_rdreg_spi_mode1 <= 1'b0;


	if(NT_CONFIG == REGCFG_IDLE)									tx0_rdreg_spi_addr0 <= 10'h000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx0_rdreg_spi_addr0 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ1)								tx0_rdreg_spi_addr0 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDEN2)								tx0_rdreg_spi_addr0 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ2)								tx0_rdreg_spi_addr0 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDDIS)								tx0_rdreg_spi_addr0 <= 10'h000;
	else															tx0_rdreg_spi_addr0 <= tx0_rdreg_spi_addr0;

	if(NT_CONFIG == REGCFG_IDLE)									tx0_rdreg_spi_addr1 <= 10'h000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx0_rdreg_spi_addr1 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ1)								tx0_rdreg_spi_addr1 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDEN2)								tx0_rdreg_spi_addr1 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ2)								tx0_rdreg_spi_addr1 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDDIS)								tx0_rdreg_spi_addr1 <= 10'h000;
	else															tx0_rdreg_spi_addr1 <= tx0_rdreg_spi_addr1;

	if(NT_CONFIG == REGCFG_IDLE)									tx1_rdreg_spi_addr0 <= 10'h000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx1_rdreg_spi_addr0 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ1)								tx1_rdreg_spi_addr0 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDEN2)								tx1_rdreg_spi_addr0 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ2)								tx1_rdreg_spi_addr0 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDDIS)								tx1_rdreg_spi_addr0 <= 10'h000;
	else															tx1_rdreg_spi_addr0 <= tx1_rdreg_spi_addr0;

	if(NT_CONFIG == REGCFG_IDLE)									tx1_rdreg_spi_addr1 <= 10'h000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx1_rdreg_spi_addr1 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ1)								tx1_rdreg_spi_addr1 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDEN2)								tx1_rdreg_spi_addr1 <= 10'h000;
	else if(NT_CONFIG == REGCFG_READ2)								tx1_rdreg_spi_addr1 <= tx_spi_rdaddr[9:0];
	else if(NT_CONFIG == REGCFG_RDDIS)								tx1_rdreg_spi_addr1 <= 10'h000;
	else															tx1_rdreg_spi_addr1 <= tx1_rdreg_spi_addr1;


	if(NT_CONFIG == REGCFG_IDLE)									tx0_rdreg_spi_txdata0 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx0_rdreg_spi_txdata0 <= 32'h00000004;
	else if(NT_CONFIG == REGCFG_READ1)								tx0_rdreg_spi_txdata0 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx0_rdreg_spi_txdata0 <= 32'h00000002;
	else if(NT_CONFIG == REGCFG_READ2)								tx0_rdreg_spi_txdata0 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx0_rdreg_spi_txdata0 <= 32'h00000000;
	else															tx0_rdreg_spi_txdata0 <= tx0_rdreg_spi_txdata0;

	if(NT_CONFIG == REGCFG_IDLE)									tx0_rdreg_spi_txdata1 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx0_rdreg_spi_txdata1 <= 32'h00000004;
	else if(NT_CONFIG == REGCFG_READ1)								tx0_rdreg_spi_txdata1 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx0_rdreg_spi_txdata1 <= 32'h00000002;
	else if(NT_CONFIG == REGCFG_READ2)								tx0_rdreg_spi_txdata1 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx0_rdreg_spi_txdata1 <= 32'h00000000;
	else															tx0_rdreg_spi_txdata1 <= tx0_rdreg_spi_txdata1;


	if(NT_CONFIG == REGCFG_IDLE)									tx1_rdreg_spi_txdata0 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx1_rdreg_spi_txdata0 <= 32'h00000004;
	else if(NT_CONFIG == REGCFG_READ1)								tx1_rdreg_spi_txdata0 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx1_rdreg_spi_txdata0 <= 32'h00000002;
	else if(NT_CONFIG == REGCFG_READ2)								tx1_rdreg_spi_txdata0 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx1_rdreg_spi_txdata0 <= 32'h00000000;
	else															tx1_rdreg_spi_txdata0 <= tx1_rdreg_spi_txdata0;

	if(NT_CONFIG == REGCFG_IDLE)									tx1_rdreg_spi_txdata1 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN1)								tx1_rdreg_spi_txdata1 <= 32'h00000004;
	else if(NT_CONFIG == REGCFG_READ1)								tx1_rdreg_spi_txdata1 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDEN2)								tx1_rdreg_spi_txdata1 <= 32'h00000002;
	else if(NT_CONFIG == REGCFG_READ2)								tx1_rdreg_spi_txdata1 <= 32'h00000000;
	else if(NT_CONFIG == REGCFG_RDDIS)								tx1_rdreg_spi_txdata1 <= 32'h00000000;
	else															tx1_rdreg_spi_txdata1 <= tx1_rdreg_spi_txdata1;

	if(NT_CONFIG == REGCFG_IDLE)									rdreg_cfg_done <= 0;
	else if(NT_CONFIG == REGCFG_DONE)								rdreg_cfg_done <= 1;
end

endmodule