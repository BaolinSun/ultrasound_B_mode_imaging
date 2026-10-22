# Local held-data handshake only. Never exclude a whole clock/domain.
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/req_sync1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/ack_sync1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/idle_sync1_reg/D}]
set_max_delay -datapath_only 10.000 -from [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/payload_control_reg*/C || NAME =~ */tx_power_i/cmd_seq_reg*/C}] -to [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/tx_control_reg*/D || NAME =~ */tx_power_i/report_cmd_seq_reg*/D}]
set_max_delay -datapath_only 10.000 -from [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/report_cmd_seq_reg*/C || NAME =~ */tx_power_i/report_flags_reg*/C || NAME =~ */tx_power_i/error_tx_reg/C}] -to [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/ack_cmd_seq_reg*/D || NAME =~ */tx_power_i/flags_reg*/D || NAME =~ */tx_power_i/report_error_reg/D}]
