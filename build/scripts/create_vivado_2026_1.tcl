# Rebuild the hardware project from source assets with Vivado 2026.1.
# Usage: vivado -mode batch -source create_vivado_2026_1.tcl -tclargs project|build

proc fail {message} {
    puts stderr "MIGRATION_ERROR: $message"
    exit 1
}

proc collect_source_files {directory extensions} {
    set result {}
    foreach entry [glob -nocomplain -directory $directory *] {
        if {[file isdirectory $entry]} {
            set result [concat $result [collect_source_files $entry $extensions]]
        } elseif {[lsearch -exact $extensions [string tolower [file extension $entry]]] >= 0} {
            lappend result [file normalize $entry]
        }
    }
    return $result
}

proc assert_address_offset {expected label} {
    set found {}
    foreach segment [get_bd_addr_segs -quiet -hier] {
        set offset [get_property OFFSET $segment]
        if {[string equal -nocase $offset $expected]} {
            lappend found $segment
        }
    }
    if {[llength $found] == 0} {
        fail "$label address segment $expected was not found"
    }
    puts "MIGRATION_CHECK: $label=$expected ([join $found {, }])"
}

proc assert_pin_exists {pattern label} {
    set pins [get_pins -quiet $pattern]
    if {[llength $pins] == 0} {
        fail "$label pin pattern matched nothing: $pattern"
    }
    puts "MIGRATION_CHECK: $label matched [llength $pins] pin(s)"
}

proc write_ip_configuration {path} {
    set fh [open $path w]
    foreach ip [lsort [get_ips -quiet -all]] {
        set vlnv ""
        set locked ""
        # IP objects expose IPDEF in 2026.1; probing the legacy VLNV property
        # emits an ERROR even when wrapped by catch.
        catch {set vlnv [get_property IPDEF $ip]}
        catch {set locked [get_property IS_LOCKED $ip]}
        puts $fh "IP=$ip VLNV=$vlnv LOCKED=$locked"
        foreach property [lsort [list_property $ip]] {
            if {[string match "CONFIG.*" $property]} {
                set value ""
                catch {set value [get_property $property $ip]}
                puts $fh "$ip\t$property=$value"
            }
        }
    }
    close $fh
}

set script_dir [file dirname [file normalize [info script]]]
set root_dir [file dirname [file dirname $script_dir]]
set build_dir [file join $root_dir vivado]
set output_dir [file join $root_dir build output]
set report_dir [file join $root_dir build reports]
set project_name ultrasound_B_mode_imaging
set mode [expr {$argc > 0 ? [lindex $argv 0] : "build"}]

# OOC synthesis workers share the per-user Tcl Store on Windows. Disable
# background Git catalog refresh and start the initial IP runs serially to
# avoid tclapp::load_apps races during a clean Vivado 2026.1 build.
set_param tclapp.enableGitAccess 0

if {$mode ni {project build}} {
    fail "mode must be 'project' or 'build'"
}

file mkdir $build_dir
file mkdir $output_dir
file mkdir $report_dir

create_project -force $project_name $build_dir -part xc7z030sbg485-2
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property platform.design_intent.embedded true [current_project]
set_property platform.design_intent.external_host false [current_project]
set_property platform.design_intent.server_managed false [current_project]
set_property platform.design_intent.datacenter false [current_project]
set_property ip_repo_paths [list [file join $root_dir ip_repo]] [current_project]
update_ip_catalog

set rtl_files [collect_source_files [file join $root_dir build src imports] {.v .sv .vh}]
if {[llength $rtl_files] == 0} {
    fail "no RTL source files were found"
}
import_files -force -fileset sources_1 $rtl_files

set include_dirs [list [file join $build_dir ${project_name}.srcs sources_1 imports]]
set_property include_dirs $include_dirs [get_filesets sources_1]

set sim_files [collect_source_files [file join $root_dir build src sim] {.v .sv}]
if {[llength $sim_files] > 0} {
    import_files -force -fileset sim_1 $sim_files
    set_property top tb_rx_channel_reorder [get_filesets sim_1]
    update_compile_order -fileset sim_1
}

set all_xci_files [collect_source_files [file join $root_dir build src ip] {.xci}]
set xci_files {}
foreach xci $all_xci_files {
    set ip_name [file rootname [file tail $xci]]
    if {$ip_name in {ila_bf ila_timing ila_tx}} {
        puts "MIGRATION_INFO: excluding debug-only $ip_name from BASIC-license release build"
        continue
    }
    lappend xci_files $xci
}
if {[llength $xci_files] == 0} {
    fail "no standalone XCI files were found"
}
import_files -fileset sources_1 $xci_files

set managed_xci_files {}
foreach xci $xci_files {
    set managed_xci [get_files -quiet -all [file tail $xci]]
    if {[llength $managed_xci] != 1} {
        fail "expected one imported XCI for [file tail $xci], found [llength $managed_xci]"
    }
    lappend managed_xci_files [lindex $managed_xci 0]
}
set xci_files $managed_xci_files

set bd_file [file normalize [file join $root_dir build src bd pus_ctrl_system.bd]]
set xdc_file [file normalize [file join $root_dir build constraints pin.xdc]]
import_files -fileset sources_1 $bd_file
import_files -fileset constrs_1 $xdc_file
set bd_file [lindex [get_files -quiet -all pus_ctrl_system.bd] 0]
set xdc_file [lindex [get_files -quiet -all pin.xdc] 0]
if {$bd_file eq "" || $xdc_file eq ""} {
    fail "managed BD or XDC import failed"
}
set_property target_constrs_file $xdc_file [current_fileset -constrset]

