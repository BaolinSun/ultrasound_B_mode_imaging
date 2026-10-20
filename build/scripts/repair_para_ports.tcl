# Offline BD-only repair. No synthesis/implementation or board operations.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
set bd [lindex [get_files -all -quiet pus_ctrl_system.bd] 0]
open_bd_design $bd
source [file join $root build scripts configure_para_ports.tcl]
configure_para_ports
save_reload_para_ports
generate_target all [get_files $bd]
make_wrapper -files [get_files $bd] -top
file copy -force $bd [file join $root build src bd pus_ctrl_system.bd]
puts "PARA_PORT_REPAIR_COMPLETE: BD and wrapper only; bit/XSA/BOOT unchanged"
close_project
exit
