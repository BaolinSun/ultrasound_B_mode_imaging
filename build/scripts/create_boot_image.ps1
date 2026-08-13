param(
    [string]$Bootgen = 'C:\AMDDesignTools\2026.1\Vivado\bin\bootgen.bat'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Output = Join-Path $Root 'build\output'
$Required = @(
    'zynq_fsbl.elf',
    'heart_top.bit',
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
}

$Bif = @'
the_ROM_image:
{
    [bootloader] zynq_fsbl.elf
    heart_top.bit
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
