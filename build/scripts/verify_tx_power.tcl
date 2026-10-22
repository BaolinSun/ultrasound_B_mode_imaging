# Sign off the delivered run as-is. Never re-apply constraints to hide stale XDC.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
open_run impl_1
set block [get_cells -hier -quiet -filter {NAME =~ */tx_power_i}]
if {[llength $block]!=1} {error "TX power module missing/ambiguous"}
set report_dir [file join $root build reports]
report_utilization -cells $block -file [file join $report_dir tx_power_implemented_utilization.rpt]
report_timing_summary -file [file join $report_dir tx_power_implemented_timing.rpt]
report_cdc -no_waiver -details -all_checks_per_endpoint -file [file join $report_dir tx_power_implemented_cdc.rpt]
foreach stage {req_sync1 req_sync2 ack_sync1 ack_sync2 idle_sync1 idle_sync2} {
    set regs [get_cells -hier -quiet -filter "NAME =~ */tx_power_i/${stage}_reg"]
    if {[llength $regs]!=1 || ![get_property ASYNC_REG $regs]} {error "Missing CDC synchronizer $stage"}
}
# These new local registers also prove the bit contains the final held-data
# capture and registered idle, rather than an earlier debugging build.
foreach group {tx_control idle_tx_q} {
    if {![llength [get_cells -hier -quiet -filter "NAME =~ */tx_power_i/${group}_reg*"]]} {error "Stale TX RTL: missing $group"}
}
set forward_src [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/cmd_seq_reg*/C || NAME =~ */tx_power_i/payload_control_reg*/C}]
set forward_dst [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/report_cmd_seq_reg*/D || NAME =~ */tx_power_i/tx_control_reg*/D}]
set return_src [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/report_cmd_seq_reg*/C || NAME =~ */tx_power_i/report_flags_reg*/C || NAME =~ */tx_power_i/error_tx_reg/C}]
set return_dst [get_pins -hier -quiet -filter {NAME =~ */tx_power_i/ack_cmd_seq_reg*/D || NAME =~ */tx_power_i/flags_reg*/D || NAME =~ */tx_power_i/report_error_reg/D}]
foreach {label from to expected} [list forward $forward_src $forward_dst 34 return $return_src $return_dst 36] {
    set paths [get_timing_paths -from $from -to $to -max_paths 100]
    puts "TX_HELD_PATHS $label count=[llength $paths]"
    if {[llength $paths]<$expected} {error "Missing/false-pathed TX held data $label"}
    foreach path $paths {
        if {[get_property REQUIREMENT $path]>10.0001 || [get_property SLACK $path]<0} {error "TX held path not bounded/met"}
    }
    report_timing -from $from -to $to -max_paths 100 -file [file join $report_dir tx_power_${label}_timing.rpt]
}
source [file join $root build scripts verify_bf_ila.tcl]
set wns [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set whs [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
puts "TX_IMPLEMENTED_REVIEW WNS=$wns WHS=$whs"
if {$wns<=0 || $whs<=0} {error "Non-positive setup/hold slack"}
if {$wns<=0.5} {puts "WARNING: WNS below project 0.5ns target"}
# No new critical endpoints may be hidden among inherited CDC debt.
set f [open [file join $report_dir tx_power_implemented_cdc.rpt] r]
set text [read $f]
close $f
foreach line [split $text \n] {
    if {[regexp {CDC-[0-9]+\s+Critical} $line] && [string first /tx_power_i/ $line]>=0} {error "New TX critical CDC: $line"}
}
foreach ext {bit xsa} {
    set result [exec certutil -hashfile [file join $root build output ultrasound_B_mode_imaging.$ext] SHA256]
    if {![regexp {([a-fA-F0-9]{64})} $result -> hash]} {error "Could not hash reviewed $ext"}
    puts "TX_REVIEW_${ext}_SHA256=$hash"
}
puts "TX_IMPLEMENTED_VERIFIED: combine with unexcepted OOC CDC and RTL self-checks"
close_project
