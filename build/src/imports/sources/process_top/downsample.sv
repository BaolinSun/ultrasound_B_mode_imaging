module downsample
(
	input									clk,
	input									rst,
	input									env_headen,
	input			[15:0]					env_head,
	input									env_start,
	input									env_last,
	input									env_dataen,
	input	 		[15:0]					env_data,

	output	reg								down_headen,
	output	reg		[15:0]					down_head,
	output	reg								down_start,
	output	reg								down_last,
	output	reg								down_dataen,
	output	reg		[15:0]					down_data
);
//---------------------------------------------------------------------------------
//               signal
//---------------------------------------------------------------------------------
reg			[15:0]				rtc_rfdot;
//-------------------------------------------------------
//---------------------------头部信息解析
reg			[2:0]				cnt_head;
reg			[6:0]				DecFactor;

always@(posedge clk)
begin
	cnt_head <= env_headen ? cnt_head + 1'b1 : 0;
	
	if(env_headen & (cnt_head == 1))	
	begin
		rtc_rfdot	 <=	env_head[15:0];	 //有效点数
	end	
	if(env_headen & (cnt_head == 3))	
	begin
		DecFactor	 <=	env_head[3:0];	 //降采样率
	end	
end	

//-------------------------------------------------------
reg					[1:0]					dly_headen;
reg					[1:0][31:0]				dly_head;
reg					[1:0]					dly_start;
reg					[1:0]					dly_last;
reg					[1:0]					dly_dataen;
reg					[1:0][15:0]				dly_data;

always@(posedge clk)
begin
	dly_headen	<=	{dly_headen	,	env_headen	};
	dly_head	<=	{dly_head	,	env_head	};
	dly_start	<=	{dly_start	,	env_start	};
	dly_last	<=	{dly_last	,	env_last	};
	dly_dataen	<=	{dly_dataen	,	env_dataen	};
	dly_data	<=	{dly_data	,	env_data	};
end

reg[3:0]					cnt_dot;
always@(posedge clk)
begin
	if(!env_dataen)			cnt_dot <= 4'd0;
	else					cnt_dot <= cnt_dot +4'd1;
end

reg							down_sel;
always@(posedge clk)
begin
	if(env_dataen) begin
		case(DecFactor)
		4'd0:down_sel <= 1'b1;
		4'd1:down_sel <= (cnt_dot[0] == 0) ? 1'b1:1'b0;
		4'd2:down_sel <= (cnt_dot[1:0] == 0) ? 1'b1:1'b0;
		4'd3:down_sel <= (cnt_dot[2:0] == 0) ? 1'b1:1'b0;
		4'd4:down_sel <= (cnt_dot[3:0] == 0) ? 1'b1:1'b0;
		default:down_sel <= (cnt_dot[1:0] == 0) ? 1'b1:1'b0;
		endcase
	end
	else down_sel <= 1'b0;
end

//-------------------------------------------------------------------------------	
assign				down_headen			=	dly_headen[0];
assign				down_head			=	dly_head[0];
assign				down_start			=	dly_start[0];
assign				down_last			=	dly_last[0];
assign				down_dataen			=	down_sel;
assign				down_data			=	dly_data[0];

//----------------------------------------------------------------------------------

endmodule