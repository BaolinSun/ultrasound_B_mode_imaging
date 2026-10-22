//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
module tx_cfg_top
(
    output wire power_idle,
    input wire power_hold,
	input									clk,
	input									reset,

	input									scan_enable,
	input									tx_cfg_req,
	input			[6:0]					rtc_line,
	input									rtc_polarity,
	output									tx_cfg_ready,
	output									tx_cfg_done,
	input									spi_start,	//��ʼ������������ram�����
	input			[6:0]					tx_st_reg_num,
	output									tx_st_reg_rden,
	output			[8:0]					tx_st_reg_rdaddr,
	input			[31:0]					tx_st_reg_rddata,

	input			[4:0]					tx_rt_reg_num,
	output									tx_rt_reg_rden,
	output			[13:0]					tx_rt_reg_rdaddr,
	input			[31:0]					tx_rt_reg_rddata,
	
	input									tx_spi_rdreq,
	input			[9:0]					tx_spi_rdaddr,	
	output									tx_spi_rdaccept,
	
	output	reg								tx_spi_trig0,
	output	reg								tx_spi_mode0,
	output	reg		[9:0]					tx0_spi_addr0,
	output	reg		[31:0]					tx0_spi_txdata0,
	output	reg								tx_spi_trig1,
	output	reg								tx_spi_mode1,
	output	reg		[9:0]					tx0_spi_addr1,
	output	reg		[31:0]					tx0_spi_txdata1,
	output	reg		[9:0]					tx1_spi_addr0,
	output	reg		[31:0]					tx1_spi_txdata0,
	output	reg		[9:0]					tx1_spi_addr1,
	output	reg		[31:0]					tx1_spi_txdata1,
	input									tx_spi_done0,
	input									tx_spi_done1,
	input									tx_spi_rdnext
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire st_power_idle, rt_power_idle, rd_power_idle;
reg											scan_enable_t0;
reg											scan_enable_t1;
wire										scan_enable_pos;
wire										scan_enable_neg;
always@(posedge clk)
begin
	scan_enable_t0 <= scan_enable;
	scan_enable_t1 <= scan_enable_t0;
end
assign scan_enable_pos = (!scan_enable_t1) & scan_enable_t0;
assign scan_enable_neg = (!scan_enable_t0) & scan_enable_t1;
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg											spi_start_t0;
reg											spi_start_t1;
wire										spi_start_pos;
wire										spi_start_neg;
always@(posedge clk)
begin
	spi_start_t0 <= spi_start;
	spi_start_t1 <= spi_start_t0;
end
assign spi_start_pos = (!spi_start_t1) & spi_start_t0;
assign spi_start_neg = (!spi_start_t0) & spi_start_t1;
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg											streg_cfg_en;
wire										streg_cfg_done;
reg											rtreg_cfg_en;
wire										rtreg_cfg_done;
reg											rdreg_cfg_en;
wire										rdreg_cfg_done;
//--------------------------------------------------------------------------------------------------
localparam			CFG_IDLE			=	4'b0000;
localparam			CFG_STREG			=	4'b0001;
localparam			CFG_RTREG			=	4'b0010;
localparam			CFG_DONE			=	4'b0100;
localparam			CFG_RDREG			=	4'b1000;
//--------------------------------------------------------------------------------------------------
reg					[3:0]					ST_CONFIG;
reg					[3:0]					NT_CONFIG;

// A read request may wait here while static/dynamic configuration is active.
// Acknowledge it only on the cycle in which CFG_IDLE selects CFG_RDREG.
assign tx_spi_rdaccept = (ST_CONFIG == CFG_IDLE) && !spi_start_pos && tx_spi_rdreq;

always@(posedge clk)
begin
    // Power HOLD is asserted only after the real shifters/children drained.
    // Re-arm the controller without resetting SPI or the chip/register cache.
	if(reset | scan_enable_neg | (power_hold && power_idle))
		ST_CONFIG <= CFG_IDLE;
	else
		ST_CONFIG <= NT_CONFIG;
end

always @(*) 
begin
	case(ST_CONFIG)
		CFG_IDLE:	if(spi_start_pos)			NT_CONFIG = CFG_STREG;
					else if(tx_spi_rdreq)		NT_CONFIG = CFG_RDREG;
					else						NT_CONFIG = CFG_IDLE;
		CFG_STREG:	if(streg_cfg_done)			NT_CONFIG = CFG_RTREG;
					else						NT_CONFIG = CFG_STREG;
		CFG_RTREG:	if(scan_enable_neg)			NT_CONFIG = CFG_DONE;
					else						NT_CONFIG = CFG_RTREG;
		CFG_RDREG:	if(rdreg_cfg_done)			NT_CONFIG = CFG_DONE;
					else						NT_CONFIG = CFG_RDREG;
		CFG_DONE:								NT_CONFIG = CFG_IDLE;
		default:								NT_CONFIG = CFG_IDLE;
	endcase	
end
//==================================================================================================
//==================================================================================================
always @(posedge clk) 
begin
	if(NT_CONFIG == CFG_IDLE)								streg_cfg_en <= 1'b0;
	else if(NT_CONFIG == CFG_STREG)							streg_cfg_en <= 1'b1;
	else													streg_cfg_en <= 1'b0;

	if(NT_CONFIG == CFG_IDLE)								rtreg_cfg_en <= 1'b0;
	else if(NT_CONFIG == CFG_RTREG)							rtreg_cfg_en <= 1'b1;
	else													rtreg_cfg_en <= 1'b0;
	
	if(NT_CONFIG == CFG_IDLE)								rdreg_cfg_en <= 1'b0;
	else if(NT_CONFIG == CFG_RDREG)							rdreg_cfg_en <= 1'b1;
	else													rdreg_cfg_en <= 1'b0;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire									tx_streg_spi_trig;
wire									tx_streg_spi_mode;
wire		[9:0]						tx0_streg_spi_addr0;
wire		[31:0]						tx0_streg_spi_txdata0;
wire		[9:0]						tx0_streg_spi_addr1;
wire		[31:0]						tx0_streg_spi_txdata1;
wire		[9:0]						tx1_streg_spi_addr0;
wire		[31:0]						tx1_streg_spi_txdata0;
wire		[9:0]						tx1_streg_spi_addr1;
wire		[31:0]						tx1_streg_spi_txdata1;

reg			[31:0]					reg18_base_tx0_0;
reg			[31:0]					reg18_base_tx0_1;
reg			[31:0]					reg18_base_tx1_0;
reg			[31:0]					reg18_base_tx1_1;
reg			[3:0]					reg18_valid;

// Capture the actual static register 18h values independently for all four
// TX7332 devices. Dynamic PI writes only change PAT_INV (bit 6).
always@(posedge clk)
begin
	if(reset || spi_start_pos) begin
		reg18_base_tx0_0 <= 32'd0;
		reg18_base_tx0_1 <= 32'd0;
		reg18_base_tx1_0 <= 32'd0;
		reg18_base_tx1_1 <= 32'd0;
		reg18_valid <= 4'b0000;
	end else if(tx_streg_spi_trig) begin
		if(tx0_streg_spi_addr0 == 10'h018) begin
			reg18_base_tx0_0 <= tx0_streg_spi_txdata0 & 32'hFFFF_FFBF;
			reg18_valid[0] <= 1'b1;
		end
		if(tx0_streg_spi_addr1 == 10'h018) begin
			reg18_base_tx0_1 <= tx0_streg_spi_txdata1 & 32'hFFFF_FFBF;
			reg18_valid[1] <= 1'b1;
		end
		if(tx1_streg_spi_addr0 == 10'h018) begin
			reg18_base_tx1_0 <= tx1_streg_spi_txdata0 & 32'hFFFF_FFBF;
			reg18_valid[2] <= 1'b1;
		end
		if(tx1_streg_spi_addr1 == 10'h018) begin
			reg18_base_tx1_1 <= tx1_streg_spi_txdata1 & 32'hFFFF_FFBF;
			reg18_valid[3] <= 1'b1;
		end
	end
end

assign tx_cfg_ready = (&reg18_valid) && (ST_CONFIG == CFG_RTREG);
assign tx_cfg_done = rtreg_cfg_done;

tx_cfg_streg							tx_cfg_streg_i
(
    .power_idle(st_power_idle),
	.clk								(clk					),
	.reset								(reset					),
	.streg_cfg_en						(streg_cfg_en			),
	.streg_cfg_done						(streg_cfg_done			),
	.tx_st_reg_num						(tx_st_reg_num			),
	.tx_st_reg_rden						(tx_st_reg_rden			),
	.tx_st_reg_rdaddr					(tx_st_reg_rdaddr		),
	.tx_st_reg_rddata					(tx_st_reg_rddata		),
	.tx_streg_spi_trig					(tx_streg_spi_trig		),
	.tx_streg_spi_mode					(tx_streg_spi_mode		),
	.tx0_streg_spi_addr0				(tx0_streg_spi_addr0	),
	.tx0_streg_spi_txdata0				(tx0_streg_spi_txdata0	),
	.tx0_streg_spi_addr1				(tx0_streg_spi_addr1	),
	.tx0_streg_spi_txdata1				(tx0_streg_spi_txdata1	),
	.tx1_streg_spi_addr0				(tx1_streg_spi_addr0	),
	.tx1_streg_spi_txdata0				(tx1_streg_spi_txdata0	),
	.tx1_streg_spi_addr1				(tx1_streg_spi_addr1	),
	.tx1_streg_spi_txdata1				(tx1_streg_spi_txdata1	),
	.tx_spi_done						(tx_spi_done0			),
	.tx_spi_rdnext						(tx_spi_rdnext			)
);

//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire									tx_rtreg_spi_trig;
wire									tx_rtreg_spi_mode;			//	0=tx	1=rx
wire		[9:0]						tx0_rtreg_spi_addr0;
wire		[31:0]						tx0_rtreg_spi_txdata0;
wire		[9:0]						tx0_rtreg_spi_addr1;
wire		[31:0]						tx0_rtreg_spi_txdata1;
wire		[9:0]						tx1_rtreg_spi_addr0;
wire		[31:0]						tx1_rtreg_spi_txdata0;
wire		[9:0]						tx1_rtreg_spi_addr1;
wire		[31:0]						tx1_rtreg_spi_txdata1;

tx_cfg_rtreg							tx_cfg_rtreg_i
(
    .power_idle(rt_power_idle),
	.clk								(clk					),
	.reset								(reset					),
	.rtc_line							(rtc_line				),
	.polarity							(rtc_polarity			),
	.rtreg_cfg_en						(tx_cfg_req				),
	.rtreg_cfg_done						(rtreg_cfg_done			),
	.reg18_base_tx0_0					(reg18_base_tx0_0		),
	.reg18_base_tx0_1					(reg18_base_tx0_1		),
	.reg18_base_tx1_0					(reg18_base_tx1_0		),
	.reg18_base_tx1_1					(reg18_base_tx1_1		),
	.tx_rt_reg_num						(tx_rt_reg_num			),
	.tx_rt_reg_rden						(tx_rt_reg_rden			),
	.tx_rt_reg_rdaddr					(tx_rt_reg_rdaddr		),
	.tx_rt_reg_rddata					(tx_rt_reg_rddata		),
	.tx_rtreg_spi_trig					(tx_rtreg_spi_trig		),
	.tx_rtreg_spi_mode					(tx_rtreg_spi_mode		),
	.tx0_rtreg_spi_addr0				(tx0_rtreg_spi_addr0	),
	.tx0_rtreg_spi_txdata0				(tx0_rtreg_spi_txdata0	),
	.tx0_rtreg_spi_addr1				(tx0_rtreg_spi_addr1	),
	.tx0_rtreg_spi_txdata1				(tx0_rtreg_spi_txdata1	),
	.tx1_rtreg_spi_addr0				(tx1_rtreg_spi_addr0	),
	.tx1_rtreg_spi_txdata0				(tx1_rtreg_spi_txdata0	),
	.tx1_rtreg_spi_addr1				(tx1_rtreg_spi_addr1	),
	.tx1_rtreg_spi_txdata1				(tx1_rtreg_spi_txdata1	),
	.tx_spi_done						(tx_spi_done0			),
	.tx_spi_rdnext						(tx_spi_rdnext			)
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire									tx_rdreg_spi_trig0;
wire									tx_rdreg_spi_mode0;			//	0=tx	1=rx
wire									tx_rdreg_spi_trig1;
wire									tx_rdreg_spi_mode1;			//	0=tx	1=rx
wire		[9:0]						tx0_rdreg_spi_addr0;
wire		[31:0]						tx0_rdreg_spi_txdata0;
wire		[9:0]						tx0_rdreg_spi_addr1;
wire		[31:0]						tx0_rdreg_spi_txdata1;
wire		[9:0]						tx1_rdreg_spi_addr0;
wire		[31:0]						tx1_rdreg_spi_txdata0;
wire		[9:0]						tx1_rdreg_spi_addr1;
wire		[31:0]						tx1_rdreg_spi_txdata1;

tx_cfg_rdreg							tx_cfg_rdreg_i
(
    .power_idle(rd_power_idle),
	.clk								(clk					),
	.reset								(reset					),
	.rdreg_cfg_done						(rdreg_cfg_done			),
	.tx_spi_rdreq						(tx_spi_rdreq			),
	.tx_spi_rdaddr						(tx_spi_rdaddr		),
	.tx_rdreg_spi_trig0					(tx_rdreg_spi_trig0		),
	.tx_rdreg_spi_mode0					(tx_rdreg_spi_mode0		),
	.tx0_rdreg_spi_addr0				(tx0_rdreg_spi_addr0		),
	.tx0_rdreg_spi_txdata0				(tx0_rdreg_spi_txdata0	),
	.tx_rdreg_spi_trig1					(tx_rdreg_spi_trig1		),
	.tx_rdreg_spi_mode1					(tx_rdreg_spi_mode1		),
	.tx0_rdreg_spi_addr1				(tx0_rdreg_spi_addr1		),
	.tx0_rdreg_spi_txdata1				(tx0_rdreg_spi_txdata1	),
	.tx1_rdreg_spi_addr0				(tx1_rdreg_spi_addr0		),
	.tx1_rdreg_spi_txdata0				(tx1_rdreg_spi_txdata0	),
	.tx1_rdreg_spi_addr1				(tx1_rdreg_spi_addr1		),
	.tx1_rdreg_spi_txdata1				(tx1_rdreg_spi_txdata1	),
	.tx_spi_done0						(tx_spi_done0			),
	.tx_spi_done1						(tx_spi_done1			)
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
always@(posedge clk)
begin
	case({streg_cfg_en,rtreg_cfg_en,rdreg_cfg_en})
		3'b100:begin
			tx_spi_trig0 <= tx_streg_spi_trig;
			tx_spi_mode0 <= tx_streg_spi_mode;
			tx0_spi_addr0 <= tx0_streg_spi_addr0;
			tx0_spi_txdata0 <= tx0_streg_spi_txdata0;
			tx_spi_trig1 <= tx_streg_spi_trig;
			tx_spi_mode1 <= tx_streg_spi_mode;
			tx0_spi_addr1 <= tx0_streg_spi_addr1;
			tx0_spi_txdata1 <= tx0_streg_spi_txdata1;
			tx1_spi_addr0 <= tx1_streg_spi_addr0;
			tx1_spi_txdata0 <= tx1_streg_spi_txdata0;
			tx1_spi_addr1 <= tx1_streg_spi_addr1;
			tx1_spi_txdata1 <= tx1_streg_spi_txdata1;
		end
		3'b010:begin
			tx_spi_trig0 <= tx_rtreg_spi_trig;
			tx_spi_mode0 <= tx_rtreg_spi_mode;
			tx0_spi_addr0 <= tx0_rtreg_spi_addr0;
			tx0_spi_txdata0 <= tx0_rtreg_spi_txdata0;
			tx_spi_trig1 <= tx_rtreg_spi_trig;
			tx_spi_mode1 <= tx_rtreg_spi_mode;
			tx0_spi_addr1 <= tx0_rtreg_spi_addr1;
			tx0_spi_txdata1 <= tx0_rtreg_spi_txdata1;
			tx1_spi_addr0 <= tx1_rtreg_spi_addr0;
			tx1_spi_txdata0 <= tx1_rtreg_spi_txdata0;
			tx1_spi_addr1 <= tx1_rtreg_spi_addr1;
			tx1_spi_txdata1 <= tx1_rtreg_spi_txdata1;
		end
		3'b001:begin
			tx_spi_trig0 <= tx_rdreg_spi_trig0;
			tx_spi_mode0 <= tx_rdreg_spi_mode0;
			tx0_spi_addr0 <= tx0_rdreg_spi_addr0;
			tx0_spi_txdata0 <= tx0_rdreg_spi_txdata0;
			tx_spi_trig1 <= tx_rdreg_spi_trig1;
			tx_spi_mode1 <= tx_rdreg_spi_mode1;
			tx0_spi_addr1 <= tx0_rdreg_spi_addr1;
			tx0_spi_txdata1 <= tx0_rdreg_spi_txdata1;
			tx1_spi_addr0 <= tx1_rdreg_spi_addr0;
			tx1_spi_txdata0 <= tx1_rdreg_spi_txdata0;
			tx1_spi_addr1 <= tx1_rdreg_spi_addr1;
			tx1_spi_txdata1 <= tx1_rdreg_spi_txdata1;
		end
		default:begin
			tx_spi_trig0 <= 0;
			tx_spi_mode0 <= 0;
			tx0_spi_addr0 <= 0;
			tx0_spi_txdata0 <= 0;
			tx_spi_trig1 <= 0;
			tx_spi_mode1 <= 0;
			tx0_spi_addr1 <= 0;
			tx0_spi_txdata1 <= 0;
			tx1_spi_addr0 <= 0;
			tx1_spi_txdata0 <= 0;
			tx1_spi_addr1 <= 0;
			tx1_spi_txdata1 <= 0;
		end
	endcase
end


assign power_idle=(ST_CONFIG==CFG_IDLE || ST_CONFIG==CFG_RTREG) &&
    st_power_idle && rt_power_idle && rd_power_idle && !tx_cfg_req &&
    !spi_start_pos && !tx_spi_rdreq && !tx_spi_trig0 && !tx_spi_trig1;
endmodule
