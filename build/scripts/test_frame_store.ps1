param([string]$Vivado='D:/AMDDesignTools/2026.1/Vivado')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$output=Join-Path $root 'build/frame_store_sim'
New-Item -ItemType Directory -Force $output | Out-Null
Push-Location $output
try {
    & "$Vivado/bin/xvlog.bat" --sv "$root/build/src/imports/sources/capture_drain_tracker.sv" `
        "$root/build/src/sim/tb_capture_drain_tracker.sv" `
        "$root/ip_repo/xilinx.com_user_w_ddr_1.1/hdl/ddr_frame_store.sv" "$root/build/src/sim/tb_ddr_frame_store.sv"
    if($LASTEXITCODE) {throw 'RTL compilation failed'}
    & "$Vivado/bin/xvlog.bat" "$Vivado/data/verilog/src/glbl.v"
    if($LASTEXITCODE) {throw 'glbl compilation failed'}
    & "$Vivado/bin/xelab.bat" work.tb_capture_drain_tracker -s capture_drain_sim -debug typical
    if($LASTEXITCODE) {throw 'Capture drain elaboration failed'}
    $capture=& "$Vivado/bin/xsim.bat" capture_drain_sim -runall 2>&1
    $capture
    if($LASTEXITCODE -or !($capture -match 'PASS capture_drain_tracker') -or
       ($capture -match '(?i)^\s*(Error:|Fatal:|ERROR:)')) {throw 'Capture drain self-check failed'}
    & "$Vivado/bin/xvlog.bat" --sv "$root/build/src/imports/sources/process_top/apt_add.sv" `
        "$root/build/src/sim/tb_apt_add_pi.sv" "$root/build/src/sim/tb_capture_processing_tail.sv"
    if($LASTEXITCODE) {throw 'PI tail compilation failed'}
    & "$Vivado/bin/xelab.bat" work.tb_capture_processing_tail -s capture_processing_sim -debug typical
    if($LASTEXITCODE) {throw 'PI tail elaboration failed'}
    $pi=& "$Vivado/bin/xsim.bat" capture_processing_sim -runall 2>&1
    $pi
    if($LASTEXITCODE -or !($pi -match 'PASS capture_processing_tail') -or
       ($pi -match '(?i)^\s*(Error:|Fatal:|ERROR:)')) {throw 'PI tail self-check failed'}
    & "$Vivado/bin/xelab.bat" -L xpm -L unisims_ver work.tb_ddr_frame_store work.glbl -s frame_store_sim -debug typical
    if($LASTEXITCODE) {throw 'RTL elaboration failed'}
    $result=& "$Vivado/bin/xsim.bat" frame_store_sim -runall 2>&1
    $result
    if($LASTEXITCODE -or !($result -match 'PASS ddr_frame_store') -or
        ($result -match '(?i)^\s*(Error:|Fatal:|ERROR:)|XPM_CDC_HANDSHAKE S-')) {
        throw 'RTL self-check failed (including XPM assertions)'
    }
} finally { Pop-Location }
