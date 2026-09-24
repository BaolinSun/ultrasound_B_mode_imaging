# Compatibility entry point for the complete self-checking RTL suite.
set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set project_file [file join $root_dir vivado ultrasound_B_mode_imaging.xpr]

if {![file exists $project_file]} {
    error "Vivado project does not exist: $project_file"
}

source [file join $script_dir test_rtl.tcl]
