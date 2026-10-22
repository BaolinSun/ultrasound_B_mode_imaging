# Offline fallback: reuse ONLY the final synthesis, never old placed logic.
# Incremental LUTRAM-shape reuse can fail; a fresh implementation is safer.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
if {[get_property PROGRESS [get_runs synth_1]] ne "100%" || [get_property NEEDS_REFRESH [get_runs synth_1]]} {
    error "Synthesis is missing/stale; rebuild before implementation"
}
set_property INCREMENTAL_CHECKPOINT {} [get_runs impl_1]
# Auto-incremental is already disabled in this project's run configuration.
# Clearing the explicit checkpoint avoids the incremental placer entirely.
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Fresh TX implementation failed"}
open_run impl_1
source [file join $root build scripts verify_bf_ila.tcl]
report_timing_summary -file [file join $root build reports sdio_timing.rpt]
report_utilization -file [file join $root build reports sdio_utilization.rpt]
report_cdc -details -file [file join $root build reports sdio_cdc.rpt]
report_drc -file [file join $root build reports sdio_drc.rpt]
report_methodology -file [file join $root build reports sdio_methodology.rpt]
write_hw_platform -fixed -include_bit -force -file [file join $root build output ultrasound_B_mode_imaging.xsa]
set bits [glob -directory [get_property DIRECTORY [get_runs impl_1]] *.bit]
if {[llength $bits]!=1} {error "Expected one current bitstream"}
file copy -force [lindex $bits 0] [file join $root build output ultrasound_B_mode_imaging.bit]
write_debug_probes -force [file join $root build output ultrasound_B_mode_imaging.ltx]
puts "TX_FRESH_IMPLEMENTATION_COMPLETE"
close_project
