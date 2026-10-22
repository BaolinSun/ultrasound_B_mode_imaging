module tx_cfg_rtreg
(
    output wire power_idle,
	input             clk,
	input             reset,
	input      [6:0]  rtc_line,
	input             polarity,
	input             rtreg_cfg_en,
	output reg        rtreg_cfg_done,

	input      [31:0] reg18_base_tx0_0,
	input      [31:0] reg18_base_tx0_1,
	input      [31:0] reg18_base_tx1_0,
	input      [31:0] reg18_base_tx1_1,

	input      [4:0]  tx_rt_reg_num,
	output reg        tx_rt_reg_rden,
	output reg [13:0] tx_rt_reg_rdaddr,
	input      [31:0] tx_rt_reg_rddata,

	output reg        tx_rtreg_spi_trig,
	output reg        tx_rtreg_spi_mode,
	output reg [9:0]  tx0_rtreg_spi_addr0,
	output reg [31:0] tx0_rtreg_spi_txdata0,
	output reg [9:0]  tx0_rtreg_spi_addr1,
	output reg [31:0] tx0_rtreg_spi_txdata1,
	output reg [9:0]  tx1_rtreg_spi_addr0,
	output reg [31:0] tx1_rtreg_spi_txdata0,
	output reg [9:0]  tx1_rtreg_spi_addr1,
	output reg [31:0] tx1_rtreg_spi_txdata1,
	input             tx_spi_done,
	input             tx_spi_rdnext
);

reg rtreg_cfg_en_t0;
reg rtreg_cfg_en_t1;
wire rtreg_cfg_en_pos;

always@(posedge clk)
begin
	if(reset) begin
		rtreg_cfg_en_t0 <= 1'b0;
		rtreg_cfg_en_t1 <= 1'b0;
	end else begin
		rtreg_cfg_en_t0 <= rtreg_cfg_en;
		rtreg_cfg_en_t1 <= rtreg_cfg_en_t0;
	end
end

assign rtreg_cfg_en_pos = rtreg_cfg_en_t0 & !rtreg_cfg_en_t1;

localparam RTREGCFG_IDLE          = 4'd0;
localparam RTREGCFG_RDCYC         = 4'd1;
localparam RTREGCFG_RDRAM         = 4'd2;
localparam RTREGCFG_WORD          = 4'd3;
localparam RTREGCFG_WAIT0         = 4'd4;
localparam RTREGCFG_WAIT1         = 4'd5;
localparam RTREGCFG_INV_WAIT_DONE = 4'd6;
localparam RTREGCFG_INV_WORD      = 4'd7;
localparam RTREGCFG_INV_TRIG      = 4'd8;
localparam RTREGCFG_WAIT2         = 4'd9;
localparam RTREGCFG_DONE          = 4'd10;

reg [3:0] ST_CONFIG;
reg [3:0] NT_CONFIG;
reg [6:0] word_cnt;
reg [3:0] rddata_cnt;
reg       tx_data_en;
reg       polarity_latched;

always@(posedge clk)
begin
	if(reset)
		ST_CONFIG <= RTREGCFG_IDLE;
	else
		ST_CONFIG <= NT_CONFIG;
end

always@(*)
begin
	case(ST_CONFIG)
		RTREGCFG_IDLE:
			if(rtreg_cfg_en_pos) NT_CONFIG = RTREGCFG_RDCYC;
			else                 NT_CONFIG = RTREGCFG_IDLE;
		RTREGCFG_RDCYC:
			if(word_cnt == tx_rt_reg_num) NT_CONFIG = RTREGCFG_INV_WAIT_DONE;
			else                          NT_CONFIG = RTREGCFG_RDRAM;
		RTREGCFG_RDRAM:
			if(rddata_cnt == 7) NT_CONFIG = RTREGCFG_WORD;
			else                NT_CONFIG = RTREGCFG_RDRAM;
		RTREGCFG_WORD:          NT_CONFIG = RTREGCFG_WAIT0;
		RTREGCFG_WAIT0:         NT_CONFIG = RTREGCFG_WAIT1;
		RTREGCFG_WAIT1:
			if(tx_spi_rdnext) NT_CONFIG = RTREGCFG_RDCYC;
			else              NT_CONFIG = RTREGCFG_WAIT1;
		// tx_spi_rdnext is intentionally earlier than tx_spi_done. Wait for
		// the ninth ordinary write to finish before issuing register 18h.
		RTREGCFG_INV_WAIT_DONE:
			if(tx_spi_done) NT_CONFIG = RTREGCFG_INV_WORD;
			else            NT_CONFIG = RTREGCFG_INV_WAIT_DONE;
		RTREGCFG_INV_WORD: NT_CONFIG = RTREGCFG_INV_TRIG;
		RTREGCFG_INV_TRIG: NT_CONFIG = RTREGCFG_WAIT2;
		RTREGCFG_WAIT2:
			if(tx_spi_done) NT_CONFIG = RTREGCFG_DONE;
			else            NT_CONFIG = RTREGCFG_WAIT2;
		RTREGCFG_DONE: NT_CONFIG = RTREGCFG_IDLE;
		default:       NT_CONFIG = RTREGCFG_IDLE;
	endcase
