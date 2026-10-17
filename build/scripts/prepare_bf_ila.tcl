set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set_param tclapp.enableGitAccess 0
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
source [file join $root build scripts configure_bf_ila.tcl]
update_compile_order -fileset sources_1
close_project
puts "BF_ILA_PREPARE_COMPLETE"
