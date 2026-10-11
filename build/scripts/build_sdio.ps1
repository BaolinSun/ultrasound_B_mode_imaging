param(
    [ValidateSet('hardware','software','all')][string]$Stage='all',
    [ValidateSet(25,50)][int]$SdioClockMHz=25
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$toolsRoot='D:/AMDDesignTools/2026.1'
if ($Stage -in @('hardware','all')) {
    Push-Location $projectRoot
    try {
        & "$toolsRoot/Vivado/bin/vivado.bat" -mode batch -source build/scripts/build_sdio_current.tcl `
            -log build/reports/sdio_build.log -journal build/reports/sdio_build.jou
        if ($LASTEXITCODE -ne 0) { throw 'SDIO hardware build failed' }
    } finally { Pop-Location }
}
if ($Stage -in @('software','all')) {
    # Never remove the user's current Vitis workspace. Use a fresh, dedicated
    # build directory because the legacy platform builder recreates its target.
    $previousWorkspace=$env:ULTRASOUND_VITIS_WORKSPACE
    $previousClock=$env:ULTRASOUND_SDIO_CLOCK_MHZ
    $temporaryWorkspace=Join-Path $projectRoot ('build/sdio_vitis_'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
    if (Test-Path -LiteralPath $temporaryWorkspace) { throw 'Temporary workspace must not already exist' }
    try {
        $env:ULTRASOUND_VITIS_WORKSPACE=$temporaryWorkspace
        $env:ULTRASOUND_SDIO_CLOCK_MHZ="$SdioClockMHz"
        & (Join-Path $PSScriptRoot 'run_vitis_2026_1.ps1') -Stage all
        if (-not $?) { throw 'Software build failed; refusing BOOT.BIN packing.' }
        & (Join-Path $PSScriptRoot 'create_boot_image.ps1')
    } finally {
        $env:ULTRASOUND_VITIS_WORKSPACE=$previousWorkspace
        $env:ULTRASOUND_SDIO_CLOCK_MHZ=$previousClock
    }
}
