# Offline OOC CDC review: do not use inherited whole-design exceptions here.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
read_verilog -sv [file join $root build src imports sources tx_power_hw.sv]
synth_design -mode out_of_context -flatten_hierarchy none -top tx_power_hw -part xc7z030sbg485-2
create_clock -period 10.0 -name tp_ctrl [get_ports clk_ctrl]
create_clock -period 5.0 -name tp_tx [get_ports clk_tx]
report_cdc -details -all_checks_per_endpoint -no_waiver -file [file join $root build reports tx_power_structural_cdc.rpt]
report_utilization -file [file join $root build reports tx_power_structural_utilization.rpt]
set f [open [file join $root build reports tx_power_structural_cdc.rpt] r]
set text [read $f]
close $f
if {[regexp -line {^CDC-[0-9]+\s+Critical} $text]} {error "Unreviewed TX structural CDC critical"}
puts "TX_STRUCTURAL_CDC_VERIFIED"
