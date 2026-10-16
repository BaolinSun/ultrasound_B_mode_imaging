# Read-only DDR ring observation. No CPU halt, register write or FPGA programming.
connect
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
for {set n 0} {$n < 3} {incr n} {
    set words [mrd -value 0x20000000 256000]
    if {[llength $words] != 256000} { error "Incomplete DDR read" }
    puts [format "DDR_RING sample=%d bytes=1024000 crc32=%08x" $n [zlib crc32 [binary format I* $words]]]
    after 300
}
disconnect
