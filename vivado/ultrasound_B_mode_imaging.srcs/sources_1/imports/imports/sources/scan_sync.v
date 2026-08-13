//////////////////////////////////////////////////////////////////////////////////
`include "fpga_define.vh"

module scan_sync
(
	input										clk,
	input										reset,

	input										rtc_enable,
	input		 [31:0]							rtc_period,
	input		 [15:0]							rt_length,
	input		 [15:0]							tx_start,
	input		 [15:0]							tx_length,
	input		 [15:0]							rx_start,
	input		 [31:0]							rx_length,
	input		 [31:0]							bf_start,
	input		 [31:0]							bf_length,
	output	  reg								rtc_trig,
	output	  reg								rtc_txen,
	output	  reg								rtc_rxen,
	output	  reg								rtc_bfen,
	output	  reg								rtc_tgc_slope,
	output	  reg								rtc_tgc_updown,
	output	  reg	 [6:0]						rtc_line,
	output	  reg								beamform_active
);
//--------------------------------------------------------------------------------------------------
// Synchronize the PS run request into the 200 MHz scan-control clock domain.
//--------------------------------------------------------------------------------------------------
(* ASYNC_REG = "TRUE" *) reg								rtc_enable_sync_ff1;
(* ASYNC_REG = "TRUE" *) reg								rtc_enable_sync_ff2;

always@(posedge clk)
begin
	if(reset) begin
		rtc_enable_sync_ff1 <= 1'b0;
		rtc_enable_sync_ff2 <= 1'b0;
	end else begin
		rtc_enable_sync_ff1 <= rtc_enable;
		rtc_enable_sync_ff2 <= rtc_enable_sync_ff1;
	end
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg				   [31:0]						tx_period;
reg				   [31:0]						rx_period;
reg				   [31:0]						bf_period;

always@(posedge clk)
begin
	if(reset) begin
		tx_period <= 32'd0;
		rx_period <= 32'd0;
		bf_period <= 32'd0;
	end else begin
		tx_period <= tx_start + tx_length;
		rx_period <= rx_start + rx_length;
		bf_period <= bf_start + bf_length;
	end
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
localparam		  SYNC_IDLE						=	2'b00;
localparam		  SYNC_RUN						=	2'b01;
localparam		  SYNC_DONE						=	2'b10;
//--------------------------------------------------------------------------------------------------
reg				   [1:0]						ST_SYNC;
reg				   [1:0]						NT_SYNC;
reg				   [34:0]						cnt_clk;
reg												flag_rtc;
reg												stop_pending;

always@(*)
begin
	NT_SYNC = SYNC_IDLE;
	case(ST_SYNC)
		SYNC_IDLE:	if(rtc_enable_sync_ff2)									NT_SYNC = SYNC_RUN;
					else														NT_SYNC = SYNC_IDLE;
		SYNC_RUN:	if(cnt_clk >= {rtc_period, 3'b010}) begin
						// Finish the complete 128-line frame before freezing.
						if((rtc_line == 7'd127) && !rtc_enable_sync_ff2)		NT_SYNC = SYNC_IDLE;
						else													NT_SYNC = SYNC_DONE;
					end else														NT_SYNC = SYNC_RUN;
		SYNC_DONE:																NT_SYNC = SYNC_RUN;
		default:																NT_SYNC = SYNC_IDLE;
	endcase
end

always@(posedge clk)
begin
	if(reset)
		ST_SYNC <= SYNC_IDLE;
	else
		ST_SYNC <= NT_SYNC;
end

always@(posedge clk)
begin
	if(reset) begin
		cnt_clk <= 35'd2;
		flag_rtc <= 1'b0;
	end else begin
		if(NT_SYNC == SYNC_IDLE)		cnt_clk <= 35'd2;
		else if(NT_SYNC == SYNC_RUN)	cnt_clk <= cnt_clk + 1'b1;
		else if(NT_SYNC == SYNC_DONE)	cnt_clk <= 35'd3;

		if(NT_SYNC == SYNC_IDLE)		flag_rtc <= 1'b0;
		else							flag_rtc <= 1'b1;
	end
end

always@(posedge clk)
begin
	if(reset) begin
		beamform_active <= 1'b0;
		stop_pending <= 1'b0;
	end else begin
		if((ST_SYNC == SYNC_IDLE) && (NT_SYNC == SYNC_RUN))
			beamform_active <= 1'b1;
		else if((ST_SYNC == SYNC_RUN) && (NT_SYNC == SYNC_IDLE))
			beamform_active <= 1'b0;

		if(ST_SYNC == SYNC_IDLE)
			stop_pending <= 1'b0;
		else if(rtc_enable_sync_ff2)
			stop_pending <= 1'b0;
		else
			stop_pending <= 1'b1;
	end
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
always@(posedge clk)
begin
	if(reset)
		rtc_trig <= 1'b0;
	else if(!flag_rtc)
		rtc_trig <= 1'b0;
	else if(cnt_clk <= {rt_length, 3'b010})
		rtc_trig <= 1'b1;
	else
		rtc_trig <= 1'b0;
end
//--------------------------------------------------------------------------------------------------
always@(posedge clk)
begin
	if(reset) begin
		rtc_txen <= 1'b0;
		rtc_rxen <= 1'b0;
		rtc_bfen <= 1'b0;
		rtc_tgc_slope <= 1'b0;
		rtc_tgc_updown <= 1'b0;
	end else begin
		if(!flag_rtc)
			rtc_txen <= 1'b0;
		else if((cnt_clk > {tx_start, 3'b010}) & (cnt_clk <= {tx_period, 3'b010}))
			rtc_txen <= 1'b1;
		else
			rtc_txen <= 1'b0;

		if(!flag_rtc)
			rtc_rxen <= 1'b0;
		else if((cnt_clk > {rx_start, 3'b010}) & (cnt_clk <= {rx_period, 3'b010}))
			rtc_rxen <= 1'b1;
		else
			rtc_rxen <= 1'b0;

		if(!flag_rtc)
			rtc_bfen <= 1'b0;
		else if((cnt_clk > {bf_start, 3'b010}) & (cnt_clk <= {bf_period, 3'b010}))
			rtc_bfen <= 1'b1;
		else
			rtc_bfen <= 1'b0;

		if(!flag_rtc)
			rtc_tgc_slope <= 1'b0;
		else if((cnt_clk > {rx_start, 3'b010}) & (cnt_clk <= ({rx_start, 3'b010}+35'd20)))
			rtc_tgc_slope <= 1'b1;
		else
			rtc_tgc_slope <= 1'b0;

		if(!flag_rtc)
			rtc_tgc_updown <= 1'b0;
		else if((cnt_clk > ({rx_period, 3'b010}-35'd20)) & (cnt_clk <= ({rx_period, 3'b010})))
			rtc_tgc_updown <= 1'b1;
		else
			rtc_tgc_updown <= 1'b0;
	end
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												rtc_trig_t0;
reg												rtc_trig_t1;
reg												rtc_trig_pos;
reg												rtc_trig_neg;

always@(posedge clk)
begin
	if(reset) begin
		rtc_trig_t0 <= 1'b0;
		rtc_trig_t1 <= 1'b0;
		rtc_trig_pos <= 1'b0;
		rtc_trig_neg <= 1'b0;
	end else begin
		rtc_trig_t0 <= rtc_trig;
		rtc_trig_t1 <= rtc_trig_t0;
		rtc_trig_pos <= ( rtc_trig_t0 & !rtc_trig_t1);
		rtc_trig_neg <= (!rtc_trig_t0 &  rtc_trig_t1);
	end
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
always@(posedge clk)
begin
	if(reset)												rtc_line <= 7'd127;
	else if((ST_SYNC == SYNC_IDLE) && (NT_SYNC == SYNC_RUN))	rtc_line <= 7'd127;
	else if(rtc_trig_pos)									rtc_line <= rtc_line + 7'd1;
	else													rtc_line <= rtc_line;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
