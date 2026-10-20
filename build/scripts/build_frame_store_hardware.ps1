param([string]$Vivado='D:/AMDDesignTools/2026.1/Vivado')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# configure_frame_store installs a synthesis pre-hook limiting this build's
# runtime workers. Never rewrite shared manifests or change global APPDATA.
Push-Location $root
try {
    & "$Vivado/bin/vivado.bat" -mode batch -source build/scripts/build_sdio_current.tcl `
        -log build/frame_store_final.log -journal build/frame_store_final.jou
    if($LASTEXITCODE) {throw 'Vivado frame-store build failed'}
} finally {
    Pop-Location
}
# No bootgen, hardware target, flash or JTAG commands in this entrypoint.
