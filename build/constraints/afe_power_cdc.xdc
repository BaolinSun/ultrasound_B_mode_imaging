# AFE-only crossings. No clock-group/whole-domain false paths.
# Raw counter Gray bits must reach first destination FF within one DCLK period.
set ap_raw_dst [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/raw_sync1_reg*/D}]
# A Gray MSB FF may legally merge with the binary counter MSB. Trace the
# actual launch pins, rather than silently omitting that merged bit by name.
set ap_raw_cells [get_cells -of_objects [all_fanin -flat -startpoints_only -to $ap_raw_dst]]
# Sequential clock pins are valid timing startpoints. Q would segment C->Q,
# hide the launch clock from CDC analysis, and is explicitly forbidden here.
set ap_raw_src [get_pins -of_objects $ap_raw_cells -filter {REF_PIN_NAME == C}]
set_max_delay -datapath_only 2.500 -from $ap_raw_src -to $ap_raw_dst
set_bus_skew 2.500 -from $ap_raw_src -to $ap_raw_dst
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/req_sync1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/ack_sync1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/ready_sync1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/mmcm_sync1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/idelay_sync1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/lock_rx1_reg/D}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/idelay_rx1_reg/D}]
# Epoch buses are held until the synchronized request/ACK completes.
set_max_delay -datapath_only 10.0 -from [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/epoch_reg*/C}] -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/rx_epoch_reg*/D}]
set_max_delay -datapath_only 10.0 -from [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/report_epoch_reg*/C}] -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/ack_epoch_reg*/D}]
# Only asynchronous reset assertion pins, not receiver data paths.
set ap_reset_pins [get_pins -hier -quiet -filter {NAME =~ */afe5832_adc_14b_16la_0/inst/rst_clkdiv_reg_reg*/PRE}]
set_false_path -to $ap_reset_pins
set ap_hold [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/hold_sync_reg*/PRE}]
set_false_path -to $ap_hold
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/rst_raw_sync_reg*/CLR}]
set_false_path -to [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/rst_rx_sync_reg*/CLR}]
