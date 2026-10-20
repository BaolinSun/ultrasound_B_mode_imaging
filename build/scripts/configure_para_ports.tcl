# BRAM_PORTA is exported entirely as scalar/vector ports, never as a partial
# external BRAM interface mixed with same-prefix explicit ports. Vivado 2026.1
# otherwise drops para_addr/en/we/din while reloading the saved BD.
# No datapath, clock frequency, address map or CDC circuit is changed here.
proc para_port_specs {} {
    return {
        bram_addr_a   para_addr  O 24 {}  ctrl_addr
        bram_wrdata_a para_din   O 32 {}  ctrl_wdata
        bram_en_a     para_en    O 1  {}  ctrl_en
        bram_we_a     para_we    O 4  {}  ctrl_we
        bram_clk_a    para_clk   O 1  clk {}
        bram_rst_a    para_rst   O 1  rst {}
        bram_rddata_a para_dout  I 32 {}  {}
    }
}
proc configure_para_ports {} {
    set old [get_bd_intf_ports -quiet para]
    if {[llength $old]} {delete_bd_objs $old}
    set old_net [get_bd_intf_nets -quiet -of_objects [get_bd_intf_pins axi_bram_ctrl_0/BRAM_PORTA]]
    foreach n $old_net {
        if {[llength [get_bd_intf_ports -quiet -of_objects $n]] ||
            [get_bd_intf_pins -quiet -of_objects $n] ne "/axi_bram_ctrl_0/BRAM_PORTA"} {
            error "Unexpected other connection on former BRAM interface $n"
        }
        delete_bd_objs $n
    }
    foreach {source name direction width type sink} [para_port_specs] {
        set pin [get_bd_pins axi_bram_ctrl_0/$source]
        set port [get_bd_ports -quiet $name]
        if {![llength $port]} {
            set args [list -dir $direction]
            if {$type ne ""} {lappend args -type $type}
            if {$width>1} {lappend args -from [expr {$width-1}] -to 0}
            set port [create_bd_port {*}$args $name]
        }
        set net [get_bd_nets -quiet -of_objects $pin]
        set existing [get_bd_nets -quiet -of_objects $port]
        if {[llength $existing] && $existing ne $net} {
            error "Refusing to replace an unexpected connection on $name: $existing"
        }
        if {![llength $existing]} {connect_bd_net $pin $port}
        if {$sink ne ""} {
            set input [get_bd_pins w_ddr_0/$sink]
            set net [get_bd_nets -of_objects $pin]
            set existing [get_bd_nets -quiet -of_objects $input]
            if {[llength $existing] && $existing ne $net} {
                error "Unexpected frame control connection on $sink: $existing"
            }
            if {![llength $existing]} {connect_bd_net $pin $input}
        }
    }
    # Same 100 MHz BRAM clock / active-high reset as the former interface.
    set_property CONFIG.FREQ_HZ 100000000 [get_bd_ports para_clk]
    set_property CONFIG.ASSOCIATED_RESET para_rst [get_bd_ports para_clk]
    set_property CONFIG.POLARITY ACTIVE_HIGH [get_bd_ports para_rst]
    assert_para_ports
}
proc assert_para_ports {} {
    if {[llength [get_bd_intf_ports -quiet para]]} {
        error "Partial external para interface must not coexist with explicit ports"
    }
    foreach {source name direction width type sink} [para_port_specs] {
        set p [get_bd_ports -quiet $name]
        if {[llength $p]!=1} {error "Missing/ambiguous external port $name"}
        if {[get_property DIR $p] ne $direction} {error "Wrong direction on $name"}
        set left [get_property LEFT $p]
        set right [get_property RIGHT $p]
        set actual 1
        if {$left ne "" && $right ne ""} {set actual [expr {abs($left-$right)+1}]}
        if {$actual!=$width} {error "Wrong width on $name: $actual, expected $width"}
        set net [get_bd_nets -quiet -of_objects [get_bd_pins axi_bram_ctrl_0/$source]]
        if {[llength $net]!=1 || [get_bd_nets -quiet -of_objects $p] ne $net} {
            error "External port $name is not connected to BRAM $source"
        }
        if {$sink ne "" && [get_bd_nets -quiet -of_objects [get_bd_pins w_ddr_0/$sink]] ne $net} {
            error "Frame-store $sink is not on the same BRAM net as $name"
        }
    }
    puts "PARA_PORTS_VERIFIED: seven ports, widths/directions and shared BRAM nets"
}
proc save_reload_para_ports {} {
    assert_para_ports
    validate_bd_design
    set files [get_files -all -quiet [get_property FILE_NAME [current_bd_design]]]
    if {[llength $files]!=1} {error "Cannot resolve the current BD absolute file path"}
    set path [lindex $files 0]
    save_bd_design
    close_bd_design [current_bd_design]
    open_bd_design $path
    # Deliberately only assert here: NEVER recreate missing ports after reload.
    assert_para_ports
    validate_bd_design
    puts "PARA_PORTS_ROUNDTRIP_VERIFIED"
}
