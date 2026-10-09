# Normalize disabled Zynq-7000 peripheral parameters for Vivado 2026.1.
#
# A migrated PS7 customizer can write 0 to an inactive peripheral parameter
# even though processing_system7 v5.5 accepts only the sentinel value NA.
# Override the default list before sourcing this file when necessary:
#   set ps7_peripherals_to_disable {USB1 SD1 UART0}
#   source build/scripts/fix_ps7_peripheral_io_2026_1.tcl

proc ps7_io_fix_fail {message} {
    return -code error "PS7_IO_FIX_ERROR: $message"
}

if {[version -short] ne "2026.1"} {
    ps7_io_fix_fail "Vivado 2026.1 is required, got [version -short]"
}

if {[llength [get_projects -quiet]] == 0} {
    ps7_io_fix_fail "Open ultrasound_B_mode_imaging.xpr before sourcing this script"
}

if {![info exists ps7_peripherals_to_disable]} {
    set ps7_peripherals_to_disable {USB1}
}

set ps7_bd_file [lindex [get_files -quiet -all pus_ctrl_system.bd] 0]
if {$ps7_bd_file eq ""} {
    ps7_io_fix_fail "pus_ctrl_system.bd is not registered in the current project"
}

if {[current_bd_design -quiet] eq ""} {
    open_bd_design $ps7_bd_file
}

set ps7_cell [lindex [get_bd_cells -quiet processing_system7_0] 0]
if {$ps7_cell eq ""} {
    ps7_io_fix_fail "processing_system7_0 is not present in the current block design"
}

set ps7_properties [list_property $ps7_cell]
set ps7_all_updates {}
foreach peripheral $ps7_peripherals_to_disable {
    set peripheral [string toupper $peripheral]
    set updates {}

    set master_enable "CONFIG.PCW_EN_${peripheral}"
    if {[lsearch -exact $ps7_properties $master_enable] >= 0} {
        lappend updates $master_enable {0}
    }

    set prefix "CONFIG.PCW_${peripheral}_"
    foreach property $ps7_properties {
        if {![string match "${prefix}*" $property]} {
            continue
        }
        if {$property eq "${prefix}PERIPHERAL_ENABLE" ||
            [string match "*_IO" $property] ||
            [string match "*_ENABLE" $property]} {
            lappend updates $property {NA}
        }
    }

    if {[llength $updates] == 0} {
        ps7_io_fix_fail "No configurable properties were found for $peripheral"
    }

    lappend ps7_all_updates {*}$updates
    puts "PS7_IO_FIX: prepared $peripheral with [expr {[llength $updates] / 2}] normalized properties"
}

set_property -dict $ps7_all_updates $ps7_cell
validate_bd_design
save_bd_design

puts "PS7_IO_FIX_COMPLETE: disabled peripheral parameters use Vivado 2026.1 NA values."

unset -nocomplain ps7_bd_file ps7_cell ps7_properties ps7_all_updates master_enable prefix updates property peripheral
