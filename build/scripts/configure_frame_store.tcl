# Idempotent BD integration; no board access.
set fs_root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set fs_synth_hook [file join $fs_root build scripts frame_store_synth_pre.tcl]
set fs_existing_hook [get_property STEPS.SYNTH_DESIGN.TCL.PRE [get_runs synth_1]]
if {$fs_existing_hook ne "" && [file normalize $fs_existing_hook] ne [file normalize $fs_synth_hook]} {
    error "Refusing to replace a user synthesis pre-hook: $fs_existing_hook"
}
set_property STEPS.SYNTH_DESIGN.TCL.PRE $fs_synth_hook [get_runs synth_1]
update_ip_catalog -rebuild
upgrade_bd_cells [get_bd_cells w_ddr_0]
foreach {name direction width pin} {
    frame_header I 1 in_header
    frame_last I 1 in_last
    frame_busy I 1 in_busy
    frame_ctrl_rdata O 32 ctrl_rdata
} {
    if {![llength [get_bd_ports -quiet $name]]} {
        if {$width == 1} {create_bd_port -dir $direction $name} else {
            create_bd_port -dir $direction -from [expr {$width-1}] -to 0 $name
        }
    }
    if {![llength [get_bd_nets -quiet -of_objects [get_bd_pins w_ddr_0/$pin]]]} {
        connect_bd_net [get_bd_ports $name] [get_bd_pins w_ddr_0/$pin]
    }
}
# BRAM/DDR control share clk_100m; the read responder remains in heart_top.
source [file join $fs_root build scripts configure_para_ports.tcl]
configure_para_ports
save_reload_para_ports
generate_target all [get_files [get_property FILE_NAME [current_bd_design]]]
make_wrapper -files [get_files [get_property FILE_NAME [current_bd_design]]] -top
foreach name {heart_top.sv proc_top.sv} {
    set src [file join $fs_root build src imports sources $name]
    foreach dst [get_files -quiet -all $name] {
        if {[file normalize $src] ne [file normalize $dst]} {file copy -force $src $dst}
    }
}
set fs_pin_source [file join $fs_root build constraints pin.xdc]
foreach dst [get_files -quiet -all pin.xdc] {
    if {[file normalize $fs_pin_source] ne [file normalize $dst]} {file copy -force $fs_pin_source $dst}
}
set fs_xdc [file join $fs_root build constraints frame_store_cdc.xdc]
if {![llength [get_files -quiet -all frame_store_cdc.xdc]]} {add_files -fileset constrs_1 $fs_xdc}
set_property USED_IN_SYNTHESIS true [get_files -all frame_store_cdc.xdc]
set fs_bd [lindex [get_files -quiet -all pus_ctrl_system.bd] 0]
set fs_canonical [file join $fs_root build src bd pus_ctrl_system.bd]
if {[file normalize $fs_bd] ne [file normalize $fs_canonical]} {file copy -force $fs_bd $fs_canonical}
puts "FRAME_STORE_CONFIGURED: ABI UBF1, three 1MiB slots"
