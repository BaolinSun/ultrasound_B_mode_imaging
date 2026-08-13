set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set project_file [file join $root_dir vivado ultrasound_B_mode_imaging.xpr]
set output_file [file join $root_dir build output ultrasound_B_mode_imaging.xsa]

open_project $project_file
set_property platform.design_intent.embedded true [current_project]
set_property platform.design_intent.external_host false [current_project]
set_property platform.design_intent.server_managed false [current_project]
set_property platform.design_intent.datacenter false [current_project]
open_run impl_1
write_hw_platform -fixed -include_bit -force -file $output_file
puts "MIGRATION_COMPLETE: embedded XSA re-exported to $output_file"
exit 0
