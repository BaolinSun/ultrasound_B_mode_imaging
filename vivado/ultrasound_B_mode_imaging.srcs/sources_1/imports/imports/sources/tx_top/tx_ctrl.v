module tx_ctrl
(
	input									clk,
	input									reset,

	input									tx_cfg_reset,			//- 1clk 脉冲自清除复位信号	
	output		reg							tx_reset,	 			//- tx管脚，reset
	output									tx_dsel0,				//-	tx管脚，dsel0
	output									tx_dsel1,				//-	tx管脚，dsel1
	output									ctrl_spi_rdy			//- SPI通信允许，高有效
);

reg											rst_spi_rdy;
//==================================================================================================
//==================================================================================================
localparam		RST_INIT					=	6'b00_0000;
localparam		RST_CLC1					=	6'b00_0001;
localparam		RST_ING						=	6'b00_0010;
localparam		RST_CLC2					=	6'b00_0100;
localparam		RST_WAIT					=	6'b01_0000;
localparam		RST_END						=	6'b10_0000;
reg				[5:0]						STATE_RST;
reg				[5:0]						NEXT_RST;
reg				[18:0]						cnt_rst = 0;

assign ctrl_spi_rdy = rst_spi_rdy;
//--------------------------------------------------------------------------------------------------

always@(posedge clk)
begin
	if(reset)
		STATE_RST <= RST_INIT;
	else
		STATE_RST <= NEXT_RST;
end

always @(*) 
begin
	case(STATE_RST)
		
		RST_INIT:		if(cnt_rst[8])							NEXT_RST = RST_CLC1;
						else									NEXT_RST = RST_INIT;
		RST_CLC1:												NEXT_RST = RST_ING;
		RST_ING:		if(cnt_rst[8])							NEXT_RST = RST_CLC2;
						else									NEXT_RST = RST_ING;
		RST_CLC2:												NEXT_RST = RST_WAIT;
		RST_WAIT:		if(cnt_rst[18])							NEXT_RST = RST_END;
						else									NEXT_RST = RST_WAIT;
		RST_END:		if(tx_cfg_reset)						NEXT_RST = RST_ING;
						else									NEXT_RST = RST_END;
		default:												NEXT_RST = RST_INIT;
	endcase	
	
end

always @(posedge clk) 
begin
	if(reset)													cnt_rst <= 18'd0;
	else if(NEXT_RST == RST_CLC1)								cnt_rst <= 18'd0;
	else if(NEXT_RST == RST_CLC2)								cnt_rst <= 18'd0;
	else if(NEXT_RST == RST_INIT)								cnt_rst <= cnt_rst[8] ? cnt_rst : cnt_rst + 1'b1;
	else if(NEXT_RST == RST_ING)								cnt_rst <= cnt_rst[8] ? cnt_rst : cnt_rst + 1'b1;
	else if(NEXT_RST == RST_WAIT)								cnt_rst <= cnt_rst[18] ? cnt_rst : cnt_rst + 1'b1;
	else														cnt_rst	<= 18'd0;
			
	if(reset)													tx_reset <= 1'b0;
	else if(NEXT_RST == RST_ING)								tx_reset <= 1'b1;
	else														tx_reset <= 1'b0;
			
	if(reset)													rst_spi_rdy <= 1'b0;
	else if(NEXT_RST == RST_ING)								rst_spi_rdy <= 1'b0;
	else if(NEXT_RST == RST_END)								rst_spi_rdy <= 1'b1;
	else														rst_spi_rdy <= rst_spi_rdy;

end

assign	tx_dsel0 = 1'b1;
assign	tx_dsel1 = 1'b1;
endmodule

 