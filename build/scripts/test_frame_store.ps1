param([string]$Vivado='D:/AMDDesignTools/2026.1/Vivado')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$output=Join-Path $root 'build/frame_store_sim'
New-Item -ItemType Directory -Force $output | Out-Null
Push-Location $output
try {
    & "$Vivado/bin/xvlog.bat" --sv "$root/ip_repo/xilinx.com_user_w_ddr_1.1/hdl/ddr_frame_store.sv" "$root/build/src/sim/tb_ddr_frame_store.sv"
    if($LASTEXITCODE) {throw 'RTL compilation failed'}
    & "$Vivado/bin/xvlog.bat" "$Vivado/data/verilog/src/glbl.v"
    if($LASTEXITCODE) {throw 'glbl compilation failed'}
    & "$Vivado/bin/xelab.bat" -L xpm -L unisims_ver work.tb_ddr_frame_store work.glbl -s frame_store_sim -debug typical
    if($LASTEXITCODE) {throw 'RTL elaboration failed'}
    $result=& "$Vivado/bin/xsim.bat" frame_store_sim -runall 2>&1
    $result
    if($LASTEXITCODE -or !($result -match 'PASS ddr_frame_store') -or
        ($result -match '(?i)^\s*(Error:|Fatal:|ERROR:)|XPM_CDC_HANDSHAKE S-')) {
        throw 'RTL self-check failed (including XPM assertions)'
    }
} finally { Pop-Location }
