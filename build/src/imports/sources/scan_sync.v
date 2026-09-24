//////////////////////////////////////////////////////////////////////////////////
`include "fpga_define.vh"

module scan_sync
(
	input                            clk,
	input                            reset,

	input                            rtc_enable,
	input      [31:0]                rtc_period,
	input      [15:0]                rt_length,
	input      [15:0]                tx_start,
	input      [15:0]                tx_length,
	input      [15:0]                rx_start,
	input      [31:0]                rx_length,
	input      [31:0]                bf_start,
	input      [31:0]                bf_length,
	input                            tx_cfg_ready,
	input                            tx_cfg_done,
	output reg                       tx_cfg_req,
	output reg                       rtc_trig,
	output reg                       rtc_txen,
	output reg                       rtc_rxen,
	output reg                       rtc_bfen,
	output reg                       rtc_tgc_slope,
	output reg                       rtc_tgc_updown,
	output     [6:0]                 rtc_line,
	output reg [7:0]                 rtc_shot,
	output reg                       beamform_active
);

// Synchronize the PS run request into the 200 MHz scan-control clock domain.
(* ASYNC_REG = "TRUE" *) reg rtc_enable_sync_ff1;
(* ASYNC_REG = "TRUE" *) reg rtc_enable_sync_ff2;

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

reg [31:0] tx_period;
reg [31:0] rx_period;
reg [31:0] bf_period;

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

localparam SYNC_IDLE      = 3'b000;
localparam SYNC_PREP_REQ  = 3'b001;
localparam SYNC_PREP_WAIT = 3'b010;
localparam SYNC_RUN       = 3'b011;
localparam SYNC_DONE      = 3'b100;

reg [2:0]  ST_SYNC;
reg [2:0]  NT_SYNC;
reg [34:0] cnt_clk;
reg        flag_rtc;

// Four physical shots form one PC-visible line:
// shot[1:0] = 0:A+, 1:A-, 2:B+, 3:B-.
// Existing delay and aperture tables continue to see the legacy 0..127 index.
assign rtc_line = rtc_shot[7:1];

always@(*)
begin
	NT_SYNC = SYNC_IDLE;
	case(ST_SYNC)
		SYNC_IDLE: begin
			if(rtc_enable_sync_ff2 && tx_cfg_ready)
				NT_SYNC = SYNC_PREP_REQ;
			else
				NT_SYNC = SYNC_IDLE;
		end
		SYNC_PREP_REQ:
			NT_SYNC = SYNC_PREP_WAIT;
		SYNC_PREP_WAIT: begin
			if(tx_cfg_done)
				NT_SYNC = SYNC_RUN;
			else
				NT_SYNC = SYNC_PREP_WAIT;
		end
		SYNC_RUN: begin
			if(cnt_clk >= {rtc_period, 3'b010}) begin
				// Honor stop only after B- of logical line 63.
				if((rtc_shot == 8'd255) && !rtc_enable_sync_ff2)
					NT_SYNC = SYNC_IDLE;
				else
					NT_SYNC = SYNC_DONE;
			end else begin
				NT_SYNC = SYNC_RUN;
			end
		end
		SYNC_DONE:
			NT_SYNC = SYNC_PREP_REQ;
		default:
			NT_SYNC = SYNC_IDLE;
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
		tx_cfg_req <= 1'b0;
	end else begin
		if(NT_SYNC == SYNC_RUN) begin
			if(ST_SYNC == SYNC_RUN)
				cnt_clk <= cnt_clk + 1'b1;
			else
				cnt_clk <= 35'd3;
		end else begin
			cnt_clk <= 35'd2;
		end

		flag_rtc <= (NT_SYNC == SYNC_RUN);
		// One 200 MHz clock request pulse. TX configuration acknowledges only
		// after all nine BRAM groups plus the synthetic PAT_INV write finish.
		tx_cfg_req <= (NT_SYNC == SYNC_PREP_REQ);
	end
end

always@(posedge clk)
begin
	if(reset) begin
		beamform_active <= 1'b0;
	end else begin
		if((ST_SYNC == SYNC_IDLE) && (NT_SYNC == SYNC_PREP_REQ))
			beamform_active <= 1'b1;
		else if((ST_SYNC == SYNC_RUN) && (NT_SYNC == SYNC_IDLE))
			beamform_active <= 1'b0;
	end
end

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
		else if((cnt_clk > ({rx_period, 3'b010}-35'd20)) & (cnt_clk <= {rx_period, 3'b010}))
			rtc_tgc_updown <= 1'b1;
		else
			rtc_tgc_updown <= 1'b0;
	end
end

always@(posedge clk)
begin
	if(reset)
		rtc_shot <= 8'd0;
	else if((ST_SYNC == SYNC_IDLE) && (NT_SYNC == SYNC_PREP_REQ))
		rtc_shot <= 8'd0;
	else if((ST_SYNC == SYNC_RUN) && (NT_SYNC == SYNC_DONE))
		rtc_shot <= rtc_shot + 8'd1;
end

endmodule
