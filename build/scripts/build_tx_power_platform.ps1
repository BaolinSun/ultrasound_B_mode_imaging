param([switch]$WaitForHardware)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$workspace=[IO.Path]::GetFullPath((Join-Path $root 'build/tx_power_platform_20261022'))
$allowed=[IO.Path]::GetFullPath((Join-Path $root 'build'))+[IO.Path]::DirectorySeparatorChar
if(!$workspace.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Platform workspace escapes build directory'}
# The generic generator can replace its workspace. Always require a NEW,
# exact, task-owned directory here; never delete the user's current workspace.
if(Test-Path -LiteralPath $workspace){throw "Platform workspace already exists; preserve it: $workspace"}
if($WaitForHardware) {
    $deadline=(Get-Date).AddMinutes(25)
    $log=Join-Path $root 'build/tx_power_fresh_impl.console.log'
    while(!(Test-Path -LiteralPath $log) -or !(Select-String -LiteralPath $log -Pattern '^TX_FRESH_IMPLEMENTATION_COMPLETE$' -Quiet)) {
        if((Get-Date) -ge $deadline){throw 'Final hardware did not finish; no platform generated'}
        Start-Sleep -Seconds 2
    }
}
# Recheck after waiting: another process must not populate this directory in
# the meantime, since the generic platform generator replaces its workspace.
if(Test-Path -LiteralPath $workspace){throw "Platform workspace appeared while waiting; preserve it: $workspace"}
$env:ULTRASOUND_VITIS_WORKSPACE=$workspace
Push-Location $root
try {
    & "$PSScriptRoot/run_vitis_2026_1.ps1" -Stage platform *> build/tx_power_platform.log
    if($LASTEXITCODE){throw 'Fresh platform generation failed'}
    $bsp=Join-Path $workspace 'ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0'
    & "$PSScriptRoot/build_frame_store_apps.ps1" -Bsp $bsp *> build/tx_power_apps_fresh_bsp.log
    if($LASTEXITCODE){throw 'Fresh BSP app build failed'}
    Write-Output "TX_PLATFORM_AND_APPS_COMPLETE: $workspace"
} finally {Pop-Location}
# No hardware target, flash, bootgen, or current-workspace replacement.
