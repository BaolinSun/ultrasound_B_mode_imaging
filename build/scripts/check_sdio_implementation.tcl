# Read-only diagnostics against the implemented checkpoint; no RTL edits.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
open_run impl_1
report_cdc -file [file join $root build reports sdio_cdc.rpt]
report_bus_skew -file [file join $root build reports sdio_bus_skew.rpt]
report_methodology -file [file join $root build reports sdio_methodology.rpt]
close_project
