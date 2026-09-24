module log_compress
(

	input									clk,
	input									rst,
	
	output	 reg							log1_coef_rden,
	output	 reg	[ 9:0]					log1_coef_rdaddr,
	input			[15:0]					log1_coef_rddata,
	output	 reg							log2_coef_rden,
	output	 reg	[ 5:0]					log2_coef_rdaddr,
	input			[15:0]					log2_coef_rddata,
	
	input									down_headen,
	input			[15:0]					down_head,
	input									down_start,
	input									down_last,
	input									down_dataen,
	input	 		[15:0]					down_data,
		
	output	reg								log_headen,
	output	reg		[15:0]					log_head,
	output	reg								log_start,
	output	reg								log_last,
	output	reg								log_dataen,
	output	reg		[15:0]					log_data
);
//-------------------------------------------------------
reg					[15:0]					dly_headen;
reg					[15:0][15:0]			dly_head;
reg					[15:0]					dly_start;
reg					[15:0]					dly_last;
reg					[15:0]					dly_dataen;
reg					[15:0][15:0]			dly_data;

always@(posedge clk)
begin
	dly_headen	<=	{dly_headen	,	down_headen	};
	dly_head	<=	{dly_head	,	down_head	};
	dly_start	<=	{dly_start	,	down_start	};
	dly_last	<=	{dly_last	,	down_last	};
	dly_dataen	<=	{dly_dataen	,	down_dataen	};
	dly_data	<=	{dly_data	,	down_data	};
end

reg		[5:0]				N;
always@(posedge clk)
begin
	casex(down_data[15:10]) 
		6'b1xxxxx	: N <= 6'd6 ; 
		6'b01xxxx	: N <= 6'd5 ; 
		6'b001xxx	: N <= 6'd4 ; 
		6'b0001xx	: N <= 6'd3 ; 
		6'b00001x	: N <= 6'd2 ; 
		6'b000001	: N <= 6'd1 ; 
		6'b000000	: N <= 6'd0 ; 
		default		: N <= 6'd0 ;
	endcase
end

reg		[9:0]				shift_data;
reg		[9:0]				r_log2_rdaddr;
always@(posedge clk)
begin
	case(N) 
		6'd0	: begin shift_data <= dly_data[0][ 9:0];r_log2_rdaddr <= 6'd0   ; end
		6'd1	: begin shift_data <= dly_data[0][10:1];r_log2_rdaddr <= 6'd1   ; end
		6'd2	: begin shift_data <= dly_data[0][11:2];r_log2_rdaddr <= 6'd3   ; end
		6'd3	: begin shift_data <= dly_data[0][12:3];r_log2_rdaddr <= 6'd7   ; end
		6'd4	: begin shift_data <= dly_data[0][13:4];r_log2_rdaddr <= 6'd15  ; end
		6'd5	: begin shift_data <= dly_data[0][14:5];r_log2_rdaddr <= 6'd31  ; end
		6'd6	: begin shift_data <= dly_data[0][15:6];r_log2_rdaddr <= 6'd63  ; end
 		default	: begin shift_data <= dly_data[0][ 9:0];r_log2_rdaddr <= 6'd0   ; end
	endcase
end

always@(posedge clk)
begin
	log1_coef_rden   <= dly_dataen[1];
	log1_coef_rdaddr <= shift_data;
end

always@(posedge clk)
begin
	log2_coef_rden   <= dly_dataen[1];
	log2_coef_rdaddr <= r_log2_rdaddr;
end

// log1 + log2 overflows 16 bits. With the shipped table the maximum is
// log1[1023] + log2[63] = 0xC0A9 + 0x7398 = 0x13441, which truncated to 16 bits
// becomes 0x3441 and folds the brightest echoes back into the dark end of the
// image. The wrap threshold is 16384*log10(x) = 65535, i.e. any envelope >= 10000
// out of the 65535 the envelope word can carry. Saturate instead, so a strong
// specular reflector clips white rather than inverting to black.
wire [16:0] w_sum ;
wire [15:0] w_data ;
assign w_sum  = {1'b0, log1_coef_rddata} + {1'b0, log2_coef_rddata};
assign w_data = w_sum[16] ? 16'hFFFF : w_sum[15:0];

assign				log_headen			=	dly_headen[3];
assign				log_head			=	dly_head[3];
assign				log_start			=	dly_start[3];
assign				log_last			=	dly_last[3];
assign				log_dataen			=	dly_dataen[3];
assign				log_data			=	w_data;

//-------------------------------------------------------------------------------	
endmodule
