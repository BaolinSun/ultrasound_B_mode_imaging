# Post-implementation, read-only review. Never opens a hardware target.
set ap_root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
open_project [file join $ap_root vivado ultrasound_B_mode_imaging.xpr]
open_run impl_1
# Review constraints EMBEDDED in the routed checkpoint. Do not repair/re-apply
# XDC here: doing so could mask that a delivered bit used stale constraints.
set ap_cell [get_cells -hier -quiet -filter {NAME =~ */afe_power_i}]
if {[llength $ap_cell]!=1} {error "AFE power module missing/ambiguous in implemented netlist"}
set ap_control [get_cells -hier -quiet -filter {NAME =~ */afe_power_i/control_reg*}]
if {[llength $ap_control]!=2} {error "AFE control registers missing/trimmed"}
foreach group {epoch rx_epoch report_epoch ack_epoch} {
    set regs [get_cells -hier -quiet -filter "NAME =~ */afe_power_i/${group}_reg* && REF_NAME =~ FD*"]
    puts "AFE_HANDSHAKE_REGISTERS $group count=[llength $regs]"
    if {[llength $regs]!=32} {error "AFE 32-bit handshake register $group missing/trimmed"}
}
foreach group {control_reg raw_count_reg raw_sync1_reg req_sync1_reg} {
    set cp [get_pins -hier -quiet -filter "NAME =~ */afe_power_i/${group}*/C"]
    set clks [get_clocks -of_objects $cp]
    if {![llength $clks]} {error "AFE register group $group has no timing clock"}
    puts "AFE_CLOCK $group = $clks"
}
set ap_raw_dst [get_pins -hier -quiet -filter {NAME =~ */afe_power_i/raw_sync1_reg*/D}]
if {[llength $ap_raw_dst]!=8} {error "Expected all eight Gray destination FFs"}
set ap_raw_cells [get_cells -of_objects [all_fanin -flat -startpoints_only -to $ap_raw_dst]]
set ap_raw_src [get_pins -of_objects $ap_raw_cells -filter {REF_PIN_NAME == C}]
if {[llength $ap_raw_src]!=8} {error "Expected eight actual Gray launch pins (including merged MSB)"}
set ap_raw_paths [get_timing_paths -from $ap_raw_src -to $ap_raw_dst -max_paths 8]
if {[llength $ap_raw_paths]!=8} {error "Gray paths are missing/false-pathed in the delivered checkpoint"}
foreach path $ap_raw_paths {
    if {[get_property REQUIREMENT $path]>2.500} {error "Gray max-delay is not bounded by one DCLK period"}
}
foreach name {req_sync1 ack_sync1 ready_sync1 mmcm_sync1 idelay_sync1 lock_rx1 idelay_rx1} {
    set p [get_pins -hier -quiet -filter "NAME =~ */afe_power_i/${name}_reg/D"]
    if {[llength $p]!=1} {error "Missing first CDC stage $name"}
}
foreach name {rst_raw_sync rst_rx_sync hold_sync} {
    set c [get_cells -hier -quiet -filter "NAME =~ */afe_power_i/${name}_reg*"]
    if {[llength $c]!=2} {error "Missing reset synchronization stages $name"}
}
set ap_reports [file join $ap_root build reports]
report_utilization -cells $ap_cell -file [file join $ap_reports afe_power_utilization.rpt]
report_bus_skew -file [file join $ap_reports afe_power_gray_skew.rpt]
report_timing -from $ap_raw_src -to $ap_raw_dst -max_paths 8 -file [file join $ap_reports afe_power_gray_timing.rpt]
report_exceptions -file [file join $ap_reports afe_power_exceptions.rpt]
report_cdc -no_waiver -summary -all_checks_per_endpoint -file [file join $ap_reports afe_power_cdc_summary.rpt]
report_cdc -no_waiver -details -all_checks_per_endpoint -file [file join $ap_reports afe_power_cdc_raw.rpt]
source [file join $ap_root build scripts afe_power_cdc_waivers.tcl]
report_cdc -details -all_checks_per_endpoint -file [file join $ap_reports afe_power_review_cdc.rpt]
report_cdc -waived -details -file [file join $ap_reports afe_power_cdc_waived.rpt]
set ap_fp [open [file join $ap_reports afe_power_review_cdc.rpt] r]
set ap_cdc_text [read $ap_fp]
close $ap_fp
foreach line [split $ap_cdc_text \n] {
    if {[regexp {^\s+[0-9]+\s+CDC-[0-9]+\s+Critical} $line] &&
        ([string first /afe_power_i/ $line]>=0 || [string first /inst/rst_clkdiv_reg_reg $line]>=0)} {
        error "Unwaived AFE CDC critical: $line"
    }
}
report_timing_summary -file [file join $ap_reports afe_power_review_timing.rpt]
set ap_setup [get_timing_paths -delay_type max -max_paths 1]
set ap_hold [get_timing_paths -delay_type min -max_paths 1]
set wns [get_property SLACK $ap_setup]
set whs [get_property SLACK $ap_hold]
puts "AFE_IMPLEMENTED_REVIEW: raw_launch=[llength $ap_raw_src] raw_dest=[llength $ap_raw_dst] WNS=$wns WHS=$whs"
if {$wns<=0 || $whs<=0} {error "Implementation timing does not meet positive setup/hold slack"}
if {$wns<=0.5} {puts "WARNING: WNS below the 0.5ns project target; investigate before release"}
close_project
