# Before opt_design: keep debug discovery independent of the AFE/DCLK domain.
# With one real ILA enabled, synthesis can retain the other empty debug modules
# as black boxes. They have no outputs/functional logic. Remove only these two
# exact, verified stubs before DRC; never remove a real debug core or datapath.
foreach bf_unused_path {proc_top_i/ila_timing_i proc_top_i/tx_top_i/ila_tx_i} {
    set bf_unused [get_cells -quiet $bf_unused_path]
    if {[llength $bf_unused]} {
        if {[llength $bf_unused] != 1 || ![get_property IS_BLACKBOX $bf_unused] ||
            [llength [get_pins -quiet -of_objects $bf_unused -filter {DIRECTION != IN}]]} {
            error "Refusing to remove non-empty debug instance: $bf_unused_path"
        }
        set_property DONT_TOUCH false $bf_unused
        remove_cell $bf_unused
        puts "BF_ILA_UNUSED_STUB_REMOVED: $bf_unused_path"
    }
}
set bf_debug_hub [get_debug_cores -quiet dbg_hub]
if {[llength $bf_debug_hub] != 1} { error "Real ILA has no debug hub" }
set bf_debug_clk [get_nets -quiet clk_100m]
if {[llength $bf_debug_clk] != 1} { error "Independent system clock clk_100m missing" }
set_property C_CLK_INPUT_FREQ_HZ 100000000 $bf_debug_hub
set_property C_ENABLE_CLK_DIVIDER false $bf_debug_hub
catch {disconnect_debug_port dbg_hub/clk}
connect_debug_port dbg_hub/clk $bf_debug_clk
puts "BF_ILA_HUB_CLOCK: 100 MHz independent system clock"
