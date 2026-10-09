# Build the repaired project with Vivado 2026.1.

proc build_fail {message} {
    puts stderr "BUILD_ERROR: $message"
    exit 1
}

if {[version -short] ne "2026.1"} {
    build_fail "Vivado 2026.1 is required, got [version -short]"
}

set_param tclapp.enableGitAccess 0
set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set project_path [file join $root_dir vivado ultrasound_B_mode_imaging.xpr]

open_project $project_path
set bridge_bd [lindex [get_files -quiet -all pus_ctrl_system.bd] 0]
open_bd_design $bridge_bd
source [file join $script_dir configure_sdio.tcl]
generate_target all [get_files $bridge_bd]
if {[llength [get_files -quiet -all pus_ctrl_system_wrapper.v]] != 1} {
    build_fail "pus_ctrl_system_wrapper.v is not registered in sources_1"
}

reset_run synth_1
launch_runs synth_1 -jobs 1
wait_on_run synth_1
set synth_progress [get_property PROGRESS [get_runs synth_1]]
set synth_status [get_property STATUS [get_runs synth_1]]
puts "BUILD_SYNTHESIS: progress=$synth_progress status=$synth_status"
if {$synth_progress ne "100%" || [string match -nocase "*error*" $synth_status]} {
    build_fail "synthesis failed: $synth_status"
}

reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 1
wait_on_run impl_1
set impl_progress [get_property PROGRESS [get_runs impl_1]]
set impl_status [get_property STATUS [get_runs impl_1]]
puts "BUILD_IMPLEMENTATION: progress=$impl_progress status=$impl_status"
if {$impl_progress ne "100%" || [string match -nocase "*error*" $impl_status]} {
    build_fail "implementation failed: $impl_status"
}

puts "BUILD_COMPLETE: synthesis, implementation, and bitstream generation passed"
close_project
exit 0
