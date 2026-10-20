set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set_param tclapp.enableGitAccess 0
open_project [file join $root vivado ultrasound_B_mode_imaging.xpr]
open_bd_design [lindex [get_files -all pus_ctrl_system.bd] 0]
source [file join $root build scripts configure_frame_store.tcl]
source [file join $root build scripts configure_sdio.tcl]
close_project
