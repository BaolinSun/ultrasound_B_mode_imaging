module tx_cfg_streg
(
	input									clk,
	input									reset,
	input									streg_cfg_en,			//波形配置使能
	output	reg								streg_cfg_done,			//波形配置结束

	input			[6:0]					tx_st_reg_num,
	output	reg								tx_st_reg_rden,
	output	reg		[8:0]					tx_st_reg_rdaddr,
	input			[31:0]					tx_st_reg_rddata,

	output	reg								tx_streg_spi_trig,
	output	reg								tx_streg_spi_mode,			//	0=tx	1=rx
	output	reg		[9:0]					tx0_streg_spi_addr0,
	output	reg		[31:0]					tx0_streg_spi_txdata0,
	output	reg		[9:0]					tx0_streg_spi_addr1,
	output	reg		[31:0]					tx0_streg_spi_txdata1,
	output	reg		[9:0]					tx1_streg_spi_addr0,
	output	reg		[31:0]					tx1_streg_spi_txdata0,
	output	reg		[9:0]					tx1_streg_spi_addr1,
	output	reg		[31:0]					tx1_streg_spi_txdata1,
	input									tx_spi_done,
	input									tx_spi_rdnext
	);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg											streg_cfg_en_t0;
reg											streg_cfg_en_t1;
wire										streg_cfg_en_pos;

always@(posedge clk)
begin
	streg_cfg_en_t0 <= streg_cfg_en;
	streg_cfg_en_t1 <= streg_cfg_en_t0;
end

assign	streg_cfg_en_pos = streg_cfg_en_t0 & (!streg_cfg_en_t1);

//--------------------------------------------------------------------------------------------------
localparam			STREGCFG_IDLE			=	8'b0000_0000;
localparam			STREGCFG_RDCYC			=	8'b0000_0001;
localparam			STREGCFG_RDRAM			=	8'b0000_0010;
localparam			STREGCFG_WORD			=	8'b0000_0100;
localparam			STREGCFG_WAIT0			=	8'b0000_1000;
localparam			STREGCFG_WAIT1			=	8'b0001_0000;
localparam			STREGCFG_WAIT2			=	8'b0010_0000;
localparam			STREGCFG_DONE			=	8'b0100_0000;
//--------------------------------------------------------------------------------------------------
reg					[12:0]					ST_CONFIG;
reg					[12:0]					NT_CONFIG;
reg					[7:0]					word_cnt;
reg					[3:0]					rddata_cnt;
reg											tx_data_en;

always@(posedge clk)
begin
	if(reset)
		ST_CONFIG <= STREGCFG_IDLE;
	else
		ST_CONFIG <= NT_CONFIG;
end

always @(*) 
begin
	case(ST_CONFIG)
		STREGCFG_IDLE:	if(streg_cfg_en_pos)						NT_CONFIG = STREGCFG_RDCYC;
					else											NT_CONFIG = STREGCFG_IDLE;
		STREGCFG_RDCYC:	if(word_cnt==tx_st_reg_num)					NT_CONFIG = STREGCFG_WAIT2;
					else											NT_CONFIG = STREGCFG_RDRAM;
		STREGCFG_RDRAM:	if(rddata_cnt==7)							NT_CONFIG = STREGCFG_WORD;
					else											NT_CONFIG = STREGCFG_RDRAM;
		STREGCFG_WORD:												NT_CONFIG = STREGCFG_WAIT0;
		STREGCFG_WAIT0:												NT_CONFIG = STREGCFG_WAIT1;
		STREGCFG_WAIT1:	if(tx_spi_rdnext)							NT_CONFIG = STREGCFG_RDCYC;
					else											NT_CONFIG = STREGCFG_WAIT1;
		STREGCFG_WAIT2:	if(tx_spi_done)								NT_CONFIG = STREGCFG_DONE;
					else											NT_CONFIG = STREGCFG_WAIT2;
		STREGCFG_DONE:												NT_CONFIG = STREGCFG_IDLE;
		default:													NT_CONFIG = STREGCFG_IDLE;
	endcase	
