# Only the FIRST stage of identified held single-bit crossings is excepted.
# XPM supplies its Gray pointer max-delay/bus-skew constraints.
# diag_cdc is an XPM_CDC_HANDSHAKE held 16-bit snapshot: XPM scopes the
# max-delay/bus-skew data constraints and first-stage synchronizer exceptions.
# Do NOT false-path the held data bus or sample the input flags directly.
# Preserve old-design CDC timing exclusions without masking the new FIFO.
# This is inherited timing debt, NOT certification of the old CDC circuits.
set fs_legacy_filter {NAME !~ *frame_store_i/* && NAME !~ proc_top_i/bf_sync* && NAME != proc_top_i/tail_pending_reg && NAME != proc_top_i/capture_busy_reg}
set fs_afe_clocks [get_clocks -include_generated_clocks dcp_0]
set fs_sys_clocks [get_clocks -include_generated_clocks sys_clk_p]
set fs_afe_legacy [filter [all_registers -clock $fs_afe_clocks] $fs_legacy_filter]
set fs_sys_legacy [filter [all_registers -clock $fs_sys_clocks] $fs_legacy_filter]
# Keep launch AND capture clock predicates: a dual-clock RAM belongs to
# both cell collections. Cell-to-cell exceptions would cut same-clock paths.
# This XDC is loaded only with the complete design, so all sets must exist.
set fs_afe_pins [get_pins -of_objects $fs_afe_legacy -filter {DIRECTION == IN && IS_CLOCK == 0}]
set fs_sys_pins [get_pins -of_objects $fs_sys_legacy -filter {DIRECTION == IN && IS_CLOCK == 0}]
set_false_path -from $fs_afe_clocks -through $fs_sys_pins -to $fs_sys_clocks
set_false_path -from $fs_sys_clocks -through $fs_afe_pins -to $fs_afe_clocks
set fs_sync [get_cells -hier -quiet -regexp {.*frame_store_i/(req_sync1|ack_sync1|ov_sync1|busy_sync1)_reg}]
set_false_path -to [get_pins -of_objects $fs_sync -filter {REF_PIN_NAME == D}]
set fs_bf [get_cells -hier -quiet -regexp {.*proc_top_i/bf_sync1_reg}]
set_false_path -to [get_pins -of_objects $fs_bf -filter {REF_PIN_NAME == D}]
# The reset input is asynchronous to clk_in by design. BOTH reset-synchronizer
# stages assert asynchronously, while release propagates through their D chain.
# Cut only their CLR pins; keep stage-to-stage D timing and every data path.
set fs_rst_sync [get_cells -hier -quiet -regexp {.*frame_store_i/rst_in_sync_reg\[[01]\]}]
set_false_path -to [get_pins -of_objects $fs_rst_sync -filter {REF_PIN_NAME == CLR}]
