`include "fpga_define.vh"

module apt_add
(

	input							clk,
	input							rst,
	
	input							rtc_enable,
	
	input							trans_headen,
	input			[15:0]			trans_head,
	input							trans_start,
	input							trans_last,
	input							trans_dataen,
	input	signed	[19:0]			trans_data,

	output	reg						apt_headen,
	output	reg		[15:0]			apt_head,
	output	reg						apt_start,
	output	reg						apt_last,
	output	reg						apt_dataen,
	output	reg		[31:0]			apt_data
);

//--------------------------------------------------------
//---------------------------头部信息解析
reg			[2:0]				cnt_head;
reg			[6:0]				trans_line;
always@(posedge clk)
begin
	cnt_head <= trans_headen ? cnt_head + 1'b1 : 0;
	if(trans_headen & (cnt_head == 2))
	begin
		trans_line <=  trans_head[6:0];
	end
end

reg							fifo_wren;
reg		[31:0]				fifo_wrdata;
reg							fifo_rden;
wire	[31:0]				fifo_rddata;
wire						fifo_full;
wire						fifo_empty;

always@(posedge clk)
begin
	if(!trans_line[0]) begin
		fifo_wren <= trans_dataen;
		fifo_wrdata <= {{12{trans_data[19]}},trans_data};
		fifo_rden <= 1'b0;
	end
	else begin
		fifo_wren <= 1'b0;
		fifo_wrdata <= 32'd0;
		fifo_rden <= trans_dataen;
	end
end

fifo_32bx8192					fifo_apt_i
(
	.clk						(clk),
	.rst						(rst),
	.din						(fifo_wrdata),
	.wr_en						(fifo_wren),
	.rd_en						(fifo_rden),
	.dout						(fifo_rddata),
	.full						(fifo_full),
	.empty						(fifo_empty)
);

reg	signed[31:0]						trans_data_d0;
reg	signed[31:0]						trans_data_d1;
reg	signed[31:0]						trans_data_d2;
reg	signed[31:0]						fifo_rddata_d1;
always@(posedge clk)
begin
	trans_data_d0 <= {{12{trans_data[19]}},trans_data};
	trans_data_d1 <= trans_data_d0;
	trans_data_d2 <= trans_data_d1;
	fifo_rddata_d1 <= fifo_rddata;
end

reg		[5:0]					trans_dataen_dly;
reg		[5:0]					trans_headen_dly;
reg		[5:0]					trans_start_dly;
reg		[5:0]					trans_last_dly;
reg		[5:0][15:0]				trans_head_dly;

always@(posedge clk)
begin
	trans_dataen_dly <= {trans_dataen_dly,trans_dataen};
	trans_headen_dly <= {trans_headen_dly,trans_headen};
	trans_start_dly <= {trans_start_dly,trans_start};
	trans_last_dly <= {trans_last_dly,trans_last};
	trans_head_dly <= {trans_head_dly,trans_head};
end

always@(posedge clk)
begin
	if(trans_line[0]) begin
		apt_data <= fifo_rddata_d1 + trans_data_d2;
		apt_dataen <= trans_dataen_dly[2];
		apt_headen <= trans_headen_dly[2];
		apt_start <= trans_start_dly[2];
		apt_last <= trans_last_dly[2];
		apt_head <= trans_head_dly[2];
	end
	else begin
		apt_data <= 32'd0;
		apt_dataen <= 1'b0;
		apt_headen <= 1'b0;
		apt_start <= 1'b0;
		apt_last <= 1'b0;
		apt_head <= 16'd0;
	end
end
//--------------------------------------------------------------------------------


endmodule