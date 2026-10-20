# Read-only analysis of final routed checkpoint. Never opens a hardware target.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set run [file join $root vivado ultrasound_B_mode_imaging.runs impl_1]
set dcp [file join $run heart_top_routed.dcp]
set newer [file join $run heart_top_postroute_physopt.dcp]
if {[file exists $newer] && [file mtime $newer]>[file mtime $dcp]} {set dcp $newer}
set verified [file join $root build output frame_store_verified.dcp]
if {[file exists $verified] && [file mtime $verified]>[file mtime $dcp]} {set dcp $verified}
open_checkpoint $dcp
set out [file join $root build reports]
report_utilization -hierarchical -file [file join $out frame_store_hierarchy.rpt]
set core [get_cells -hier -quiet -filter {NAME =~ *frame_store_i}]
if {![llength $core]} {error "Frame-store hierarchy missing from routed design"}
report_utilization -cells $core -file [file join $out frame_store_resources.rpt]
set regs [get_cells -hier -quiet -filter {NAME =~ *frame_store_i/* && IS_SEQUENTIAL}]
if {![llength $regs]} {error "Frame-store registers missing"}
report_timing -to $regs -delay_type max -max_paths 20 -file [file join $out frame_store_setup.rpt]
report_timing -to $regs -delay_type min -max_paths 20 -file [file join $out frame_store_hold.rpt]
report_cdc -details -file [file join $out frame_store_all_cdc.rpt]
set fh [open [file join $out frame_store_all_cdc.rpt] r]
set cdc_text [read $fh]
close $fh
foreach line [split $cdc_text "\n"] {
    if {[regexp {CDC-(1|4|10|13)\s+Critical.*(frame_store_i/|proc_top_i/(bf_sync|capture_busy))} $line]} {
        error "Unresolved CDC Critical in new frame-store crossing: $line"
    }
}
report_clock_interaction -file [file join $out frame_store_clock_interaction.rpt]
# Inspect the asynchronous clock pair directly: the remaining paths must use
# XPM datapath-only limits, never fictitious synchronous setup relationships.
report_timing -from [get_clocks clk_out1_clk_gen] -to [get_clocks clkdiv2_mmcm] -max_paths 20 -file [file join $out frame_store_cross_sys_afe.rpt]
report_timing -from [get_clocks clkdiv2_mmcm] -to [get_clocks clk_out1_clk_gen] -max_paths 20 -file [file join $out frame_store_cross_afe_sys.rpt]
puts "FRAME_STORE_ANALYSIS_COMPLETE"
