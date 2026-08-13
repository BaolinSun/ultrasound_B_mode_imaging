module add_rf_head
(
	input										clk,
	input										reset,
	input										rtc_enable,
	input										rtc_trig,
	input		[31:0]							rtc_rfdot,
	input		[6:0]							rtc_line,
	input		[3:0]							rtc_down,
	
	input										rf_dataen,
	input		[20:0]							rf_data,
	
	output	reg									trans_headen,
	output	reg	[15:0]							trans_head_d,
	output	reg									trans_start,
	output	reg									trans_last,
	output	reg									trans_dataen,
	output	reg	[19:0]							trans_data
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg												rtc_enable_t0;
reg												rtc_enable_t1;
reg												rtc_trig_t0;
reg												rtc_trig_t1;
reg				[31:0]							cnt_clk_time;
reg				[31:0]							cnt_10us;
reg				[31:0]							rtc_time;

always@(posedge clk)
begin
    rtc_enable_t0 <= rtc_enable;
    rtc_enable_t1 <= rtc_enable_t0;
	
    rtc_trig_t0 <= rtc_trig;
    rtc_trig_t1 <= rtc_trig_t0;
	
    if(reset)
        cnt_clk_time <= 0;
    else if(cnt_clk_time == (320*10-1))
        cnt_clk_time <= 0;
    else
        cnt_clk_time <= cnt_clk_time + 1'b1;
    
    if(reset)
        cnt_10us <= 0;
    else if(cnt_clk_time == (320*10-1))
        cnt_10us <= cnt_10us + 1'b1;
    
    if(rtc_trig_t0 & !rtc_trig_t1)
        rtc_time <= cnt_10us;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
reg				[15:0]							pack_head0;
reg				[15:0]							pack_head1;
reg				[15:0]							pack_head2;
reg				[15:0]							pack_head3;
reg				[15:0]							pack_head4;
reg				[15:0]							pack_head5;
reg				[15:0]							pack_head6;
reg				[15:0]							pack_head7;
reg				[5:0]							pack_updot_x1024;	//	填充1024

always@(posedge clk)
begin
	pack_updot_x1024 <= (rtc_rfdot[9:0] <= 1008) ? (rtc_rfdot[15:10] + 1) : (rtc_rfdot[15:10] + 2);
end

always@(posedge clk)
begin
	pack_head0 <= {pack_updot_x1024, 10'b0};//RF抽样后的实际点数
	pack_head1 <=  rtc_rfdot[15:0];			//RF采集的实际点数
	pack_head2 <= {9'd0,rtc_line[6:0]};		//发射线号
	pack_head3 <= {12'd0, rtc_down};		//抽取系数
	pack_head4 <= 16'd0;
	pack_head5 <= 16'd0;
	pack_head6 <= {rtc_time[31:16]};		//采集时间
	pack_head7 <= {rtc_time[15:0]};		//采集时间
end
//--------------------------------------------------------------------------------------------------
reg					[38:0]				dly_rf_dataen;
reg					[37:0][20:0]			dly_rf_data;
reg					[7:0]					flag_head;

always@(posedge clk)
begin
	dly_rf_dataen <= {dly_rf_dataen, rf_dataen};
	dly_rf_data   <= {dly_rf_data, rf_data};
	
	trans_start		<=	( dly_rf_dataen[29] & !dly_rf_dataen[30]);
	trans_last		<=	(!dly_rf_dataen[28] &  dly_rf_dataen[29]);
	trans_dataen	<=	( dly_rf_dataen[29]);
	trans_data		<=	( dly_rf_data[29]);
	
	flag_head <= {flag_head, (dly_rf_dataen[20] & (!dly_rf_dataen[21]))};
	trans_headen <= |flag_head;
	
	     if(flag_head[0])	trans_head_d <= pack_head0;
	else if(flag_head[1])	trans_head_d <= pack_head1;
	else if(flag_head[2])	trans_head_d <= pack_head2;
	else if(flag_head[3])	trans_head_d <= pack_head3;
	else if(flag_head[4])	trans_head_d <= pack_head4;
	else if(flag_head[5])	trans_head_d <= pack_head5;
	else if(flag_head[6])	trans_head_d <= pack_head6;
	else if(flag_head[7])	trans_head_d <= pack_head7;
end
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
