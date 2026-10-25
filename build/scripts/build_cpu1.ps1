param([string]$Workspace='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(!$Workspace){$Workspace=Join-Path $root ('build/cpu1_platform_'+(Get-Date -Format 'yyyyMMdd_HHmmss'))}
$Workspace=[IO.Path]::GetFullPath($Workspace)
$allowed=[IO.Path]::GetFullPath((Join-Path $root 'build'))+[IO.Path]::DirectorySeparatorChar
if(!$Workspace.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $Workspace)){throw 'Require a NEW, task-owned build workspace; never replace the current IDE workspace'}
$old=$env:ULTRASOUND_VITIS_WORKSPACE
$env:ULTRASOUND_VITIS_WORKSPACE=$Workspace
Push-Location $root
try {
    & "$PSScriptRoot/run_vitis_2026_1.ps1" -Stage platform
    if($LASTEXITCODE){throw 'CPU1 platform/FSBL generation failed'}
    & "$PSScriptRoot/build_frame_store_apps.ps1" -Bsp (Join-Path $Workspace 'ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0')
    if($LASTEXITCODE){throw 'CPU1 application build failed'}
    Write-Output "CPU1_BUILDS_COMPLETE: $Workspace"
} finally {$env:ULTRASOUND_VITIS_WORKSPACE=$old;Pop-Location}
# Deliberately no bootgen, FPGA programming, JTAG or flash command.
