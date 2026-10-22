# Reviewed RESET-ONLY CDC-10 structures. No data/clock exceptions or blanket
# domain waivers. Keep the raw report (report_cdc -no_waiver) for comparison.
# Both reset requests may assert asynchronously; only the LAST stage of each
# ASYNC_REG shift chain releases its consumers, after 2/3 receiver clock edges.
# A glitch in reset combination can only add reset, never bypass that release.
set ap_reset_base pus_ctrl_system_wrapper_i/pus_ctrl_system_i/afe5832_adc_14b_16la_0/inst
foreach {launch capture reason} [list \
    "$ap_reset_base/afe_power_i/control_reg\[1\]/C" \
    "$ap_reset_base/afe_power_i/hold_sync_reg\[0\]/PRE" \
    {AFE RX hold: combined global/local async assertion; 2FF synchronous release. RTL stop-clock/restart self-check passes.} \
    {pus_ctrl_system_wrapper_i/pus_ctrl_system_i/proc_sys_reset_0/U0/ACTIVE_LOW_PR_OUT_DFF[0].FDRE_PER_N/C} \
    "$ap_reset_base/rst_clkdiv_reg_reg\[0\]/PRE" \
    {ADC local reset: combined global/local async assertion; original 3FF ASYNC_REG chain releases all receiver consumers synchronously.}] {
    set source [get_pins -quiet $launch]
    set dest [get_pins -quiet $capture]
    if {[llength $source]!=1 || [llength $dest]!=1} {error "AFE reset waiver endpoint changed: $launch -> $capture"}
    create_waiver -type CDC -id CDC-10 -from $source -to $dest \
        -user AFE_design_review -description $reason
}
