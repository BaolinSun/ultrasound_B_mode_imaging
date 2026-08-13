module envelop_iq
(

	input									clk				,
	input									rst				,
	
	input									filter_headen	,
	input			[15:0]					filter_head		,
	input									filter_start	,
	input									filter_last		,
	input									filter_dataen	,
	input	 		[15:0]					filter_i		,
	input	 		[15:0]					filter_q		,
		
	output	reg								envelop_headen	,
	output	reg		[15:0]					envelop_head	,
	output	reg								envelop_start	,
	output	reg								envelop_last	,
	output	reg								envelop_dataen 	,
	output	reg		[15:0]					envelop_data
);
//---------------------------------------------------------------------------------
//               signal
//--------------------------------------------------------------------------------------------------
reg					[25:0]						dly_headen;
reg					[25:0][15:0]				dly_head;
reg					[25:0]						dly_start;
reg					[25:0]						dly_last;
reg					[25:0]						dly_dataen;

always@(posedge clk)
begin
	dly_headen	<=	{dly_headen	,	filter_headen	};
	dly_head	<=	{dly_head	,	filter_head		};
	dly_start	<=	{dly_start	,	filter_start	};
	dly_last	<=	{dly_last	,	filter_last		};
	dly_dataen	<=	{dly_dataen	,	filter_dataen	};
end

wire	[31:0]					mult_data0;
wire	[31:0]					mult_data1;

mult_16bx16b_d3 mult0(
	.CLK(clk),
	.A(filter_i), 
	.B(filter_i),
	.P(mult_data0)
);

mult_16bx16b_d3 mult1(
	.CLK(clk),
	.A(filter_q),
	.B(filter_q),
	.P(mult_data1)
);

reg [32:0] data_add;
always@(posedge clk)
begin
	data_add <=  {mult_data0[31],mult_data0} + {mult_data1[31],mult_data1} ; 
end

wire	[23:0]					square_data_d0;
wire	[16:0]					square_data;
square_33b_d17 square1(
	.aclk(clk),
	.s_axis_cartesian_tvalid(dly_dataen[3]),
	.s_axis_cartesian_tdata({7'd0,data_add}),
	.m_axis_dout_tvalid( ),
	.m_axis_dout_tdata(square_data_d0)
);

assign square_data = square_data_d0[16:0] ;

reg [15:0] data_cut;
always@(posedge clk)
begin
	if(square_data[16]=='b1)begin
		data_cut <= 'hffff ;
	end
	else begin
		data_cut <= square_data[15:0] ;
	end
end

//-------------------------------------------------------------------------------------------------- */
assign				envelop_headen			=	dly_headen[22];
assign				envelop_head			=	dly_head[22];
assign				envelop_start			=	dly_start[22];
assign				envelop_last			=	dly_last[22];
assign				envelop_dataen			=	dly_dataen[22];
assign				envelop_data			=	data_cut;

endmodule