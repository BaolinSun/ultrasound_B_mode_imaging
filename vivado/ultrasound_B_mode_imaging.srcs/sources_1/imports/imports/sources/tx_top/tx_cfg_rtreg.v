module tx_cfg_rtreg
(
	input									clk,
	input									reset,
	input			[6:0]					rtc_line,
	input									rtreg_cfg_en,			//波形配置使能
	output	reg								rtreg_cfg_done,			//波形配置结束
	
	input			[4:0]					tx_rt_reg_num,
	output	reg								tx_rt_reg_rden,
	output	reg		[13:0]					tx_rt_reg_rdaddr,
	input			[31:0]					tx_rt_reg_rddata,

	output	reg								tx_rtreg_spi_trig,
	output	reg								tx_rtreg_spi_mode,
	output	reg		[9:0]					tx0_rtreg_spi_addr0,
	output	reg		[31:0]					tx0_rtreg_spi_txdata0,
	output	reg		[9:0]					tx0_rtreg_spi_addr1,
	output	reg		[31:0]					tx0_rtreg_spi_txdata1,
	output	reg		[9:0]					tx1_rtreg_spi_addr0,
	output	reg		[31:0]					tx1_rtreg_spi_txdata0,
	output	reg		[9:0]					tx1_rtreg_spi_addr1,
	output	reg		[31:0]					tx1_rtreg_spi_txdata1,
	input									tx_spi_done,
	input									tx_spi_rdnext
	);
//--------------------------------------------------------------------------------------------------
reg											rtreg_cfg_en_t0;
reg											rtreg_cfg_en_t1;
wire										rtreg_cfg_en_neg;

always@(posedge clk)
begin
	rtreg_cfg_en_t0 <= rtreg_cfg_en;
	rtreg_cfg_en_t1 <= rtreg_cfg_en_t0;
end

assign	rtreg_cfg_en_neg = (!rtreg_cfg_en_t0) & rtreg_cfg_en_t1;

//--------------------------------------------------------------------------------------------------
localparam			RTREGCFG_IDLE			=	8'b0000_0000;
localparam			RTREGCFG_RDCYC			=	8'b0000_0001;
localparam			RTREGCFG_RDRAM			=	8'b0000_0010;
localparam			RTREGCFG_WORD			=	8'b0000_0100;
localparam			RTREGCFG_WAIT0			=	8'b0000_1000;
localparam			RTREGCFG_WAIT1			=	8'b0001_0000;
localparam			RTREGCFG_WAIT2			=	8'b0010_0000;
localparam			RTREGCFG_DONE			=	8'b0100_0000;
//--------------------------------------------------------------------------------------------------
reg					[12:0]					ST_CONFIG;
reg					[12:0]					NT_CONFIG;
reg					[6:0]					word_cnt;
reg					[3:0]					rddata_cnt;
reg											tx_data_en;

always@(posedge clk)
begin
	if(reset)
		ST_CONFIG <= RTREGCFG_IDLE;
	else
		ST_CONFIG <= NT_CONFIG;
end

always @(*) 
begin
	case(ST_CONFIG)
		RTREGCFG_IDLE:	if(rtreg_cfg_en_neg)						NT_CONFIG = RTREGCFG_RDCYC;
					else											NT_CONFIG = RTREGCFG_IDLE;
		RTREGCFG_RDCYC:	if(word_cnt==tx_rt_reg_num)					NT_CONFIG = RTREGCFG_WAIT2;
					else											NT_CONFIG = RTREGCFG_RDRAM;
		RTREGCFG_RDRAM:	if(rddata_cnt==7)							NT_CONFIG = RTREGCFG_WORD;
					else											NT_CONFIG = RTREGCFG_RDRAM;
		RTREGCFG_WORD:												NT_CONFIG = RTREGCFG_WAIT0;
		RTREGCFG_WAIT0:												NT_CONFIG = RTREGCFG_WAIT1;
		RTREGCFG_WAIT1:	if(tx_spi_rdnext)							NT_CONFIG = RTREGCFG_RDCYC;
					else											NT_CONFIG = RTREGCFG_WAIT1;
		RTREGCFG_WAIT2:	if(tx_spi_done)								NT_CONFIG = RTREGCFG_DONE;
					else											NT_CONFIG = RTREGCFG_WAIT2;
		RTREGCFG_DONE:												NT_CONFIG = RTREGCFG_IDLE;
		default:													NT_CONFIG = RTREGCFG_IDLE;
	endcase	
