# Offline post-route acceptance of the newly introduced processing drain logic.
# No hardware target, register writes, reset, bootgen or downloads.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set run [file join $root vivado ultrasound_B_mode_imaging.runs impl_1]
set dcp [file join $run heart_top_routed.dcp]
set phys [file join $run heart_top_postroute_physopt.dcp]
if {[file exists $phys] && [file mtime $phys]>[file mtime $dcp]} {set dcp $phys}
open_checkpoint $dcp
set out [file join $root build reports]
set tracker [get_cells -hier -quiet -filter {NAME =~ *proc_top_i/capture_drain_i}]
if {[llength $tracker]!=1} {error "Expected the new capture_drain_i hierarchy"}
set regs [get_cells -hier -quiet -filter {NAME =~ *proc_top_i/capture_drain_i/* && IS_SEQUENTIAL}]
if {[llength $regs]<4} {error "Capture drain state was not found in routed netlist"}
report_utilization -cells $tracker -file [file join $out capture_drain_resources.rpt]
report_timing -to $regs -delay_type max -max_paths 20 -file [file join $out capture_drain_setup.rpt]
report_timing -to $regs -delay_type min -max_paths 20 -file [file join $out capture_drain_hold.rpt]
report_timing -from $regs -delay_type max -max_paths 20 -file [file join $out capture_drain_output_setup.rpt]
report_timing_summary -file [file join $out capture_drain_timing.rpt]
report_cdc -details -file [file join $out capture_drain_cdc.rpt]
report_bus_skew -file [file join $out capture_drain_bus_skew.rpt]
set f [open [file join $out capture_drain_cdc.rpt] r]
set contents [read $f];close $f
foreach line [split $contents "\n"] {
    if {[regexp {CDC-(1|4|10|13)\s+Critical.*(frame_store_i/|proc_top_i/(bf_sync|capture_drain_i/|bproc_top_i/trans_headen_drain_d))} $line]} {
        error "Unresolved CDC Critical affecting the drain path: $line"
    }
}
foreach delay {max min} {
    set paths [get_timing_paths -to $regs -delay_type $delay -max_paths 1]
    if {![llength $paths]} {error "Missing $delay timing path to drain registers"}
    set slack [get_property SLACK [lindex $paths 0]]
    puts "CAPTURE_DRAIN_${delay}_SLACK=$slack"
    if {$slack<=0} {error "Capture drain $delay timing violation"}
}
puts "CAPTURE_DRAIN_ANALYSIS_COMPLETE"
close_design
