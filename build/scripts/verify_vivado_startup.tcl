# Fresh, in-memory OOC synthesis: no reset/rebuild of the released top-level run.
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set_param general.maxThreads 2
create_project -in_memory -part xc7z030sbg485-2
read_ip [file join $root vivado ultrasound_B_mode_imaging.srcs sources_1 ip mult_36x16 mult_36x16.xci]
foreach dcp [get_files -quiet -all -filter {FILE_TYPE == "Design Checkpoint"}] {
    set_property used_in_implementation false $dcp
}
# Deliberately call synth_design, not config_ip_cache -export.
synth_design -top mult_36x16 -part xc7z030sbg485-2 -mode out_of_context
write_checkpoint -force [file join $root build verification mult_36x16_startup_verified.dcp]
report_utilization -file [file join $root build reports mult_36x16_startup_utilization.rpt]
if {[get_msg_config -count -severity ERROR] != 0} { error "Vivado startup/OOC validation recorded errors" }
puts "VIVADO_STARTUP_OOC_VERIFIED: fresh mult_36x16 synthesis; zero ERROR messages"
close_project
exit