open_bd_design $bd_file
source [file join $script_dir configure_sdio.tcl]
report_ip_status -file [file join $report_dir ip_status_before_upgrade.rpt]
write_ip_configuration [file join $report_dir ip_configuration_before_upgrade.txt]

set locked_ips {}
foreach ip [get_ips -quiet -all] {
    if {[get_property IS_LOCKED $ip]} {
        lappend locked_ips $ip
    }
}
if {[llength $locked_ips] > 0} {
    puts "MIGRATION_INFO: upgrading locked IP: [join $locked_ips {, }]"
    upgrade_ip $locked_ips
}
write_ip_configuration [file join $report_dir ip_configuration_after_upgrade.txt]

validate_bd_design

if {[llength [get_bd_cells -quiet -hier -filter {VLNV =~ "*:axi_dma:*"}]] != 0} {
    fail "AXI DMA is present, but this migration explicitly excludes DMA"
}
assert_address_offset 0x43C00000 AFE_AXI_LITE
assert_address_offset 0x40000000 AXI_BRAM
assert_address_offset 0x00000000 W_DDR_MASTER

set address_fh [open [file join $report_dir bd_address_map.txt] w]
foreach segment [get_bd_addr_segs -quiet -hier] {
    puts $address_fh "[get_property NAME $segment]\tOFFSET=[get_property OFFSET $segment]\tRANGE=[get_property RANGE $segment]"
}
close $address_fh

set ps [get_bd_cells -quiet processing_system7_0]
if {[llength $ps] != 1} {
    fail "processing_system7_0 was not found"
}
set ps_fh [open [file join $report_dir ps7_migration_properties.txt] w]
foreach property [lsort [list_property $ps]] {
    if {[string match "CONFIG.PCW_*" $property]} {
        puts $ps_fh "$property=[get_property $property $ps]"
    }
}
close $ps_fh

save_bd_design
generate_target all [get_files $bd_file]
foreach xci $xci_files {
    catch {generate_target all [get_files $xci]} generation_error
    if {$generation_error ne ""} {
        puts "MIGRATION_INFO: generate_target $xci: $generation_error"
    }
}

set wrapper_files [make_wrapper -files [get_files $bd_file] -top]
if {[llength $wrapper_files] == 0} {
    fail "HDL wrapper generation returned no file"
}
add_files -norecurse -fileset sources_1 $wrapper_files
set_property top heart_top [get_filesets sources_1]
update_compile_order -fileset sources_1
report_ip_status -file [file join $output_dir ip_status.rpt]

if {$mode eq "project"} {
    puts "MIGRATION_COMPLETE: project created at [file join $build_dir ${project_name}.xpr]"
    exit 0
}

# Keep timing closure reproducible in the 2026.1 rebuild.  This strategy adds
# post-route physical optimization; it does not relax any timing/CDC exception.
set_property strategy Performance_ExplorePostRoutePhysOpt [get_runs impl_1]

launch_runs synth_1 -jobs 1
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {
    fail "synthesis did not complete: [get_property STATUS [get_runs synth_1]]"
}

launch_runs impl_1 -to_step write_bitstream -jobs 6
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    fail "implementation/bitstream did not complete: [get_property STATUS [get_runs impl_1]]"
}

open_run impl_1
assert_pin_exists {pus_ctrl_system_wrapper_i/pus_ctrl_system_i/w_ddr_0/inst/w_ddr_v1_0_M00_AXI_inst/wr_ddr_start_ff_reg/D} W_DDR_CDC_STAGE1
assert_pin_exists {proc_top_i/scan_sync_i/rtc_enable_sync_ff1_reg/D} SCAN_ENABLE_CDC_STAGE1
assert_pin_exists {proc_top_i/beamform_active_sync_ff1_reg/D} BEAMFORM_ACTIVE_CDC_STAGE1
assert_pin_exists {proc_top_i/bproc_top_i/filter_iq_i/gain_shift_sync_ff1_reg[*]/D} GAIN_SHIFT_CDC_STAGE1
assert_pin_exists {proc_top_i/tx_top_i/tx_spi_rdreq_sync_reg[0]/D} TX_READ_REQUEST_CDC_STAGE1
assert_pin_exists {proc_top_i/para_top_i/para_reg_i/tx_spi_rxvalid_sync_reg[0]/D} TX_READ_COMPLETION_CDC_STAGE1
assert_pin_exists {pus_ctrl_system_wrapper_i/pus_ctrl_system_i/afe5832_adc_14b_16la_0/inst/rst_clkdiv_reg_reg[*]/PRE} ADC_RESET_SYNC_PRE
report_timing_summary -file [file join $output_dir timing_summary.rpt]
report_utilization -file [file join $output_dir utilization.rpt]
report_drc -file [file join $output_dir route_drc.rpt]
report_cdc -details -file [file join $output_dir cdc.rpt]
report_clock_interaction -file [file join $output_dir clock_interaction.rpt]
check_timing -verbose -file [file join $output_dir check_timing.rpt]

set bit_file [file join $build_dir ${project_name}.runs impl_1 heart_top.bit]
if {![file exists $bit_file]} {
    fail "expected bitstream does not exist: $bit_file"
}
file copy -force $bit_file [file join $output_dir heart_top.bit]
write_hw_platform -fixed -include_bit -force -file [file join $output_dir ultrasound_B_mode_imaging.xsa]

puts "MIGRATION_COMPLETE: bitstream and XSA exported to $output_dir"
exit 0
