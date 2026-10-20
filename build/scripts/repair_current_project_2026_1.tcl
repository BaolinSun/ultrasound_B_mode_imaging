# Repair the copied Vivado 2026.1 project by regenerating and registering the
# block-design HDL wrapper required by heart_top.

proc repair_fail {message} {
    puts stderr "REPAIR_ERROR: $message"
    exit 1
}

if {[version -short] ne "2026.1"} {
    repair_fail "Vivado 2026.1 is required, got [version -short]"
}

set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set project_path [file join $root_dir vivado ultrasound_B_mode_imaging.xpr]

open_project $project_path
set bd_file [lindex [get_files -quiet -all pus_ctrl_system.bd] 0]
if {$bd_file eq ""} {
    repair_fail "pus_ctrl_system.bd is missing from sources_1"
}

# The copied BD retained BMM metadata for a former internal blk_mem_gen_0.
# The current AXI BRAM controller is exported through explicit para_* ports, so
# keeping that stale reference makes write_bitstream try to initialize a cell
# that no longer exists in the implemented netlist.
open_bd_design $bd_file
source [file join $script_dir configure_frame_store.tcl]
source [file join $script_dir configure_sdio.tcl]
set_property HDL_ATTRIBUTE.BMM_INFO_ADDRESS_SPACE {} [get_bd_cells axi_bram_ctrl_0]
set_property HDL_ATTRIBUTE.BMM_INFO_PROCESSOR {} [get_bd_cells processing_system7_0]
save_bd_design

generate_target all [get_files $bd_file]
set wrapper_files [make_wrapper -files [get_files $bd_file] -top]
if {[llength $wrapper_files] == 0} {
    repair_fail "make_wrapper returned no files"
}

add_files -norecurse -fileset sources_1 $wrapper_files
set_property top heart_top [get_filesets sources_1]
update_compile_order -fileset sources_1
reset_run synth_1

puts "REPAIR_COMPLETE: registered [join $wrapper_files {, }]"
close_project
exit 0
