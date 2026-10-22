# Independent STRUCTURAL CDC only, NOT implementation timing signoff.
# No legacy/full-design exceptions and no pin-based max-delay exclusions.
# The actual routed timing/Gray skew is checked by verify_afe_power.tcl.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
read_verilog -sv [file join $root build src imports sources afe_power_hw.sv]
synth_design -mode out_of_context -flatten_hierarchy none -top afe_power_hw -part xc7z030sbg485-2
create_clock -period 10.000 -name ap_ctrl [get_ports clk_ctrl]
create_clock -period 2.500 -name ap_raw [get_ports clk_raw]
create_clock -period 17.500 -name ap_rx [get_ports clk_rx]
set reports [file join $root build reports]
report_cdc -details -all_checks_per_endpoint -no_waiver -file [file join $reports afe_power_structural_cdc_raw.rpt]
# Same reviewed two-stage async-assert/sync-release reset structure as the
# implemented full-design hold_sync chain. No data-path waivers.
create_waiver -type CDC -id CDC-10 -from [get_pins {control_reg[1]/C}] \
    -to [get_pins {hold_sync_reg[0]/PRE}] -user AFE_design_review \
    -description {Global/local reset assertion combines before 2FF ASYNC_REG release chain; stop-clock/restart RTL self-check passes.}
report_cdc -details -all_checks_per_endpoint -file [file join $reports afe_power_structural_cdc.rpt]
report_utilization -file [file join $reports afe_power_structural_utilization.rpt]
set fp [open [file join $reports afe_power_structural_cdc.rpt] r]
set cdc [read $fp]
close $fp
if {[regexp -line {^CDC-[0-9]+\s+Critical} $cdc]} {error "Unreviewed structural AFE CDC critical"}
if {![regexp {CDC-15\s+Warning\s+64\s} $cdc] || ![regexp {CDC-6\s+Warning\s+1\s} $cdc]} {
    error "Expected both 32-bit held buses and the 8-bit Gray synchronizer in structural CDC report"
}
puts "AFE_STRUCTURAL_CDC_VERIFIED: 0 unwaived critical; held buses 32+32, Gray8; not routed timing"
