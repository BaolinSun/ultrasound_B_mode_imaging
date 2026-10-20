# Final STA-only reset exception verification; does not alter routed logic.
# No hardware manager, JTAG, flash or BOOT.BIN operations.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
open_run impl_1
set rs [get_cells -hier -quiet -regexp {.*frame_store_i/rst_in_sync_reg\[[01]\]}]
if {[llength $rs]!=2} {error "Expected exactly two reset synchronizer stages"}
set rp [get_pins -of_objects $rs -filter {REF_PIN_NAME == CLR}]
if {[llength $rp]!=2} {error "Expected two asynchronous CLR endpoints"}
set_false_path -to $rp
set out [file join $root build reports]
report_timing_summary -file [file join $out sdio_timing.rpt]
report_cdc -details -file [file join $out sdio_cdc.rpt]
report_bus_skew -file [file join $out sdio_bus_skew.rpt]
report_methodology -file [file join $out sdio_methodology.rpt]
set final [file join $root build output]
write_checkpoint -force [file join $final frame_store_verified.dcp]
write_bitstream -force [file join $final ultrasound_B_mode_imaging.bit]
# write_hw_platform collects the run bit. Keep it byte-identical to delivery.
file copy -force [file join $final ultrasound_B_mode_imaging.bit] [file join [get_property DIRECTORY [get_runs impl_1]] heart_top.bit]
write_hw_platform -fixed -include_bit -force -file [file join $final ultrasound_B_mode_imaging.xsa]
write_debug_probes -force [file join $final ultrasound_B_mode_imaging.ltx]
puts "FRAME_STORE_FINALIZED"
close_project
