# Idempotent integration shared by managed-project and clean-rebuild flows.
set bf_ila_root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set bf_ila_ip [get_ips -quiet ila_bf]
if {![llength $bf_ila_ip]} {
    set bf_ila_managed [file join [get_property DIRECTORY [current_project]] \
        [get_property NAME [current_project]].srcs sources_1 ip ila_bf ila_bf.xci]
    if {[file exists $bf_ila_managed]} {
        add_files -norecurse -fileset sources_1 $bf_ila_managed
    } else {
        import_files -fileset sources_1 [file join $bf_ila_root build src ip ila_bf ila_bf.xci]
    }
    set bf_ila_ip [get_ips -quiet ila_bf]
}
if {[llength $bf_ila_ip] != 1} { error "Expected exactly one real ila_bf IP" }
if {[get_property IS_LOCKED $bf_ila_ip]} { upgrade_ip $bf_ila_ip }
set_property -dict [list CONFIG.C_NUM_OF_PROBES {5} CONFIG.C_DATA_DEPTH {8192} \
    CONFIG.C_PROBE0_WIDTH {17} CONFIG.C_PROBE1_WIDTH {2} CONFIG.C_PROBE2_WIDTH {17} \
    CONFIG.C_PROBE3_WIDTH {32} CONFIG.C_PROBE4_WIDTH {7} \
    CONFIG.C_INPUT_PIPE_STAGES {0}] $bf_ila_ip
generate_target all $bf_ila_ip
# A GUI that was open before integration can save an older XPR. Re-registering
# an already generated XCI alone does not recreate its missing OOC run/fileset.
if {![llength [get_runs -quiet ila_bf_synth_1]]} {
    create_ip_run -force [get_files [get_property IP_FILE $bf_ila_ip]]
    if {![llength [get_runs -quiet ila_bf_synth_1]]} {
        error "Failed to restore ila_bf OOC run"
    }
}
# Persist the upgraded IP definition as the canonical clean-rebuild input.
set bf_ila_ip_file [get_property IP_FILE $bf_ila_ip]
set bf_ila_canonical [file join $bf_ila_root build src ip ila_bf ila_bf.xci]
if {[file normalize $bf_ila_ip_file] ne [file normalize $bf_ila_canonical]} {
    file copy -force $bf_ila_ip_file $bf_ila_canonical
}
set bf_ila_hook [file join $bf_ila_root build scripts bf_ila_impl_pre.tcl]
if {![llength [get_files -quiet -all bf_ila_impl_pre.tcl]]} {
    add_files -norecurse -fileset utils_1 $bf_ila_hook
}
set_property STEPS.OPT_DESIGN.TCL.PRE $bf_ila_hook [get_runs impl_1]
puts "BF_ILA_CONFIGURED: real ila_bf, original 5 probes, depth 8192, input pipeline 0"