end
//==================================================================================================
//==================================================================================================
always @(posedge clk) 
begin
	if(NT_CONFIG == STREGCFG_IDLE)									tx_streg_spi_trig <= 1'b0;
	else if(NT_CONFIG == STREGCFG_WAIT0)							tx_streg_spi_trig <= 1'b1;
	else															tx_streg_spi_trig <= 1'b0;

	if(NT_CONFIG == STREGCFG_RDRAM)									tx_st_reg_rden <= 1'b1;
	else 															tx_st_reg_rden <= 1'b0;

	if(NT_CONFIG == STREGCFG_IDLE)									rddata_cnt <= 0;
	else if(NT_CONFIG == STREGCFG_RDCYC)							rddata_cnt <= 0;
	else if(tx_st_reg_rden)											rddata_cnt <= rddata_cnt + 1'b1;

	if(NT_CONFIG == STREGCFG_IDLE)									tx_st_reg_rdaddr <= 0;
	else if(tx_st_reg_rden)											tx_st_reg_rdaddr <= tx_st_reg_rdaddr + 1;
	else 															tx_st_reg_rdaddr <= tx_st_reg_rdaddr;

	tx_streg_spi_mode <= 1'b0;
	tx_data_en <= tx_st_reg_rden;
	
	if(NT_CONFIG == STREGCFG_IDLE)									tx0_streg_spi_addr0 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 1))							tx0_streg_spi_addr0 <= tx_st_reg_rddata[9:0];
	else															tx0_streg_spi_addr0 <= tx0_streg_spi_addr0;

	if(NT_CONFIG == STREGCFG_IDLE)									tx0_streg_spi_txdata0 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 2))							tx0_streg_spi_txdata0 <= tx_st_reg_rddata;
	else															tx0_streg_spi_txdata0 <= tx0_streg_spi_txdata0;

	if(NT_CONFIG == STREGCFG_IDLE)									tx0_streg_spi_addr1 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 3))							tx0_streg_spi_addr1 <= tx_st_reg_rddata[9:0];
	else															tx0_streg_spi_addr1 <= tx0_streg_spi_addr1;

	if(NT_CONFIG == STREGCFG_IDLE)									tx0_streg_spi_txdata1 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 4))							tx0_streg_spi_txdata1 <= tx_st_reg_rddata;
	else															tx0_streg_spi_txdata1 <= tx0_streg_spi_txdata1;

	if(NT_CONFIG == STREGCFG_IDLE)									tx1_streg_spi_addr0 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 5))							tx1_streg_spi_addr0 <= tx_st_reg_rddata[9:0];
	else															tx1_streg_spi_addr0 <= tx1_streg_spi_addr0;
	
	if(NT_CONFIG == STREGCFG_IDLE)									tx1_streg_spi_txdata0 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 6))							tx1_streg_spi_txdata0 <= tx_st_reg_rddata;
	else															tx1_streg_spi_txdata0 <= tx1_streg_spi_txdata0;
	
	if(NT_CONFIG == STREGCFG_IDLE)									tx1_streg_spi_addr1 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 7))							tx1_streg_spi_addr1 <= tx_st_reg_rddata[9:0];
	else															tx1_streg_spi_addr1 <= tx1_streg_spi_addr1;
	
	if(NT_CONFIG == STREGCFG_IDLE)									tx1_streg_spi_txdata1 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 8))							tx1_streg_spi_txdata1 <= tx_st_reg_rddata;
	else															tx1_streg_spi_txdata1 <= tx1_streg_spi_txdata1;

	if(NT_CONFIG == STREGCFG_IDLE)									word_cnt <= 0;
	else if(NT_CONFIG == STREGCFG_WORD)								word_cnt <= word_cnt + 1'b1;

	if(NT_CONFIG == STREGCFG_IDLE)									streg_cfg_done <= 0;
	else if(NT_CONFIG == STREGCFG_DONE)								streg_cfg_done <= 1;
end

endmodule

