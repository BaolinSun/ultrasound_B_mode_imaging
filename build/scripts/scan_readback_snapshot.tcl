# Passive scan-control/DDR diagnosis. No halt, download, register write or reset.
connect
set cores [targets -filter {name == "ARM Cortex-A9 MPCore #0"} -target-properties]
if {[llength $cores] != 1} { error "Expected exactly one Zynq Cortex-A9 #0" }
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
puts "SCAN_TIMING_READ_UNSUPPORTED (data is not run/spi status):"
puts [mrd 0x40011528 3]
for {set n 0} {$n < 3} {incr n} {
    puts "DDR_SNAPSHOT $n"
    puts [mrd 0x20000000 16]
    after 200
}
disconnect
