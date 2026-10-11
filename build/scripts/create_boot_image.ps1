param(
    [string]$Bootgen
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Tools = if ($env:AMD_DESIGN_TOOLS_2026_1) {
    [System.IO.Path]::GetFullPath($env:AMD_DESIGN_TOOLS_2026_1)
} else {
    'D:\AMDDesignTools\2026.1'
}
if (-not $Bootgen) {
    $Bootgen = Join-Path $Tools 'Vivado\bin\bootgen.bat'
}
$Output = Join-Path $Root 'build\output'
$Required = @(
    'zynq_fsbl.elf',
    'ultrasound_B_mode_imaging.bit',
    'B_mode_focus_imaging.elf'
)

if (-not (Test-Path -LiteralPath $Bootgen -PathType Leaf)) {
    throw "Bootgen was not found: $Bootgen"
}

$ResolvedOutput = [System.IO.Path]::GetFullPath($Output).TrimEnd('\') + '\'
foreach ($Name in $Required) {
    $Path = Join-Path $Output $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "BOOT.BIN input is missing: $Path"
    }
    $Resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not $Resolved.StartsWith($ResolvedOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "BOOT.BIN input is outside the new build output directory: $Resolved"
    }
    if ((Get-Item -LiteralPath $Resolved).Length -eq 0) {
        throw "BOOT.BIN input is empty: $Resolved"
    }
    Write-Host "BOOT.BIN input: $Resolved"
}

$Bif = @'
the_ROM_image:
{
    [bootloader] zynq_fsbl.elf
    ultrasound_B_mode_imaging.bit
    B_mode_focus_imaging.elf
}
'@

New-Item -ItemType Directory -Force -Path $Output | Out-Null
[System.IO.File]::WriteAllText(
    (Join-Path $Output 'boot.bif'),
    $Bif,
    [System.Text.UTF8Encoding]::new($false)
)

Push-Location $Output
try {
    & $Bootgen -arch zynq -image boot.bif -o BOOT.BIN -w on
    if ($LASTEXITCODE -ne 0) {
        throw "Bootgen failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$BootImage = Join-Path $Output 'BOOT.BIN'
if (-not (Test-Path -LiteralPath $BootImage -PathType Leaf) -or
    (Get-Item -LiteralPath $BootImage).Length -eq 0) {
    throw 'Bootgen did not produce a non-empty BOOT.BIN.'
}

Write-Host "BOOT image created: $BootImage"
# Persist exact absolute inputs and hashes with the generated artifacts.
$InputManifest = foreach ($Name in $Required) {
    $InputPath = [System.IO.Path]::GetFullPath((Join-Path $Output $Name))
    '{0}  {1}' -f (Get-FileHash -LiteralPath $InputPath -Algorithm SHA256).Hash, $InputPath
}
$InputManifest | Set-Content -LiteralPath (Join-Path $Output 'boot_inputs.sha256') -Encoding utf8
