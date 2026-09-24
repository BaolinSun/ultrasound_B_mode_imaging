//--------------------------------------------------------------------------------------------------
`include "fpga_define.vh"
//--------------------------------------------------------------------------------------------------
module bproc_top
(
	input										clk_25,
	input										reset,
	input										rtc_trig,
	input										rtc_rxen,
	input		[7:0]							rtc_shot,

	output										demod_sin_rden,
	output		[12:0]							demod_sin_rdaddr,
	input		[31:0]							demod_sin_rddata,
	output										demod_cos_rden,
	output		[12:0]							demod_cos_rdaddr,
	input		[31:0]							demod_cos_rddata,

	output										log1_coef_rden,
	output		[9:0]							log1_coef_rdaddr,
	input		[31:0]							log1_coef_rddata,
	output										log2_coef_rden,
	output		[5:0]							log2_coef_rdaddr,
	input		[31:0]							log2_coef_rddata,

	output										filter_coef_rden,
	output		[5:0]							filter_coef_rdaddr,
	input		[15:0]							filter_coef_rddata,
	
	input										scan_enable,
	input										rtc_enable,
	input		[31:0]							rtc_rfdot,
	input		[31:0]							rtc_period,
	input		[31:0]							rt_length,
	input		[31:0]							tx_start,
	input		[31:0]							tx_length,
	input		[31:0]							rx_start,
	input		[31:0]							rx_length,
	input		[31:0]							bf_start,
	input		[31:0]							bf_length,
	input		[3:0]							rtc_down,
	input		[3:0]							gain_shift,

	input										rf_dataen,
	input		[20:0]							rf_data,
	output										log_headen,
	output		[15:0]							log_head,
	output										log_start,
	output										log_last,
	output										log_dataen,
	output		[15:0]							log_data

);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
wire											trans_headen;
wire	[15:0]									trans_head;
wire											trans_start;
wire											trans_last;
wire											trans_dataen;
wire	[20:0]									trans_data;
wire	[1:0]									trans_phase;

add_rf_head										add_rf_head_i
(
	.clk										(clk_25			),
	.reset										(reset			),
	.rtc_enable									(rtc_enable		),
	.rtc_trig									(rtc_trig		),
	.rtc_rfdot									(rtc_rfdot		),
	.rtc_shot									(rtc_shot		),
	.rtc_down									(rtc_down		),
	.rf_dataen									(rf_dataen		),
	.rf_data									(rf_data		),
	.trans_headen								(trans_headen	),
	.trans_head_d								(trans_head		),
	.trans_start								(trans_start	),
	.trans_last									(trans_last		),
	.trans_dataen								(trans_dataen	),
	.trans_data									(trans_data		),
	.trans_phase								(trans_phase	)
);
//--------------------------------------------------------------------------------------------------
wire											apt_headen;
wire	[15:0]									apt_head;
wire											apt_start;
wire											apt_last;
wire											apt_dataen;
wire	[31:0]									apt_data;

apt_add											apt_add_i
(
	.clk										(clk_25			),
	.rst										(reset			),
	.rtc_enable									(rtc_enable		),
	.trans_phase								(trans_phase	),
	.trans_headen								(trans_headen	),
	.trans_head									(trans_head		),
	.trans_start								(trans_start	),
	.trans_last									(trans_last		),
	.trans_dataen								(trans_dataen	),
	.trans_data									(trans_data		),
	.apt_headen									(apt_headen		),
	.apt_head									(apt_head		),
	.apt_start									(apt_start		),
	.apt_last									(apt_last		),
	.apt_dataen									(apt_dataen		),
	.apt_data									(apt_data		)
);
//--------------------------------------------------------------------------------------------------
wire											demod_headen;
wire	[15:0]									demod_head;
wire											demod_start;
wire											demod_last;
wire											demod_dataen;
wire	[24:0]									demod_i;
wire	[24:0]									demod_q;


demod_iq										demod_iq_i
(
	.clk										(clk_25				),
	.demod_sin_rden								(demod_sin_rden	),
	.demod_sin_rdaddr							(demod_sin_rdaddr),
	.demod_sin_rddata							(demod_sin_rddata),
	.demod_cos_rden								(demod_cos_rden	),
	.demod_cos_rdaddr							(demod_cos_rdaddr),
	.demod_cos_rddata							(demod_cos_rddata),
	.apt_headen									(apt_headen			),
	.apt_head									(apt_head			),
	.apt_start									(apt_start			),
	.apt_last									(apt_last			),
	.apt_dataen									(apt_dataen			),
	.apt_data									(apt_data			),
	.demod_headen								(demod_headen		),
	.demod_head									(demod_head			),
	.demod_start								(demod_start		),
	.demod_last									(demod_last			),
	.demod_dataen								(demod_dataen		),
	.demod_i									(demod_i			),
	.demod_q									(demod_q			)
);
//--------------------------------------------------------------------------------------------------
wire											filter_headen;
wire	[15:0]									filter_head;
wire											filter_start;
wire											filter_last;
wire											filter_dataen;
wire	[15:0]									filter_i;
wire	[15:0]									filter_q;


filter_iq										filter_iq_i
(
	.clk										(clk_25				),
	.reset									(reset				),
	.rtc_rxen									(rtc_rxen			),
	.gain_shift									(gain_shift			),
	.filter_coef_rden							(filter_coef_rden	),
	.filter_coef_rdaddr							(filter_coef_rdaddr),
	.filter_coef_rddata							(filter_coef_rddata),
	.demod_headen								(demod_headen		),
	.demod_head									(demod_head			),
	.demod_start								(demod_start		),
	.demod_last									(demod_last			),
	.demod_dataen								(demod_dataen		),
	.demod_i									(demod_i			),
	.demod_q									(demod_q			),
	.filter_headen								(filter_headen		),
	.filter_head								(filter_head		),
	.filter_start								(filter_start		),
	.filter_last								(filter_last		),
	.filter_dataen								(filter_dataen		),
	.filter_i									(filter_i			),
	.filter_q									(filter_q			)
);

//--------------------------------------------------------------------------------------------------
wire											envelop_headen;
wire	[15:0]									envelop_head;
wire											envelop_start;
wire											envelop_last;
wire											envelop_dataen;
wire	[15:0]									envelop_data;

envelop_iq										envelop_iq_i
(
	.clk										(clk_25				),
	.rst										(reset				),
	.filter_headen								(filter_headen		),
	.filter_head								(filter_head		),
	.filter_start								(filter_start		),
	.filter_last								(filter_last		),
	.filter_dataen								(filter_dataen		),
	.filter_i									(filter_i			),
	.filter_q									(filter_q			),
	.envelop_headen								(envelop_headen		),
	.envelop_head								(envelop_head		),
	.envelop_start								(envelop_start		),
	.envelop_last								(envelop_last		),
	.envelop_dataen 							(envelop_dataen 	),
	.envelop_data								(envelop_data		)
);
//--------------------------------------------------------------------------------------------------
wire											down_headen;
wire	[15:0]									down_head;
wire											down_start;
wire											down_last;
wire											down_dataen;
wire	[15:0]									down_data;

downsample										downsample_i
(
	.clk										(clk_25				),
	.rst										(reset				),
	.env_headen									(envelop_headen		),
	.env_head									(envelop_head		),
	.env_start									(envelop_start		),
	.env_last									(envelop_last		),
	.env_dataen 								(envelop_dataen 	),
	.env_data									(envelop_data		),
	.down_headen								(down_headen		),
	.down_head									(down_head		),
	.down_start									(down_start		),
	.down_last									(down_last		),
	.down_dataen 								(down_dataen 	),
	.down_data									(down_data		)
);
//--------------------------------------------------------------------------------------------------
log_compress									log_compress_i
(
	.clk										(clk_25			),
	.rst										(reset			),
	.log1_coef_rden								(log1_coef_rden),
	.log1_coef_rdaddr							(log1_coef_rdaddr),
	.log1_coef_rddata							(log1_coef_rddata),
	.log2_coef_rden								(log2_coef_rden),
	.log2_coef_rdaddr							(log2_coef_rdaddr),
	.log2_coef_rddata							(log2_coef_rddata),
	.down_headen								(down_headen	),
	.down_head									(down_head		),
	.down_start									(down_start		),
	.down_last									(down_last		),
	.down_dataen 								(down_dataen 	),
	.down_data									(down_data		),
	.log_headen									(log_headen		),
	.log_head									(log_head		),
	.log_start									(log_start		),
	.log_last									(log_last		),
	.log_dataen 								(log_dataen 	),
	.log_data									(log_data		)
);
//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
endmodule
