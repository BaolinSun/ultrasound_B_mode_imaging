# Cold-process read-only verification. Never invoke configure/repair here.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
open_project -read_only [file join $root vivado ultrasound_B_mode_imaging.xpr]
open_bd_design [lindex [get_files -all -quiet pus_ctrl_system.bd] 0]
source [file join $root build scripts configure_para_ports.tcl]
assert_para_ports
# validate_bd_design can write regenerated IP metadata, and is not legal in
# a read-only project. It already ran before/after save in the repair process.
puts "PARA_PORTS_COLD_LOAD_VERIFIED: no repair/save/regeneration performed"
close_project
exit
