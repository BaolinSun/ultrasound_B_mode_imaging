module tx_spi
(
    output wire power_idle,
	input									clk,
	input									reset,
	input									spi_trig,
	input									spi_mode,			//	0=tx	1=rx
	input			[9:0]					spi_addr,
	input			[31:0]					spi_txdata,
	output	reg		[31:0]					spi_rxdata,
	output	reg								spi_rxvalid,
	output									spi_done,
	output	reg								spi_rdnext,
	output									o_spi_sen,
	output									o_spi_sclk,
	output									o_spi_sdata,
	input									i_spi_sdout
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg											spi_trig_t0;
reg											spi_trig_t1;
reg											spi_trig_pos;

always@(posedge clk)
begin
	spi_trig_t0 <= spi_trig;
	spi_trig_t1 <= spi_trig_t0;
	spi_trig_pos <= spi_trig_t0 & (!spi_trig_t1);
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
localparam			WRSPI_IDLE				=	8'b0000_0000;
localparam			WRSPI_ADDR_L			=	8'b0000_0001;
localparam			WRSPI_ADDR_H			=	8'b0000_0010;
localparam			WRSPI_TX_L				=	8'b0000_0100;
localparam			WRSPI_TX_H				=	8'b0000_1000;
localparam			WRSPI_DONE				=	8'b0001_0000;
//--------------------------------------------------------------------------------------------------
reg					[7:0]					STATE_WRSPI;
reg					[7:0]					NEXT_WRSPI;
reg					[3:0]					cnt_addr_wrbit;
reg					[5:0]					cnt_data_wrbit;
reg					[2:0]					cnt_wrclk;
reg					[9:0]					spi_wraddr;
reg					[31:0]					spi_wrdata;
reg											spi_wrdata_en;
reg											spi_wrsen;
reg											spi_wrsclk;
reg											spi_wrsdata;
reg											spi_wrdone;
//--------------------------------------------------------------------------------------------------
always@(posedge clk)
begin
	if(reset)
		STATE_WRSPI <= WRSPI_IDLE;
	else
		STATE_WRSPI <= NEXT_WRSPI;
end

always@(*)
begin
	case(STATE_WRSPI)
		WRSPI_IDLE:	if(spi_trig_pos & (!spi_mode))								NEXT_WRSPI = WRSPI_ADDR_L;
					else														NEXT_WRSPI = WRSPI_IDLE;
		WRSPI_ADDR_L:	if(cnt_wrclk == 1)										NEXT_WRSPI = WRSPI_ADDR_H;
					else														NEXT_WRSPI = WRSPI_ADDR_L;
		WRSPI_ADDR_H:	if((cnt_wrclk == 2) & (cnt_addr_wrbit == 10))			NEXT_WRSPI = WRSPI_TX_L;
					else if(cnt_wrclk == 2)										NEXT_WRSPI = WRSPI_ADDR_L;
					else														NEXT_WRSPI = WRSPI_ADDR_H;
		WRSPI_TX_L:	if(cnt_wrclk == 1)											NEXT_WRSPI = WRSPI_TX_H;
					else														NEXT_WRSPI = WRSPI_TX_L;
		WRSPI_TX_H:	if((cnt_wrclk == 2) & (cnt_data_wrbit == 32) )				NEXT_WRSPI = WRSPI_DONE;
					else if(cnt_wrclk == 2)										NEXT_WRSPI = WRSPI_TX_L;
					else														NEXT_WRSPI = WRSPI_TX_H;
		WRSPI_DONE:	NEXT_WRSPI = WRSPI_IDLE;
		default:	NEXT_WRSPI = WRSPI_IDLE;
	endcase
end

always@(posedge clk)
begin
	if(NEXT_WRSPI == WRSPI_IDLE)			spi_wrsen <= 1'b1;
	else if(NEXT_WRSPI == WRSPI_DONE)		spi_wrsen <= 1'b1;
	else									spi_wrsen <= 1'b0;
	
	if(NEXT_WRSPI == WRSPI_ADDR_L)			spi_wrsclk <= 1'b0;
	else if(NEXT_WRSPI == WRSPI_TX_L)		spi_wrsclk <= 1'b0;
	else									spi_wrsclk <= 1'b1;
	
	if(NEXT_WRSPI == WRSPI_ADDR_L)			spi_wrsdata <= spi_wraddr[9];	// MSB
	else if(NEXT_WRSPI == WRSPI_TX_L)		spi_wrsdata <= spi_wrdata[31];	// MSB
	else if(NEXT_WRSPI == WRSPI_ADDR_H)		spi_wrsdata <= spi_wrsdata;
	else if(NEXT_WRSPI == WRSPI_TX_H)		spi_wrsdata <= spi_wrsdata;
	else									spi_wrsdata <= 1'b1;
end

always@(posedge clk)
begin
	if(reset)							spi_wrdone <= 1'b0;
	else if(NEXT_WRSPI == WRSPI_IDLE)	spi_wrdone <= 1'b0;
	else if(NEXT_WRSPI == WRSPI_DONE)	spi_wrdone <= 1'b1;

	if(spi_trig)						spi_wraddr <= spi_addr;
	else if(cnt_wrclk == 1)				spi_wraddr <= {spi_wraddr,1'b0};

	if(NEXT_WRSPI == WRSPI_IDLE)		spi_wrdata_en <= 1'b0;
	else if(NEXT_WRSPI == WRSPI_DONE)	spi_wrdata_en <= 1'b0;
	else if(NEXT_WRSPI == WRSPI_TX_L)	spi_wrdata_en <= 1'b1;

	if(spi_trig)						spi_wrdata <= spi_txdata;
	else if(spi_wrdata_en & cnt_wrclk == 1)	spi_wrdata <= {spi_wrdata,1'b0};

	if(NEXT_WRSPI == WRSPI_IDLE)		cnt_wrclk <= 0;
	else								cnt_wrclk <= (cnt_wrclk == 2) ? 1 : cnt_wrclk + 1'b1;

	if(NEXT_WRSPI == WRSPI_IDLE)		cnt_addr_wrbit <= 0;
	else if((NEXT_WRSPI == WRSPI_ADDR_H) & (cnt_wrclk == 1))	cnt_addr_wrbit <= cnt_addr_wrbit + 1'b1;

	if(NEXT_WRSPI == WRSPI_IDLE)		cnt_data_wrbit <= 0;
	else if((NEXT_WRSPI == WRSPI_TX_H) & (cnt_wrclk == 1))	cnt_data_wrbit <= cnt_data_wrbit + 1'b1;

	if(NEXT_WRSPI == WRSPI_IDLE)		spi_rdnext <= 1'b0;
	else if(cnt_data_wrbit == 30)		spi_rdnext <= 1'b1;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
localparam			RDSPI_IDLE				=	8'b0000_0000;
localparam			RDSPI_ADDR_L			=	8'b0000_0001;
localparam			RDSPI_ADDR_H			=	8'b0000_0010;
localparam			RDSPI_TX_L				=	8'b0000_0100;
localparam			RDSPI_TX_H				=	8'b0000_1000;
localparam			RDSPI_DONE				=	8'b0001_0000;
//--------------------------------------------------------------------------------------------------
reg					[7:0]					STATE_RDSPI;
reg					[7:0]					NEXT_RDSPI;
reg					[6:0]					cnt_rdbit;
reg					[5:0]					cnt_rdclk;
reg					[41:0]					spi_rdsave;
reg											spi_rdsen;
reg											spi_rdsclk;
reg											spi_rdsdata;
reg											spi_rddone;
//--------------------------------------------------------------------------------------------------
always@(posedge clk)
begin
	if(reset)
		STATE_RDSPI <= RDSPI_IDLE;
	else
		STATE_RDSPI <= NEXT_RDSPI;
end

always@(*)
begin
	NEXT_RDSPI = RDSPI_IDLE;

	case(STATE_RDSPI)
		RDSPI_IDLE:	if(spi_trig_pos & spi_mode)									NEXT_RDSPI = RDSPI_ADDR_L;
					else														NEXT_RDSPI = RDSPI_IDLE;
		RDSPI_ADDR_L:	if(cnt_rdclk == 8)										NEXT_RDSPI = RDSPI_ADDR_H;
					else														NEXT_RDSPI = RDSPI_ADDR_L;
		RDSPI_ADDR_H:	if((cnt_rdclk == 16) & (cnt_rdbit == 10))				NEXT_RDSPI = RDSPI_TX_L;
					else if(cnt_rdclk == 16)									NEXT_RDSPI = RDSPI_ADDR_L;
					else														NEXT_RDSPI = RDSPI_ADDR_H;
		RDSPI_TX_L:	if(cnt_rdclk == 8)											NEXT_RDSPI = RDSPI_TX_H;
					else														NEXT_RDSPI = RDSPI_TX_L;
		RDSPI_TX_H:	if((cnt_rdclk == 16) & (cnt_rdbit == 42))					NEXT_RDSPI = RDSPI_DONE;
					else if(cnt_rdclk == 16)									NEXT_RDSPI = RDSPI_TX_L;
					else														NEXT_RDSPI = RDSPI_TX_H;
		RDSPI_DONE:	NEXT_RDSPI = RDSPI_IDLE;
		default:	NEXT_RDSPI = RDSPI_IDLE;
	endcase
end

always@(posedge clk)
begin
	if(NEXT_RDSPI == RDSPI_IDLE)			spi_rdsen <= 1'b1;
	else if(NEXT_RDSPI == RDSPI_DONE)		spi_rdsen <= 1'b1;
	else									spi_rdsen <= 1'b0;
	
	if(NEXT_RDSPI == RDSPI_ADDR_L)			spi_rdsclk <= 1'b0;
	else if(NEXT_RDSPI == RDSPI_TX_L)		spi_rdsclk <= 1'b0;
	else									spi_rdsclk <= 1'b1;
	
	if(NEXT_RDSPI == RDSPI_ADDR_L)			spi_rdsdata <= spi_rdsave[41];	// MSB
	else if(NEXT_RDSPI == RDSPI_TX_L)		spi_rdsdata <= spi_rdsave[41];	// MSB
	else if(NEXT_RDSPI == RDSPI_ADDR_H)		spi_rdsdata <= spi_rdsdata;
	else if(NEXT_RDSPI == RDSPI_TX_H)		spi_rdsdata <= spi_rdsdata;
	else									spi_rdsdata <= 1'b1;
end

always@(posedge clk)
begin
	if(reset)							spi_rddone <= 1'b0;
	else if(NEXT_RDSPI == RDSPI_IDLE)	spi_rddone <= 1'b0;
	else if(NEXT_RDSPI == RDSPI_DONE)	spi_rddone <= 1'b1;

	if(spi_trig)						spi_rdsave <= {spi_addr,spi_txdata};
	else if(cnt_rdclk == 8)				spi_rdsave <= {spi_rdsave, 1'b0};

	if(NEXT_RDSPI == RDSPI_IDLE)		cnt_rdclk <= 0;
	else								cnt_rdclk <= (cnt_rdclk == 16) ? 1 : cnt_rdclk + 1'b1;

	if(NEXT_RDSPI == RDSPI_IDLE)		cnt_rdbit <= 0;
	else if(cnt_rdclk == 8)				cnt_rdbit <= cnt_rdbit + 1'b1;

	if((NEXT_RDSPI == RDSPI_TX_H)&(cnt_rdclk == 6'd11))		spi_rxdata <= {spi_rxdata[31:0], i_spi_sdout};
	else if(spi_trig_t0 & spi_mode)		spi_rxdata <= 32'd0;	

	if(NEXT_RDSPI == RDSPI_DONE)		spi_rxvalid <= 1'b1;
	else								spi_rxvalid <= 1'b0;	
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg								spi_sen;
reg								spi_sclk;
reg								spi_sdata;

always@(posedge clk)
begin
	spi_sen <= spi_wrsen & spi_rdsen;
	spi_sclk <= spi_wrsclk & spi_rdsclk;
	spi_sdata <= spi_wrsdata & spi_rdsdata;
end

assign							spi_done = spi_wrdone | spi_rddone;
assign							o_spi_sen = spi_sen;
assign							o_spi_sclk = spi_sclk;
assign							o_spi_sdata = spi_sdata;
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
// Actual shifters plus the trigger pipeline and registered chip select.
assign power_idle=(STATE_WRSPI==WRSPI_IDLE) && (STATE_RDSPI==RDSPI_IDLE) &&
    !spi_trig && !spi_trig_t0 && !spi_trig_t1 && !spi_trig_pos && o_spi_sen;
endmodule
