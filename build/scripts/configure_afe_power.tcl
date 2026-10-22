# Minimal AFE-only receive reset/status; preserve original ADC MMCM parameters.
set ap_root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set ap_adc [file join $ap_root build src imports vivado Wearable_US Wearable_US.srcs sources_1 imports src afe5832_adc_14b_16lane.v]
foreach dst [get_files -quiet -all afe5832_adc_14b_16lane.v] {
    if {[file normalize $dst] ne [file normalize $ap_adc]} {file copy -force $ap_adc $dst}
}
set ap_hw [file join $ap_root build src imports sources afe_power_hw.sv]
if {![llength [get_files -quiet -all afe_power_hw.sv]]} {add_files -norecurse $ap_hw}
update_compile_order -fileset sources_1
update_module_reference -verbose [get_ips -all pus_ctrl_system_afe5832_adc_14b_16la_0_0]
foreach {source sink} {
    axi_bram_ctrl_0/bram_clk_a clk_ctrl
    axi_bram_ctrl_0/bram_addr_a power_addr
    axi_bram_ctrl_0/bram_wrdata_a power_wdata
    axi_bram_ctrl_0/bram_en_a power_en
    axi_bram_ctrl_0/bram_we_a power_we
} {
    set target [get_bd_pins afe5832_adc_14b_16la_0/$sink]
    if {![llength [get_bd_nets -quiet -of_objects $target]]} {connect_bd_net [get_bd_pins $source] $target}
}
if {![llength [get_bd_ports -quiet afe_power_rdata]]} {create_bd_port -dir O -from 31 -to 0 afe_power_rdata}
if {![llength [get_bd_nets -quiet -of_objects [get_bd_ports afe_power_rdata]]]} {
    connect_bd_net [get_bd_ports afe_power_rdata] [get_bd_pins afe5832_adc_14b_16la_0/power_rdata]
}
set ap_xdc [file join $ap_root build constraints afe_power_cdc.xdc]
if {![llength [get_files -quiet -all afe_power_cdc.xdc]]} {add_files -fileset constrs_1 $ap_xdc}
puts "AFE_POWER_CONFIGURED: UAF1 at 0x004c0300; only AFE receiver reset"
