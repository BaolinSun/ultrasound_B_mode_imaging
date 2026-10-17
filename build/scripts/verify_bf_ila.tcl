# Assert the real implemented core and unchanged original probe widths.
set bf_cores [get_debug_cores -quiet *ila_bf_i*]
if {[llength $bf_cores] != 1} { error "Implemented ila_bf_i is missing or ambiguous" }
set bf_expected_widths {17 2 17 32 7}
for {set i 0} {$i < 5} {incr i} {
    set bf_port [get_debug_ports -quiet [lindex $bf_cores 0]/probe$i]
    if {[llength $bf_port] != 1 || [get_property PORT_WIDTH $bf_port] != [lindex $bf_expected_widths $i]} {
        error "Original ILA probe$i missing or width changed"
    }
}
if {[llength [get_debug_ports -quiet [lindex $bf_cores 0]/probe5]]} { error "Unexpected extra ILA probe" }
set bf_unresolved [get_cells -quiet -hier -filter {IS_BLACKBOX == 1 && IS_PRIMITIVE == 0}]
if {[llength $bf_unresolved]} { error "Unresolved non-primitive black boxes: $bf_unresolved" }
puts "BF_ILA_VERIFIED: ila_bf_i exists with original probe widths 17/2/17/32/7"
