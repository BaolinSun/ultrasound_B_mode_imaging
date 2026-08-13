set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set xsa [file join $root_dir build output ultrasound_B_mode_imaging.xsa]

hsi::open_hw_design $xsa
puts "XSA_PROCESSORS=[hsi::get_cells -hier -filter {IP_TYPE == PROCESSOR}]"
puts "XSA_PS7=[hsi::get_cells -hier -filter {IP_NAME == processing_system7}]"
foreach cell [hsi::get_cells -hier] {
    set ip_type [hsi::get_property IP_TYPE $cell]
    if {$ip_type ne ""} {
        puts "XSA_CELL=$cell IP_TYPE=$ip_type IP_NAME=[hsi::get_property IP_NAME $cell]"
    }
}
hsi::close_hw_design [hsi::current_hw_design]
exit 0