end
//==================================================================================================
//==================================================================================================
always @(posedge clk) 
begin
	if(NT_CONFIG == RTREGCFG_IDLE)									tx_rtreg_spi_trig <= 1'b0;
	else if(NT_CONFIG == RTREGCFG_WAIT0)							tx_rtreg_spi_trig <= 1'b1;
	else															tx_rtreg_spi_trig <= 1'b0;

	if(NT_CONFIG == RTREGCFG_RDRAM)									tx_rt_reg_rden <= 1'b1;
	else 															tx_rt_reg_rden <= 1'b0;

	if(NT_CONFIG == RTREGCFG_IDLE)									rddata_cnt <= 0;
	else if(NT_CONFIG == RTREGCFG_RDCYC)							rddata_cnt <= 0;
	else if(tx_rt_reg_rden)											rddata_cnt <= rddata_cnt + 1'b1;

	if(NT_CONFIG == RTREGCFG_IDLE)									tx_rt_reg_rdaddr <= {rtc_line,7'd0};
	else if(tx_rt_reg_rden)											tx_rt_reg_rdaddr <= tx_rt_reg_rdaddr + 1;
	else 															tx_rt_reg_rdaddr <= tx_rt_reg_rdaddr;

	tx_rtreg_spi_mode <= 1'b0;
	
	tx_data_en <= tx_rt_reg_rden;
	
	if(NT_CONFIG == RTREGCFG_IDLE)									tx0_rtreg_spi_addr0 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 1))							tx0_rtreg_spi_addr0 <= tx_rt_reg_rddata[9:0];
	else															tx0_rtreg_spi_addr0 <= tx0_rtreg_spi_addr0;

	if(NT_CONFIG == RTREGCFG_IDLE)									tx0_rtreg_spi_txdata0 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 2))							tx0_rtreg_spi_txdata0 <= tx_rt_reg_rddata;
	else															tx0_rtreg_spi_txdata0 <= tx0_rtreg_spi_txdata0;

	if(NT_CONFIG == RTREGCFG_IDLE)									tx0_rtreg_spi_addr1 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 3))							tx0_rtreg_spi_addr1 <= tx_rt_reg_rddata[9:0];
	else															tx0_rtreg_spi_addr1 <= tx0_rtreg_spi_addr1;

	if(NT_CONFIG == RTREGCFG_IDLE)									tx0_rtreg_spi_txdata1 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 4))							tx0_rtreg_spi_txdata1 <= tx_rt_reg_rddata;
	else															tx0_rtreg_spi_txdata1 <= tx0_rtreg_spi_txdata1;

	if(NT_CONFIG == RTREGCFG_IDLE)									tx1_rtreg_spi_addr0 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 5))							tx1_rtreg_spi_addr0 <= tx_rt_reg_rddata[9:0];
	else															tx1_rtreg_spi_addr0 <= tx1_rtreg_spi_addr0;
	
	if(NT_CONFIG == RTREGCFG_IDLE)									tx1_rtreg_spi_txdata0 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 6))							tx1_rtreg_spi_txdata0 <= tx_rt_reg_rddata;
	else															tx1_rtreg_spi_txdata0 <= tx1_rtreg_spi_txdata0;
	
	if(NT_CONFIG == RTREGCFG_IDLE)									tx1_rtreg_spi_addr1 <= 10'h000;
	else if(tx_data_en & (rddata_cnt == 7))							tx1_rtreg_spi_addr1 <= tx_rt_reg_rddata[9:0];
	else															tx1_rtreg_spi_addr1 <= tx1_rtreg_spi_addr1;
	
	if(NT_CONFIG == RTREGCFG_IDLE)									tx1_rtreg_spi_txdata1 <= 32'h00000000;
	else if(tx_data_en & (rddata_cnt == 8))							tx1_rtreg_spi_txdata1 <= tx_rt_reg_rddata;
	else															tx1_rtreg_spi_txdata1 <= tx1_rtreg_spi_txdata1;

	if(NT_CONFIG == RTREGCFG_IDLE)									word_cnt <= 0;
	else if(NT_CONFIG == RTREGCFG_WORD)								word_cnt <= word_cnt + 1'b1;

	if(NT_CONFIG == RTREGCFG_IDLE)									rtreg_cfg_done <= 0;
	else if(NT_CONFIG == RTREGCFG_DONE)								rtreg_cfg_done <= 1;
end

endmodule

