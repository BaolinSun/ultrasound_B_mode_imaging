# Run the self-checking RX channel-map behavioral simulation.
set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set project_file [file join $root_dir vivado ultrasound_B_mode_imaging.xpr]

if {![file exists $project_file]} {
    error "Vivado project does not exist: $project_file"
}

open_project $project_file
set_property top tb_rx_channel_reorder [get_filesets sim_1]
update_compile_order -fileset sim_1
launch_simulation -simset sim_1 -mode behavioral
run all
close_sim
close_project
puts "CHANNEL_MAP_SIMULATION_COMPLETE"
