# Independent TX block is outside BD: no new reset or shared clock wiring.
set tp_root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set tp_hw [file join $tp_root build src imports sources tx_power_hw.sv]
if {![llength [get_files -quiet -all tx_power_hw.sv]]} {add_files -norecurse $tp_hw}
foreach name {tx_top.v tx_spi.v tx_cfg_top.v tx_cfg_streg.v tx_cfg_rtreg.v tx_cfg_rdreg.v} {
    set src [file join $tp_root build src imports sources tx_top $name]
    foreach dst [get_files -quiet -all $name] {
        if {[file normalize $src] ne [file normalize $dst]} {file copy -force $src $dst}
    }
}
set tp_xdc [file join $tp_root build constraints tx_power_cdc.xdc]
if {![llength [get_files -quiet -all tx_power_cdc.xdc]]} {add_files -fileset constrs_1 $tp_xdc}
puts "TX_POWER_CONFIGURED UTX1 at 0x004c0340; no AFE/clock/reset changes"
