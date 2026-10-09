# Apply after open_bd_design; reusable by current-project and clean builds.
set bridge_ps [get_bd_cells processing_system7_0]
set_property -dict [list \
    CONFIG.PCW_SD0_PERIPHERAL_ENABLE {1} CONFIG.PCW_SD0_SD0_IO {MIO 40 .. 45} \
    CONFIG.PCW_SD0_GRP_CD_ENABLE {0} CONFIG.PCW_SD0_GRP_WP_ENABLE {0} CONFIG.PCW_SD0_GRP_POW_ENABLE {0} \
    CONFIG.PCW_SD1_PERIPHERAL_ENABLE {1} CONFIG.PCW_SD1_SD1_IO {MIO 10 .. 15} \
    CONFIG.PCW_SD1_GRP_CD_ENABLE {0} CONFIG.PCW_SD1_GRP_WP_ENABLE {0} CONFIG.PCW_SD1_GRP_POW_ENABLE {0}] $bridge_ps
validate_bd_design
foreach {property expected} {PCW_SD0_PERIPHERAL_ENABLE 1 PCW_SD1_PERIPHERAL_ENABLE 1 PCW_SD0_SD0_IO {MIO 40 .. 45} PCW_SD1_SD1_IO {MIO 10 .. 15} PCW_SPI1_PERIPHERAL_ENABLE 1} {
    if {[get_property CONFIG.$property $bridge_ps] ne $expected} { error "SDIO configuration mismatch: $property" }
}
save_bd_design
unset bridge_ps
