param(
    [string]$ConfigRoot = (Join-Path $env:APPDATA 'Xilinx/Vivado'),
    [string]$BackupDirectory = ''
)
$ErrorActionPreference = 'Stop'

# Never terminate a GUI or rewrite a manifest while a worker may be using it.
if (Get-Process -Name vivado -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited }) {
    throw 'Close all Vivado GUI and batch processes before repairing startup settings.'
}
$ConfigRoot = [IO.Path]::GetFullPath($ConfigRoot)
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$template = Join-Path $PSScriptRoot 'Vivado_init_2026_1.tcl'
$initPath = Join-Path $ConfigRoot '2026.1/Vivado_init.tcl'
$manifestPath = Join-Path $ConfigRoot 'tclapp/manifest.tcl'
$templateText = [IO.File]::ReadAllText($template)

# Only replace our own previous guard. Preserve/refuse unknown personal scripts.
if (Test-Path -LiteralPath $initPath) {
    $initText = [IO.File]::ReadAllText($initPath)
    $knownLegacyHash = 'F56BA440BA04EB2EEA1C079B9A0045D328FB59461B9F5150E197B18B4E8E3FD4'
    if ($initText.Replace("`r`n", "`n") -cne $templateText.Replace("`r`n", "`n") -and
        (Get-FileHash -LiteralPath $initPath -Algorithm SHA256).Hash -ne $knownLegacyHash) {
        throw "Unknown personal init script; refusing to overwrite: $initPath"
    }
}

if (-not $BackupDirectory) {
    $BackupDirectory = Join-Path $projectRoot ('build/verification/vivado_startup_repair_' + (Get-Date -Format 'yyyyMMdd_HHmmssfff'))
}
$BackupDirectory = [IO.Path]::GetFullPath($BackupDirectory)
if (Test-Path -LiteralPath $BackupDirectory) { throw 'Backup directory must be new.' }
New-Item -ItemType Directory -Path $BackupDirectory | Out-Null
if (Test-Path -LiteralPath $initPath) {
    Copy-Item -LiteralPath $initPath -Destination (Join-Path $BackupDirectory 'Vivado_init.tcl')
}
$removed = 0
if (Test-Path -LiteralPath $manifestPath) {
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $BackupDirectory 'manifest.tcl')
    $lines = [IO.File]::ReadAllLines($manifestPath)
    # Exact obsolete app only; never evaluate manifest Tcl or remove other apps.
    $pattern = '^\s*::tclapp::load_apps?\s+-namespace\s+(?:\{aldec::activehdl\}|aldec::activehdl)\s+aldec::activehdl\s*;?\s*$'
    $kept = @($lines | Where-Object {
        if ($_ -cmatch $pattern) { $script:removed++; $false } else { $true }
    })
    if ($removed) {
        [IO.File]::WriteAllLines($manifestPath, [string[]]$kept, [Text.UTF8Encoding]::new($false))
    }
}
New-Item -ItemType Directory -Path (Split-Path $initPath -Parent) -Force | Out-Null
Copy-Item -LiteralPath $template -Destination $initPath -Force
Write-Host "VIVADO_STARTUP_REPAIRED: removed_legacy_entries=$removed"
Write-Host "Backup: $BackupDirectory"
Write-Host 'RTL, IP parameters, bitstreams and BOOT.BIN were not modified.'
