//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
// `include "fpga_define.vh"
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
module bf_apod
(
	input										clk,
	input										reset,
	input										bf_rtc_bfst,
	input			[510:0]						bf_rtc_bfen,
	input			[5:0]						rtc_line,

	output	reg									bf_apod_data_rden,		//	静态参数
	output	reg		[5:0]						bf_apod_data_rdaddr, 	//	静态参数
	input			[7:0]						bf_apod_data_rddata,	//	静态参数
	
	output										apod_dataen,
	output	reg		[31:0][7:0]					apod_data
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg					[5:0]						bf_apod_data_rdcnt;
reg					[5:0]						bf_apod_data_rdcnt_t0;
reg					[5:0]						bf_apod_data_rdcnt_t1;
reg												bf_apod_data_rdy;

always@(posedge clk)
begin
	if(!bf_rtc_bfst)							bf_apod_data_rdcnt <= 0;
	else if(bf_apod_data_rdcnt <= 31)			bf_apod_data_rdcnt <= bf_apod_data_rdcnt + 1'b1;
	else										bf_apod_data_rdcnt <= 6'd32;

	bf_apod_data_rden <= (bf_rtc_bfst & (bf_apod_data_rdcnt < 32));
	bf_apod_data_rdaddr <= {rtc_line[0],bf_apod_data_rdcnt[4:0]};

	bf_apod_data_rdy <= bf_apod_data_rden;
	bf_apod_data_rdcnt_t0 <= bf_apod_data_rdcnt;
	bf_apod_data_rdcnt_t1 <= bf_apod_data_rdcnt_t0;
	
	if(bf_apod_data_rdy)	apod_data[bf_apod_data_rdcnt_t1] <= bf_apod_data_rddata;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------

assign				apod_dataen					=	bf_rtc_bfen[86];//254
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
