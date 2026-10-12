connect
set cores [targets -filter {name == "ARM Cortex-A9 MPCore #0"} -target-properties]
if {[llength $cores] != 1} { error "Expected exactly one Zynq Cortex-A9 #0" }
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
puts "SD1 present/status/clock registers:"
puts [mrd 0xe0101024 1]
puts [mrd 0xe010102c 3]
disconnect
