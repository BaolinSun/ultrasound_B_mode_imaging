set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set_param tclapp.enableGitAccess 0
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
set bd [lindex [get_files -quiet -all pus_ctrl_system.bd] 0]
open_bd_design $bd
source [file join $root build scripts configure_sdio.tcl]
generate_target all [get_files $bd]
update_compile_order -fileset sources_1
reset_run synth_1
launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} { error "SDIO synthesis failed" }
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} { error "SDIO implementation failed" }
open_run impl_1
report_timing_summary -file [file join $root build reports sdio_timing.rpt]
report_utilization -file [file join $root build reports sdio_utilization.rpt]
report_drc -file [file join $root build reports sdio_drc.rpt]
report_cdc -details -file [file join $root build reports sdio_cdc.rpt]
report_bus_skew -file [file join $root build reports sdio_bus_skew.rpt]
report_methodology -file [file join $root build reports sdio_methodology.rpt]
write_hw_platform -fixed -include_bit -force -file [file join $root build output ultrasound_B_mode_imaging.xsa]
set bits [glob -directory [get_property DIRECTORY [get_runs impl_1]] *.bit]
if {[llength $bits]!=1} { error "Expected one bitstream" }
file copy -force [lindex $bits 0] [file join $root build output ultrasound_B_mode_imaging.bit]
puts "SDIO_BUILD_COMPLETE"
close_project
