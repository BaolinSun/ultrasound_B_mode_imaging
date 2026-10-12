# User-authorized application-only download. Preserve current hardware/PS init.
connect
set cores [targets -filter {name == "ARM Cortex-A9 MPCore #0"} -target-properties]
if {[llength $cores] != 1} { error "Expected exactly one Zynq Cortex-A9 #0" }
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
stop
set root [file dirname [file dirname [file dirname [file normalize [info script]]]]]
dow [file join $root workspace B_mode_focus_imaging build B_mode_focus_imaging.elf]
con
puts "SDIO_DEBUG_APP_RUNNING"
disconnect
