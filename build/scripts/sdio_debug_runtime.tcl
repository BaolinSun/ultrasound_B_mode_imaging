# Read-only running-state capture. Address must come from nm for the exact
# loaded ELF (online followed by phase/seq/session/frame/offset/counters).
if {$argc != 1} { error "Provide exact ELF's online symbol address" }
set address [lindex $argv 0]
if {![string is entier -strict $address] || $address < 0x00100000 || $address >= 0x20000000} {
    error "Symbol address must be in application DDR"
}
connect
set cores [targets -filter {name == "ARM Cortex-A9 MPCore #0"} -target-properties]
if {[llength $cores] != 1} { error "Expected exactly one Zynq Cortex-A9 #0" }
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
puts "Running application state; no stop/download/reset:"
puts [mrd $address 12]
puts [mrd 0xe0101024 1]
puts [mrd 0xe010102c 3]
disconnect
