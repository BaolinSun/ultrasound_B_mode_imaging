# Run every self-checking behavioral testbench imported by the 2026.1 project.
set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set project_file [file join $root_dir vivado ultrasound_B_mode_imaging.xpr]

if {![file exists $project_file]} {
    error "Vivado project does not exist: $project_file"
}

set tests {
    tb_rx_channel_reorder
    tb_add_rf_head_pi
    tb_apt_add_pi
    tb_demod_up_data_header
    tb_scan_sync_pi
    tb_tx_cfg_rtreg_pi
    tb_tx_cfg_top_capture_pi
    tb_filter_iq_gain
    tb_log_compress_sat
    tb_tx_readback_cdc
}

open_project $project_file
foreach test $tests {
    puts "RTL_TEST_BEGIN: $test"
    set_property top $test [get_filesets sim_1]
    update_compile_order -fileset sim_1
    launch_simulation -simset sim_1 -mode behavioral
    run all
    set sim_log [file join $root_dir vivado ultrasound_B_mode_imaging.sim sim_1 behav xsim simulate.log]
    if {![file exists $sim_log]} {
        error "Simulation log is missing for $test: $sim_log"
    }
    set log_fd [open $sim_log r]
    set log_text [read $log_fd]
    close $log_fd
    if {[regexp -line {^(Fatal:|ERROR:)} $log_text]} {
        error "Self-checking testbench failed: $test"
    }
    if {![regexp {PASS:} $log_text]} {
        error "Self-checking testbench did not report PASS: $test"
    }
    close_sim
    puts "RTL_TEST_PASS: $test"
}
close_project
puts "RTL_TEST_SUITE_COMPLETE"