end

always@(posedge clk)
begin
	if(reset) begin
		tx_rtreg_spi_trig <= 1'b0;
		tx_rt_reg_rden <= 1'b0;
		rddata_cnt <= 4'd0;
		tx_rt_reg_rdaddr <= 14'd0;
		tx_rtreg_spi_mode <= 1'b0;
		tx_data_en <= 1'b0;
		word_cnt <= 7'd0;
		rtreg_cfg_done <= 1'b0;
		polarity_latched <= 1'b0;
		tx0_rtreg_spi_addr0 <= 10'd0;
		tx0_rtreg_spi_txdata0 <= 32'd0;
		tx0_rtreg_spi_addr1 <= 10'd0;
		tx0_rtreg_spi_txdata1 <= 32'd0;
		tx1_rtreg_spi_addr0 <= 10'd0;
		tx1_rtreg_spi_txdata0 <= 32'd0;
		tx1_rtreg_spi_addr1 <= 10'd0;
		tx1_rtreg_spi_txdata1 <= 32'd0;
	end else begin
		tx_rtreg_spi_trig <= (NT_CONFIG == RTREGCFG_WAIT0) ||
		                       (NT_CONFIG == RTREGCFG_INV_TRIG);
		tx_rt_reg_rden <= (NT_CONFIG == RTREGCFG_RDRAM);
		tx_rtreg_spi_mode <= 1'b0;
		tx_data_en <= tx_rt_reg_rden;

		if(rtreg_cfg_en_pos) begin
			tx_rt_reg_rdaddr <= {rtc_line, 7'd0};
			polarity_latched <= polarity;
		end else if(tx_rt_reg_rden) begin
			tx_rt_reg_rdaddr <= tx_rt_reg_rdaddr + 1'b1;
		end

		if(NT_CONFIG == RTREGCFG_IDLE || NT_CONFIG == RTREGCFG_RDCYC)
			rddata_cnt <= 4'd0;
		else if(tx_rt_reg_rden)
			rddata_cnt <= rddata_cnt + 1'b1;

		if(NT_CONFIG == RTREGCFG_IDLE)
			word_cnt <= 7'd0;
		else if(NT_CONFIG == RTREGCFG_WORD)
			word_cnt <= word_cnt + 1'b1;

		if(NT_CONFIG == RTREGCFG_IDLE)
			rtreg_cfg_done <= 1'b0;
		else if(NT_CONFIG == RTREGCFG_DONE)
			rtreg_cfg_done <= 1'b1;

		if(tx_data_en && (rddata_cnt == 1))
			tx0_rtreg_spi_addr0 <= tx_rt_reg_rddata[9:0];
		if(tx_data_en && (rddata_cnt == 2))
			tx0_rtreg_spi_txdata0 <= tx_rt_reg_rddata;
		if(tx_data_en && (rddata_cnt == 3))
			tx0_rtreg_spi_addr1 <= tx_rt_reg_rddata[9:0];
		if(tx_data_en && (rddata_cnt == 4))
			tx0_rtreg_spi_txdata1 <= tx_rt_reg_rddata;
		if(tx_data_en && (rddata_cnt == 5))
			tx1_rtreg_spi_addr0 <= tx_rt_reg_rddata[9:0];
		if(tx_data_en && (rddata_cnt == 6))
			tx1_rtreg_spi_txdata0 <= tx_rt_reg_rddata;
		if(tx_data_en && (rddata_cnt == 7))
			tx1_rtreg_spi_addr1 <= tx_rt_reg_rddata[9:0];
		if(tx_data_en && (rddata_cnt == 8))
			tx1_rtreg_spi_txdata1 <= tx_rt_reg_rddata;

		// Register 18h is not stored in dynamic BRAM. Reproduce the captured
		// static value exactly and change only PAT_INV (bit 6).
		if(NT_CONFIG == RTREGCFG_INV_WORD) begin
			tx0_rtreg_spi_addr0 <= 10'h018;
			tx0_rtreg_spi_addr1 <= 10'h018;
			tx1_rtreg_spi_addr0 <= 10'h018;
			tx1_rtreg_spi_addr1 <= 10'h018;
			tx0_rtreg_spi_txdata0 <= reg18_base_tx0_0 |
			                            (polarity_latched ? 32'h0000_0040 : 32'd0);
			tx0_rtreg_spi_txdata1 <= reg18_base_tx0_1 |
			                            (polarity_latched ? 32'h0000_0040 : 32'd0);
			tx1_rtreg_spi_txdata0 <= reg18_base_tx1_0 |
			                            (polarity_latched ? 32'h0000_0040 : 32'd0);
			tx1_rtreg_spi_txdata1 <= reg18_base_tx1_1 |
			                            (polarity_latched ? 32'h0000_0040 : 32'd0);
		end
	end
end

assign power_idle=(ST_CONFIG==RTREGCFG_IDLE) && !rtreg_cfg_en && !rtreg_cfg_en_t0 && !rtreg_cfg_en_t1;
endmodule
