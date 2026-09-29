param()

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Workspace = Join-Path $Root 'workspace'
$Xsa = Join-Path $Root 'build\output\ultrasound_B_mode_imaging.xsa'
$Runner = Join-Path $PSScriptRoot 'run_vitis_2026_1.ps1'
$BackupRoot = Join-Path $Root 'build\workspace_backups'
$Timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$Backup = Join-Path $BackupRoot "workspace_before_2026_1_rebuild_$Timestamp"
$Failed = Join-Path $BackupRoot "workspace_failed_2026_1_rebuild_$Timestamp"

if (Get-Process -Name 'vitis-ide' -ErrorAction SilentlyContinue) {
    throw 'Close Vitis 2026.1 before rebuilding the workspace. The IDE locks its platform metadata and Python backend.'
}
if (-not (Test-Path -LiteralPath $Xsa -PathType Leaf)) {
    throw "XSA was not found: $Xsa"
}
if (-not (Test-Path -LiteralPath $Runner -PathType Leaf)) {
    throw "Vitis runner was not found: $Runner"
}

New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null
$WorkspaceWasMoved = $false

try {
    if (Test-Path -LiteralPath $Workspace) {
        Move-Item -LiteralPath $Workspace -Destination $Backup
        $WorkspaceWasMoved = $true
        Write-Host "VITIS_REPAIR: previous workspace backed up at $Backup"
    }

    & $Runner -Stage all

    $PlatformJson = Join-Path $Workspace 'ubmi_p\vitis-comp.json'
    $ApplicationJson = Join-Path $Workspace 'B_mode_focus_imaging\vitis-comp.json'
    $PlatformExport = Get-ChildItem -LiteralPath (Join-Path $Workspace 'ubmi_p\export') -Recurse -Filter '*.xpfm' -File -ErrorAction Stop
    $Fsbl = Join-Path $Root 'build\output\zynq_fsbl.elf'
    $Application = Join-Path $Root 'build\output\B_mode_focus_imaging.elf'

    foreach ($Required in $PlatformJson, $ApplicationJson, $Fsbl, $Application) {
        if (-not (Test-Path -LiteralPath $Required -PathType Leaf)) {
            throw "Required Vitis output was not generated: $Required"
        }
    }
    if (-not $PlatformExport) {
        throw 'The rebuilt platform did not generate an XPFM export.'
    }

    $Platform = Get-Content -Raw -LiteralPath $PlatformJson | ConvertFrom-Json
    $RecordedXsa = [System.IO.Path]::GetFullPath($Platform.configuration.xsa)
    if ($RecordedXsa -ne [System.IO.Path]::GetFullPath($Xsa)) {
        throw "Platform still references a stale XSA path: $RecordedXsa"
    }

    Write-Host 'VITIS_REPAIR_COMPLETE: platform, domains, FSBL, and application were rebuilt with Vitis 2026.1.'
    Write-Host "VITIS_REPAIR_BACKUP: $Backup"
}
catch {
    if (Test-Path -LiteralPath $Workspace) {
        Move-Item -LiteralPath $Workspace -Destination $Failed
    }
    if ($WorkspaceWasMoved -and (Test-Path -LiteralPath $Backup)) {
        Move-Item -LiteralPath $Backup -Destination $Workspace
    }
    throw
}
